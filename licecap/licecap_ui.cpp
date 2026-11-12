/*
    LICEcap
    Copyright (C) 2010 Cockos Incorporated

    LICEcap is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    LICEcap is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with LICEcap; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#include <stdio.h>
#ifdef _WIN32
#include <windows.h>
#include <commctrl.h> // SetWindowSubclass for the dark-mode dialog controls
#include <process.h>
#include <multimon.h>
#include <shellapi.h>
#include <shlobj.h>   // IFileOpenDialog / SHBrowseForFolder for the folder picker
#else
#include "../WDL/swell/swell.h"
#endif
#include "../WDL/queue.h"
#include "../WDL/mutex.h"
#include "../WDL/wdlcstring.h"


//#define TEST_MULTIPLE_MODES

#ifdef REAPER_LICECAP
  #define NO_LCF_SUPPORT
  #define VIDEO_ENCODER_SUPPORT
  #include "../jmde/fx/lice_imported.h"
  #include "../jmde/reaper_plugin.h"
  #include "../jmde/video2/video_encoder.h"
  bool WDL_ChooseFileForSave(HWND parent, const char *text, const char *initialdir, const char *initialfile, const char *extlist,const char *defext,bool preservecwd,char *fn, int fnsize,const char *dlgid=NULL, void *dlgProc=NULL, void *hi=NULL);
  char *WDL_ChooseFileForOpen(HWND parent, const char *text, const char *initialdir, const char *initialfile, const char *extlist,const char *defext,bool preservecwd,bool allowmul,const char *dlgid=NULL, void *dlgProc=NULL, void *hi=NULL);

  void *(*reaperAPI_getfunc)(const char *p);
  int (*Audio_RegHardwareHook)(bool isAdd, audio_hook_register_t *reg); // return >0 on success
  REAPER_Resample_Interface *(*Resampler_Create)();
  void OnAudioBuffer(bool isPost, int len, double srate, struct audio_hook_register_t *reg); // called twice per frame, isPost being false then true

  unsigned WINAPI audioEncodeThread(void *p);
  
  audio_hook_register_t s_audiohook = { OnAudioBuffer };
  bool s_audiohook_reg;
  double s_audiohook_timepos;
  double s_audiohook_needsilence;
  WDL_TypedQueue<ReaSample> s_audiohook_samples;
  WDL_Mutex s_audiohook_samples_mutex;
  HANDLE s_audiothread;
  REAPER_Resample_Interface *s_rs;

#else

  #define LICE_CreateSysBitmap(w,h) new LICE_SysBitmap(w,h)
  #define LICE_CreateMemBitmap(w,h) new LICE_MemBitmap(w,h)

  #include "../WDL/lice/lice_lcf.h"
  #include "../WDL/filebrowse.h"
#endif




#include "../WDL/wdltypes.h"
#include "../WDL/wingui/wndsize.h"
#include "../WDL/wdlstring.h"


#include "licecap_version.h"

#include "resource.h"
#include "licecap_ui.h"

HINSTANCE g_hInst;



class gif_encoder
{

  LICE_IBitmap *lastbm; // set if a new frame is in progress
  void *ctx; 

  int lastbm_coords[4]; // coordinates of previous frame which need to be updated, [2], [3] will always be >0 if in progress
  int lastbm_accumdelay; // delay of previous frame which is latent
  int loopcnt;
  LICE_pixel trans_mask;

public:


  gif_encoder(void *gifctx, int use_loopcnt, int trans_chan_mask=0xff)
  {
    lastbm = NULL;
    memset(lastbm_coords,0,sizeof(lastbm_coords));
    lastbm_accumdelay = 0;
    ctx=gifctx;
    loopcnt=use_loopcnt;
    trans_mask = LICE_RGBA(trans_chan_mask,trans_chan_mask,trans_chan_mask,0);
  }
  ~gif_encoder()
  {
    frame_finish();
    LICE_WriteGIFEnd(ctx);
    delete lastbm;
  }
  
  
  bool frame_compare(LICE_IBitmap *bm, int diffs[4])
  {
    diffs[0]=diffs[1]=0;
    diffs[2]=bm->getWidth();
    diffs[3]=bm->getHeight();
    return !lastbm || LICE_BitmapCmpEx(lastbm, bm, trans_mask,diffs);
  }
  
  void frame_finish()
  {
    if (ctx && lastbm && lastbm_coords[2] > 0 && lastbm_coords[3] > 0)
    {
      LICE_SubBitmap bm(lastbm, lastbm_coords[0],lastbm_coords[1], lastbm_coords[2],lastbm_coords[3]);
      
      int del = lastbm_accumdelay;
      if (del<1) del=1;
      LICE_WriteGIFFrame(ctx,&bm,lastbm_coords[0],lastbm_coords[1],true,del,loopcnt);
    }
    lastbm_accumdelay=0;
    lastbm_coords[2]=lastbm_coords[3]=0;
  }
  
  void frame_advancetime(int amt)
  {
    lastbm_accumdelay+=amt;
  }
  
  void frame_new(LICE_IBitmap *ref, int x, int y, int w, int h)
  {
    if (w > 0 && h > 0)
    {
      frame_finish();
    
      lastbm_coords[0]=x;
      lastbm_coords[1]=y;
      lastbm_coords[2]=w;
      lastbm_coords[3]=h;
    
      if (!lastbm) lastbm = LICE_CreateMemBitmap(ref->getWidth(), ref->getHeight());
      LICE_Blit(lastbm, ref, x, y, x,y, w,h, 1.0f, LICE_BLIT_MODE_COPY);
    }
  }
  
  void clear_history() // forces next frame to be a fully new frame
  {
    frame_finish();
    delete lastbm;
    lastbm=NULL;
  }
  LICE_IBitmap *prev_bitmap() { return lastbm; }
};



int g_prefs; // &1=title frame, &2=giant font, &4=record mousedown, &8=timeline, &16=shift+space pause, &32=transparency-fu
int g_stop_after_msec;


int g_gif_loopcount=0;
int g_max_fps=8;  

char g_last_fn[2048];
WDL_String g_ini_file;

HWND g_hwnd;

// ---- GifCam-style UI state ------------------------------------------------
char g_pending_fn[2048];    // finished capture that is not saved yet
bool g_pending_unsaved;     // g_pending_fn is a temporary file
static bool g_rec_is_temp;         // current capture writes to a temp file
char g_last_save_fn[2048];  // last path used by "Save As"
// GIF opened from disk for editing. The editor always works on a temporary
// copy (that copy is the pending recording), so the original is never touched;
// this path is only remembered to name and locate the file "Save As" offers.
static char g_open_src_fn[2048];
static char g_startup_gif[2048];   // GIF named on the command line, opened once
bool g_capture_cursor = true;
bool g_always_on_top;
static HHOOK g_kbd_hook;

// Theme state (g_dark_ui, g_ui_font, ...) lives in ui_theme.cpp and is
// declared in licecap_ui.h, together with the rest of the UI modules.

typedef struct {
  DWORD   cbSize;
  DWORD   flags;
  HCURSOR hCursor;
  POINT   ptScreenPos;
} pCURSORINFO, *pPCURSORINFO, *pLPCURSORINFO;

#ifdef _WIN32

static void __LogicalToPhysicalPointForPerMonitorDPI(HWND hwnd, POINT *pt)
{
  static BOOL (WINAPI *ltppfpmd)(HWND hwnd, LPPOINT lpPoint);
  static bool tried;
  if (!tried)
  {
    *(void **)&ltppfpmd = GetProcAddress(GetModuleHandle("USER32"),"LogicalToPhysicalPointForPerMonitorDPI");
    tried=true;
  }
  if (ltppfpmd) ltppfpmd(hwnd,pt);
}

void DoMouseCursor(LICE_IBitmap* sbm, HWND h, int xoffs, int yoffs)
{
  // XP+ only
  if (!g_capture_cursor) return;

  static BOOL (WINAPI *pGetCursorInfo)(pLPCURSORINFO);
  static bool tr;
  if (!tr)
  {
    tr=true;
    HINSTANCE hUser=LoadLibrary("USER32.dll");
    if (hUser)
      *(void **)&pGetCursorInfo = (void*)GetProcAddress(hUser,"GetCursorInfo");
  }

  if (pGetCursorInfo)
  {
    pCURSORINFO ci={sizeof(ci)};
    pGetCursorInfo(&ci);
    if (ci.flags && ci.hCursor)
    {
      ICONINFO inf={0,};
      GetIconInfo(ci.hCursor,&inf);

      __LogicalToPhysicalPointForPerMonitorDPI(g_hwnd,&ci.ptScreenPos);

      int mousex = ci.ptScreenPos.x+xoffs;
      int mousey = ci.ptScreenPos.y+yoffs;

      if ((g_prefs&4) && ((GetAsyncKeyState(VK_LBUTTON)&0x8000) || (GetAsyncKeyState(VK_RBUTTON)&0x8000)))
      {
        LICE_Circle(sbm, mousex+1, mousey+1, 10.0f, LICE_RGBA(0,0,0,255), 1.0f, LICE_BLIT_MODE_COPY, true);
        LICE_Circle(sbm, mousex+1, mousey+1, 9.0f, LICE_RGBA(255,255,255,255), 1.0f, LICE_BLIT_MODE_COPY, true);
      }

      DrawIconEx(sbm->getDC(),mousex-inf.xHotspot,mousey-inf.yHotspot,ci.hCursor,0,0,0,NULL,DI_NORMAL);
      if (inf.hbmColor) DeleteObject(inf.hbmColor);
      if (inf.hbmMask) DeleteObject(inf.hbmMask);
    }
  }
}
#endif

#ifdef __APPLE__
  bool GetAsyncNSWindowRect(HWND hwnd, RECT *r);
  POINT s_async_offset;
  bool s_has_async_offset;
#endif

void MakeTimeStr(int sec, char* buf, int w, int h, int* timepos)
{
  if (sec < 0) sec=0;
  sprintf(buf, "%d:%02d", sec/60, sec%60);
  LICE_MeasureText(buf, &timepos[2], &timepos[3]);
  timepos[2] += 8;
  timepos[3] += 8;
  timepos[0] = w-timepos[2];
  timepos[1] = h-timepos[3];
}

void draw_timedisp(LICE_IBitmap *bm, int sec, int timeposout[4], int bw, int bh)
{
  char timestr[256];
  int timepos[4]; // x,y,w,h
  MakeTimeStr(sec, timestr, bw, bh, timepos);
  if (bm)
  {
    LICE_FillRect(bm, timepos[0], timepos[1], timepos[2], timepos[3], LICE_RGBA(0,0,0,255), 1.0f, LICE_BLIT_MODE_COPY);
    LICE_DrawText(bm, timepos[0]+4, timepos[1]+4, timestr, LICE_RGBA(255,255,255,255), 1.0f, LICE_BLIT_MODE_COPY);
  }
  if (timeposout) memcpy(timeposout,timepos,sizeof(timepos));
}


#undef GetSystemMetrics

void my_getViewport(RECT *r, RECT *sr, bool wantWork) 
{
#ifdef _WIN32
  if (sr) 
  {
	  static HINSTANCE hlib;
    static bool haschk;
    
    if (!haschk && !hlib) { hlib=LoadLibrary("user32.dll");haschk=true; }

	  if (hlib) 
    {

      static HMONITOR (WINAPI *Mfr)(LPCRECT lpcr, DWORD dwFlags);
      static BOOL (WINAPI *Gmi)(HMONITOR mon, MONITORINFOEX* lpmi);

      if (!Mfr) Mfr = (HMONITOR (WINAPI *)(LPCRECT, DWORD)) GetProcAddress(hlib, "MonitorFromRect");
      if (!Gmi) Gmi = (BOOL (WINAPI *)(HMONITOR,MONITORINFOEX*)) GetProcAddress(hlib,"GetMonitorInfoA");    

			if (Mfr && Gmi) {
			  HMONITOR hm;
			  hm=Mfr(sr,MONITOR_DEFAULTTONULL);
        if (hm) {
          MONITORINFOEX mi;
          memset(&mi,0,sizeof(mi));
          mi.cbSize=sizeof(mi);

          if (Gmi(hm,&mi)) {
            if (wantWork)
              *r=mi.rcWork;
            else *r=mi.rcMonitor;
            return;
          }          
        }
			}
		}
	}
  if (wantWork)
    SystemParametersInfo(SPI_GETWORKAREA,0,r,0);
  else
    GetWindowRect(GetDesktopWindow(), r);
#else
  return SWELL_GetViewPort(r,sr,wantWork);
#endif
}

void union_diffs(int a[4], const int b[4]) 
{
  if (a[2] < 1 || a[3] < 1) 
  {
    memcpy(a, b, 4*sizeof(int));
  }
  else if (b[2] > 0 && b[3] > 0)
  {
    a[2] += a[0]; // a2/3 are right/bottom
    a[3] += a[1];

    if (a[0] > b[0]) a[0] = b[0]; // left/top edges
    if (a[1] > b[1]) a[1] = b[1];

    if (a[2] < b[0] + b[2]) a[2] = b[0] + b[2]; // adjust right/bottom edges
    if (a[3] < b[1] + b[3]) a[3] = b[1] + b[3];
    

    a[2] -= a[0]; // a2/3 back to width/height
    a[3] -= a[1];
  }
  else
  {
    // do nothing, a is valid b is not
  }
}

void EnsureNotCompletelyOffscreen(RECT *r)
{
  RECT scr;
  my_getViewport(&scr, r,true);
  if (r->right < scr.left+20 || r->left >= scr.right-20 ||
      r->bottom < scr.top+20 || r->top >= scr.bottom-20)
  {
    r->right -= r->left;
    r->bottom -= r->top;
    r->left = 0;
    r->top = 0;
  }
}


bool g_frate_valid;
double g_frate_avg;
DWORD g_ms_written;
int g_cap_state; // 1=rec, 2=pause

#define PREROLL_AMT 3000
DWORD g_cap_prerolluntil;
DWORD g_last_frame_capture_time;

#ifndef NO_LCF_SUPPORT
LICECaptureCompressor *g_cap_lcf;
#endif

#ifdef VIDEO_ENCODER_SUPPORT
VideoEncoder *g_cap_video;
char g_cap_video_ext[32];
double g_cap_video_lastt;
int g_cap_video_vbr=256, g_cap_video_abr=64;

class videoFrameWrap : public IVideoFrame
{
public:
  videoFrameWrap(LICE_IBitmap *bmp)  {
    m_refcnt = 1;
    m_w = bmp->getWidth();
    m_h = bmp->getHeight();
    m_bits = (char *)malloc(32 + m_w*m_h * 4);

    char *bits = get_bits(); // aligned copy
    if (bits)
    {
      const char *ptr = (const char *)bmp->getBits();
      int rs = bmp->getRowSpan() * 4;
      if (bmp->isFlipped())
      {
        ptr += (bmp->getHeight() - 1)*rs;
        rs = -rs;
      }
      int y;
      for (y = 0; y < m_h; y++)
      {
        memcpy(bits, ptr, m_w * 4);
        ptr += rs;
        bits += m_w * 4;
      }
    }
  }
  ~videoFrameWrap() { free(m_bits);  }
  int m_w, m_h;

  char *m_bits;
  int m_refcnt;

  virtual void AddRef() { m_refcnt++; };
  virtual void Release() { if (!--m_refcnt) delete this; };
  virtual char *get_bits() { return (char*)((((INT_PTR)m_bits) + 31)&~31); }
  virtual int get_w() { return m_w;  }
  virtual int get_h() { return m_h;  }
  virtual int get_fmt() { return 'RGBA'; }
  virtual int get_rowspan() { return m_w * 4; }
  virtual void resize_img(int wantw, int wanth, int wantfmt) {}
};


void EncodeFrameToVideo(VideoEncoder *enc, LICE_IBitmap *bm, bool force=false)
{
  if (!enc||!bm) return;
  const double pos=s_audiohook_timepos;
  if (pos < g_cap_video_lastt + 0.03 && !force) return; // too soon

  videoFrameWrap *wr = new videoFrameWrap(bm);

  g_cap_video_lastt=pos;
  enc->encodeVideoFrame(wr,pos);
  wr->Release();
}

#endif

gif_encoder *g_cap_gif;
#ifdef TEST_MULTIPLE_MODES
gif_encoder  *g_cap_gif2,*g_cap_gif3; // only used if TEST_MULTIPLE_MODES defined
#endif
int g_cap_gif_lastsec_written;



int g_titlems=1750;
char g_title[4096];
bool g_dotitle;

LICE_IBitmap *g_cap_bm,*g_cap_bm_inv;

DWORD g_last_wndstyle;

int g_reent=0;


static WDL_WndSizer g_wndsize;

#ifndef _WIN32
int g_capwnd_levelsave=-1;
#endif

void GetViewRectSize(int *w, int *h)
{
  RECT r={0,0,320,240};
  GetWindowRect(GetDlgItem(g_hwnd,IDC_VIEWRECT),&r);
#ifdef _WIN32
  __LogicalToPhysicalPointForPerMonitorDPI(g_hwnd,(LPPOINT)&r);
  __LogicalToPhysicalPointForPerMonitorDPI(g_hwnd,((LPPOINT)&r)+1);
#endif
  if (w) *w=r.right-r.left - 2;
  if (h) *h=abs(r.bottom-r.top) - 2;

  
}

void UpdateCaption(HWND hwndDlg);
void UpdateStatusText(HWND hwndDlg);

// Preset state (g_preset_div, g_half_base_*) and the size presets live in
// ui_presets.cpp; declared in licecap_ui.h.

// GifCam keeps the size in the title bar and nothing else: the toolbar shows
// the state. UpdateDimBoxes is the single entry point used all over the file.
void UpdateDimBoxes(HWND hwndDlg)
{
  if (!hwndDlg) return;

  HWND b = GetDlgItem(hwndDlg, IDC_REC);
  if (b)
  {
    const char *label = g_cap_state == 1 ? "Stop" : (g_cap_state == 2 ? "Resume" : "Rec");
    char cur[64];
    cur[0] = 0;
    GetWindowTextA(b, cur, sizeof(cur));
    if (strcmp(cur, label)) SetWindowTextA(b, label);
    InvalidateRect(b, NULL, FALSE);
  }
  HWND s = GetDlgItem(hwndDlg, IDC_SAVE);
  if (s) InvalidateRect(s, NULL, FALSE);

  UpdateCaption(hwndDlg);
  UpdateStatusText(hwndDlg);
}


DWORD g_pause_time; // time of last pause
DWORD g_insert_cnt=0; // number of frames inserted, purely for display
int g_insert_ms=g_titlems;
double g_insert_alpha=0.5f;
LICE_IBitmap *g_cap_bm_txt;  // is a LICE_SysBitmap

#define MAX_PREROLL_OR_DELAY (1000 * 60 * 60 * 24)

// Busy state is UI-owned; only the atomic counters cross the worker boundary.
static bool s_busy;
static char s_busy_label[96];
static volatile LONG s_busy_done, s_busy_total;

void UpdateStatusText(HWND hwndDlg)
{
  HWND st = GetDlgItem(hwndDlg, IDC_STATUS);
  if (!st) return;

  int x = 0, y = 0;
  if (g_cap_bm)
  {
    x = g_cap_bm->getWidth();
    y = g_cap_bm->getHeight();
  }
  else
  {
    GetViewRectSize(&x,&y);
  }

  char buf[512];
  char el[32];
  const int secs = (int)(g_ms_written/1000);
  snprintf(el,sizeof(el),"%d:%02d",secs/60,secs%60);

  char presetbuf[96];
  const char *preset = PresetNameFor(x,y);
  if (!preset && g_preset_div == 2)
  {
    const char *full = PresetNameFor(x*2,y*2);
    if (full)
    {
      snprintf(presetbuf,sizeof(presetbuf),"%s (/2)",full);
      preset = presetbuf;
    }
  }

  if (s_busy)
  {
    char lab[sizeof(s_busy_label)];
    lstrcpyn_safe(lab, s_busy_label, sizeof(lab));
    CharUpperA(lab);
    if (s_busy_total > 0)
      snprintf(buf,sizeof(buf),"%s\n%d/%d",lab,s_busy_done,s_busy_total);
    else if (s_busy_done > 0)
      snprintf(buf,sizeof(buf),"%s\n%d frames",lab,s_busy_done);
    else
      snprintf(buf,sizeof(buf),"%s\n\nplease wait",lab);
  }
  else if (!g_cap_state)
  {
    if (g_pending_unsaved)
    {
      // An opened GIF is not a capture: the elapsed time it would show is 0:00.
      if (g_open_src_fn[0]) snprintf(buf,sizeof(buf),"GIF LOADED\nNot saved\n\nEdit / Save\nNew clears");
      else snprintf(buf,sizeof(buf),"RECORDED\n%s\nNot saved\n\nEdit / Save\nNew clears",el);
    }
    else if (preset)
      snprintf(buf,sizeof(buf),"EMPTY\n%d x %d\n%s\n%d fps\n\nClick Rec",x,y,preset,g_max_fps);
    else
      snprintf(buf,sizeof(buf),"EMPTY\n%d x %d\n%d fps\n\nClick Rec",x,y,g_max_fps);
  }
  else if (g_cap_state == 2)
  {
    snprintf(buf,sizeof(buf),"PAUSED\n%s",el);
  }
  else
  {
    const DWORD now = timeGetTime();
    if (g_cap_prerolluntil && WDL_TICKS_IN_RANGE_ENDING_AT(now,g_cap_prerolluntil,MAX_PREROLL_OR_DELAY))
      snprintf(buf,sizeof(buf),"STARTING\n%d",(int)((g_cap_prerolluntil-now+999)/1000));
    else
      snprintf(buf,sizeof(buf),"RECORDING\n%s\n%.0f fps",el,g_frate_valid ? g_frate_avg : 0.0);
  }

  char oldtext[512];
  GetWindowTextA(st,oldtext,sizeof(oldtext));
  if (strcmp(buf,oldtext)) SetWindowTextA(st,buf);
}

bool Busy_Active(void)
{
  return s_busy;
}

// Worker callbacks must never paint, pump messages or touch HWNDs.
void Busy_Progress(void *, int done, int total)
{
  InterlockedExchange(&s_busy_done, done);
  if (total > 0) InterlockedExchange(&s_busy_total, total);
}

struct BusyJob
{
  BusyWorkFn fn;
  void *ctx;
  bool ok;
};

static unsigned __stdcall Busy_Worker(void *arg)
{
  BusyJob *job = (BusyJob *)arg;
  GIFEdit_SetProgress(Busy_Progress, NULL);
  job->ok = job->fn(job->ctx);
  GIFEdit_SetProgress(NULL, NULL);
  return 0;
}

// Native progress themes do not reliably support the application's dark palette.
// Keep the native range/position messages, but paint both modes with our colours.
static LRESULT CALLBACK Busy_ProgressProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                         UINT_PTR id, DWORD_PTR data)
{
  if (msg == WM_ERASEBKGND) return 1;
  if (msg == WM_PAINT)
  {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT r;
    GetClientRect(hwnd, &r);
    HBRUSH border = CreateSolidBrush(COL_BORDER);
    FillRect(dc, &r, border);
    DeleteObject(border);
    InflateRect(&r, -UI(1), -UI(1));
    HBRUSH track = CreateSolidBrush(COL_EDIT_BG);
    FillRect(dc, &r, track);
    DeleteObject(track);
    RECT fill = r;
    const int width = r.right - r.left;
    if (GetWindowLongPtr(hwnd, GWL_STYLE) & PBS_MARQUEE)
    {
      const int segment = width / 4;
      const int phase = (int)((GetTickCount() % 1600) * (width + segment) / 1600);
      fill.left = r.left + phase - segment;
      fill.right = fill.left + segment;
      if (fill.left < r.left) fill.left = r.left;
      if (fill.right > r.right) fill.right = r.right;
    }
    else
    {
      const int pos = (int)SendMessage(hwnd, PBM_GETPOS, 0, 0);
      fill.right = fill.left + MulDiv(width, pos, 1000);
    }
    HBRUSH accent = CreateSolidBrush(COL_ACCENT);
    FillRect(dc, &fill, accent);
    DeleteObject(accent);
    EndPaint(hwnd, &ps);
    return 0;
  }
  if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, Busy_ProgressProc, id);
  return DefSubclassProc(hwnd, msg, wp, lp);
}

static LRESULT CALLBACK Busy_WindowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
  if (msg == WM_ERASEBKGND)
  {
    RECT r;
    GetClientRect(hwnd, &r);
    FillRect((HDC)wp, &r, UI_BgBrush());
    return 1;
  }
  if (msg == WM_CTLCOLORSTATIC)
  {
    SetBkMode((HDC)wp, TRANSPARENT);
    SetBkColor((HDC)wp, COL_BG);
    SetTextColor((HDC)wp, COL_TEXT);
    return (LRESULT)UI_BgBrush();
  }
  // There is no unsafe "abort thread" operation: finish the temporary file
  // before allowing the owner to close. Paint/move/accessibility still work.
  if (msg == WM_CLOSE) return 0;
  return DefWindowProc(hwnd, msg, wp, lp);
}

bool Busy_Run(HWND owner, const char *label, int total, BusyWorkFn fn, void *ctx)
{
  if (s_busy || !fn) return false;
  s_busy = true;
  lstrcpyn_safe(s_busy_label, label, sizeof(s_busy_label));
  InterlockedExchange(&s_busy_done, 0);
  InterlockedExchange(&s_busy_total, total);

  INITCOMMONCONTROLSEX icc = {sizeof(icc), ICC_PROGRESS_CLASS};
  InitCommonControlsEx(&icc);
  WNDCLASSA wc = {0};
  wc.lpfnWndProc = Busy_WindowProc;
  wc.hInstance = g_hInst;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = NULL; // WM_ERASEBKGND always uses the current palette
  wc.lpszClassName = "LICEcapProcessing";
  RegisterClassA(&wc);
  RECT r;
  GetWindowRect(owner, &r);
  const int w = UI(380), h = UI(166);
  HWND popup = CreateWindowExA(WS_EX_DLGMODALFRAME, wc.lpszClassName,
    "LICEcap - Processing", WS_POPUP | WS_CAPTION | WS_SYSMENU,
    r.left + (r.right-r.left-w)/2, r.top + (r.bottom-r.top-h)/2,
    w, h, owner, NULL, g_hInst, NULL);
  if (!popup)
  {
    s_busy = false;
    MessageBoxA(owner, "Could not create the processing window.", "LICEcap", MB_ICONERROR);
    return false;
  }
  EnableMenuItem(GetSystemMenu(popup, FALSE), SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
  HWND text = CreateWindowA("STATIC", label, WS_CHILD | WS_VISIBLE,
    UI(18), UI(14), UI(340), UI(24), popup, (HMENU)1, g_hInst, NULL);
  HWND detail = CreateWindowA("STATIC", "Please wait...", WS_CHILD | WS_VISIBLE,
    UI(18), UI(80), UI(340), UI(24), popup, (HMENU)2, g_hInst, NULL);
  HWND bar = CreateWindowExA(0, PROGRESS_CLASSA, "", WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
    UI(18), UI(48), UI(340), UI(20), popup, (HMENU)3, g_hInst, NULL);
  UI_ThemeDialog(popup); // includes the native dark/light title bar
  SetWindowSubclass(bar, Busy_ProgressProc, 1, 0);
  SendMessage(text, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
  SendMessage(detail, WM_SETFONT, (WPARAM)g_ui_font, TRUE);
  SendMessage(bar, PBM_SETRANGE32, 0, 1000);
  SendMessage(bar, PBM_SETMARQUEE, TRUE, 35);
  const BOOL wasEnabled = IsWindowEnabled(owner);
  EnableWindow(owner, FALSE);
  ShowWindow(popup, SW_SHOW);
  UpdateWindow(popup); // visible BEFORE starting work, even for slow disk I/O

  BusyJob job = { fn, ctx, false };
  HANDLE thread = (HANDLE)_beginthreadex(NULL, 0, Busy_Worker, &job, 0, NULL);
  bool quit = false;
  int quitCode = 0;
  bool determinate = false;
  DWORD started = GetTickCount();
  while (thread && WaitForSingleObject(thread, 0) == WAIT_TIMEOUT)
  {
    const LONG done = InterlockedCompareExchange(&s_busy_done, 0, 0);
    const LONG count = InterlockedCompareExchange(&s_busy_total, 0, 0);
    char buf[160];
    if (count > 0)
    {
      if (!determinate)
      {
        SendMessage(bar, PBM_SETMARQUEE, FALSE, 0);
        SetWindowLongPtr(bar, GWL_STYLE, GetWindowLongPtr(bar, GWL_STYLE) & ~PBS_MARQUEE);
        determinate = true;
      }
      SendMessage(bar, PBM_SETPOS, (WPARAM)((double)done * 1000 / count), 0);
      snprintf(buf, sizeof(buf), "%d%% - %ld / %ld frames - %lus",
        (int)((double)done * 100 / count), done, count, (GetTickCount()-started)/1000);
    }
    else snprintf(buf, sizeof(buf), "Processing... %lus", (GetTickCount()-started)/1000);
    SetWindowTextA(detail, buf);
    RedrawWindow(popup, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_ALLCHILDREN);
    UpdateStatusText(owner);
    MsgWaitForMultipleObjects(1, &thread, FALSE, 50, QS_ALLINPUT);
    MSG msg;
    while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
    {
      if (msg.message == WM_QUIT) { quit = true; quitCode = (int)msg.wParam; continue; }
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }
  if (thread) CloseHandle(thread); // wait above is also the result memory barrier
  if (wasEnabled) EnableWindow(owner, TRUE);
  DestroyWindow(popup);
  s_busy = false;
  SetActiveWindow(owner);
  UpdateStatusText(owner);
  InvalidateRect(owner, NULL, TRUE);
  if (!thread) MessageBoxA(owner, "Could not start the processing thread.", "LICEcap", MB_ICONERROR);
  if (quit) PostQuitMessage(quitCode);
  return thread && job.ok;
}

struct BusySaveJob { const char *src, *dst; DWORD error; };
static bool Busy_Save(void *ctx)
{
  BusySaveJob *job = (BusySaveJob *)ctx;
  const BOOL ok = MoveFileExA(job->src, job->dst,
    MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
  job->error = ok ? ERROR_SUCCESS : GetLastError();
  return ok != FALSE;
}


void UpdateCaption(HWND hwndDlg)
{
  if (!hwndDlg) return;

  int x = 0, y = 0;
  GetViewRectSize(&x,&y);

  char buf[512];
  char el[32];
  const int secs = (int)(g_ms_written/1000);
  snprintf(el,sizeof(el),"%d:%02d",secs/60,secs%60);

  if (!g_cap_state)
  {
    if (g_preset_div == 2) snprintf(buf,sizeof(buf),"LICEcap %d x %d  (/2)",x,y);
    else snprintf(buf,sizeof(buf),"LICEcap %d x %d",x,y);
    if (g_open_src_fn[0])
    {
      char nm[128];
      lstrcpyn_safe(nm,WDL_get_filepart(g_open_src_fn),sizeof(nm));
      strncat(buf," - ",sizeof(buf)-strlen(buf)-1);
      strncat(buf,nm,sizeof(buf)-strlen(buf)-1);
      strncat(buf," (opened)",sizeof(buf)-strlen(buf)-1);
    }
    if (g_pending_unsaved) strncat(buf," - not saved",sizeof(buf)-strlen(buf)-1);
  }
  else if (g_cap_state == 2)
  {
    snprintf(buf,sizeof(buf),"LICEcap %d x %d - paused %s",x,y,el);
  }
  else
  {
    const DWORD now = timeGetTime();
    if (g_cap_prerolluntil && WDL_TICKS_IN_RANGE_ENDING_AT(now,g_cap_prerolluntil,MAX_PREROLL_OR_DELAY))
      snprintf(buf,sizeof(buf),"LICEcap %d x %d - starting in %d",x,y,(int)((g_cap_prerolluntil-now+999)/1000));
    else
      snprintf(buf,sizeof(buf),"LICEcap %d x %d - recording %s (%.0f fps)",x,y,el,g_frate_valid ? g_frate_avg : 0.0);
  }

  SetWindowTextA(hwndDlg,buf);

  // the title lives in the custom caption drawn by the app
  RECT cr;
  UI_CaptionRect(hwndDlg,&cr);
  InvalidateRect(hwndDlg,&cr,FALSE);
}

void SaveRestoreRecRect(HWND hwndDlg, bool restore)
{
#ifdef _WIN32
  static RECT r;
  if (!restore) GetWindowRect(GetDlgItem(hwndDlg,IDC_VIEWRECT),&r);
  else
  {
    RECT r2;
    GetWindowRect(GetDlgItem(hwndDlg,IDC_VIEWRECT),&r2);

    int xdiff = r.left-r2.left;
    int ydiff = r.top - r2.top;

    int wdiff = (r.right-r.left) - (r2.right-r2.left);
    int hdiff = (r.bottom-r.top) - (r2.bottom-r2.top);

    GetWindowRect(hwndDlg,&r2);
    SetWindowPos(hwndDlg,NULL,r2.left+xdiff,r2.top+ydiff,r2.right-r2.left + wdiff, r2.bottom-r2.top + hdiff,SWP_NOZORDER|SWP_NOACTIVATE);

  }
#endif
}
void SWELL_SetWindowResizeable(HWND, bool);

void Capture_Finish(HWND hwndDlg)
{
  if (g_cap_state)
  {
#ifdef _WIN32
    SaveRestoreRecRect(hwndDlg,false);
    SetWindowLong(hwndDlg,GWL_STYLE,g_last_wndstyle);
    SetWindowPos(hwndDlg,g_always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_DRAWFRAME|SWP_NOACTIVATE);
    SaveRestoreRecRect(hwndDlg,true);
#else
    if (g_capwnd_levelsave>=0)
    {
      SWELL_SetWindowLevel(hwndDlg,g_capwnd_levelsave);
      g_capwnd_levelsave=-1;
    }
    SWELL_SetWindowResizeable(hwndDlg,true);
#endif
    g_cap_state=0;
  }

  UpdateDimBoxes(hwndDlg);

#ifndef NO_LCF_SUPPORT
  delete g_cap_lcf;
  g_cap_lcf=0;
#endif

#ifdef REAPER_LICECAP
  if (s_audiohook_reg)
  {
    Audio_RegHardwareHook(false,&s_audiohook);
    s_audiohook_reg=false;
    // kill thread and clear buffers

    if (s_audiothread)
    {
      WaitForSingleObject(s_audiothread,INFINITE);
      CloseHandle(s_audiothread);
      s_audiothread=0;
    }

    s_audiohook_samples.Clear();
    s_audiohook_samples.Compact();
  }
#endif

#ifdef VIDEO_ENCODER_SUPPORT
  delete g_cap_video;
  g_cap_video=0;
#endif
  if (g_cap_gif)
  {
    delete g_cap_gif;
    g_cap_gif=0;
  }

#ifdef TEST_MULTIPLE_MODES
  delete g_cap_gif2;
  g_cap_gif2 = 0;
  delete g_cap_gif3;
  g_cap_gif3 = 0;
#endif

  delete g_cap_bm;
  g_cap_bm=0;

  UpdateDimBoxes(hwndDlg);
}

#ifdef VIDEO_ENCODER_SUPPORT

WDL_DLGRET VideoOptionsProc(HWND hwndDlg, UINT Message, WPARAM wParam, LPARAM lParam)
{
  switch(Message)
  {
  case WM_INITDIALOG:
    {
      UI_ThemeDialog(hwndDlg);
      SetDlgItemInt(hwndDlg, IDC_EDIT1, g_cap_video_vbr, FALSE);
      SetDlgItemInt(hwndDlg, IDC_EDIT2, g_cap_video_abr, FALSE);
    }
    break;
  case WM_COMMAND:
    switch(LOWORD(wParam))
    {
    case IDOK:
    case IDCANCEL:
      EndDialog(hwndDlg,0);
    }
    break;
#ifdef _WIN32
  case WM_ERASEBKGND:
    if (g_dark_ui)
    {
      HDC dc = (HDC)wParam;
      RECT rc;
      GetClientRect(hwndDlg,&rc);
      FillRect(dc,&rc,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(BLACK_BRUSH));
      return 1;
    }
  break;
  case WM_CTLCOLORSTATIC:
  case WM_CTLCOLORBTN:
  case WM_CTLCOLOREDIT:
  case WM_CTLCOLORLISTBOX:
  {
    LRESULT res;
    if (UI_DialogCtlColor(Message,wParam,&res)) return (WDL_DLGRET)res;
  }
  break;
#endif
    case WM_DESTROY:
    {
      BOOL t = FALSE;
      int a = GetDlgItemInt(hwndDlg, IDC_EDIT1, &t, FALSE);
      if(t && a>0) g_cap_video_vbr = a;
      t = FALSE;
      a = GetDlgItemInt(hwndDlg, IDC_EDIT2, &t, FALSE);
      if(t && a>0) g_cap_video_abr = a;
    }
    break;
  }
  return 0;
}
#endif

static UINT_PTR CALLBACK SaveOptsProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_INITDIALOG:
    {
      UI_ThemeDialog(hwndDlg);
      //ShowWindow(GetDlgItem(hwndDlg, IDC_TIMELINE), SW_HIDE);
      if (g_prefs&1) CheckDlgButton(hwndDlg, IDC_TITLEUSE, BST_CHECKED);
      if (g_prefs&2) CheckDlgButton(hwndDlg, IDC_BIGFONT, BST_CHECKED);
      if (g_prefs&4) CheckDlgButton(hwndDlg, IDC_MOUSECAP, BST_CHECKED);
      if (g_prefs&8) CheckDlgButton(hwndDlg, IDC_TIMELINE, BST_CHECKED);
      if (g_prefs&16) CheckDlgButton(hwndDlg, IDC_SSPAUSE, BST_CHECKED);
      if (g_prefs&32) CheckDlgButton(hwndDlg, IDC_CHECK1, BST_CHECKED);
      

      if (g_prefs&64) 
      {
        CheckDlgButton(hwndDlg, IDC_CHECK2, BST_CHECKED);
      }
      else
      {
        EnableWindow(GetDlgItem(hwndDlg,IDC_STOPAFTER_SEC_LBL),0);
        EnableWindow(GetDlgItem(hwndDlg,IDC_STOPAFTER_SEC),0);
      }
      char buf[256];
      snprintf(buf, sizeof(buf), "%.1f", (double)g_titlems/1000.0);
      SetDlgItemText(hwndDlg, IDC_MS, buf);
      snprintf(buf, sizeof(buf), "%.1f", (double)g_stop_after_msec/1000.0);
      SetDlgItemText(hwndDlg, IDC_STOPAFTER_SEC, buf);

      SetDlgItemText(hwndDlg, IDC_TITLE, (g_title[0] ? g_title : "Title"));
      EnableWindow(GetDlgItem(hwndDlg, IDC_MS), (g_prefs&1));
      EnableWindow(GetDlgItem(hwndDlg, IDC_BIGFONT), (g_prefs&1));
      EnableWindow(GetDlgItem(hwndDlg, IDC_TITLE), (g_prefs&1));
      SetDlgItemInt(hwndDlg, IDC_LOOPCNT, g_gif_loopcount,FALSE);
#ifndef VIDEO_ENCODER_SUPPORT
      ShowWindow(GetDlgItem(hwndDlg, IDC_BUTTON1), false);
#endif
    }
    return 0;
    case WM_DESTROY:
    {
      g_prefs=0;
      if (IsDlgButtonChecked(hwndDlg, IDC_TITLEUSE)) g_prefs |= 1;
      if (IsDlgButtonChecked(hwndDlg, IDC_BIGFONT)) g_prefs |= 2;
      if (IsDlgButtonChecked(hwndDlg, IDC_MOUSECAP)) g_prefs |= 4;
      if (IsDlgButtonChecked(hwndDlg, IDC_TIMELINE)) g_prefs |= 8;
      if (IsDlgButtonChecked(hwndDlg, IDC_SSPAUSE)) g_prefs |= 16;
      if (IsDlgButtonChecked(hwndDlg, IDC_CHECK1)) g_prefs |= 32;
      if (IsDlgButtonChecked(hwndDlg, IDC_CHECK2)) g_prefs |= 64;

      char buf[256];
      GetDlgItemText(hwndDlg, IDC_MS, buf, sizeof(buf));
      g_titlems = (int)(atof(buf)*1000.0);

      GetDlgItemText(hwndDlg, IDC_STOPAFTER_SEC, buf, sizeof(buf));
      g_stop_after_msec=(int) (atof(buf)*1000.0);

      GetDlgItemText(hwndDlg, IDC_TITLE, g_title, sizeof(g_title));
      if (!strcmp(g_title, "Title")) g_title[0]=0;

      {
        BOOL t=FALSE;
        int a=GetDlgItemInt(hwndDlg,IDC_LOOPCNT,&t,FALSE);
        if (t) g_gif_loopcount=(a>0&&a<65536) ? a : 0;
      }

    }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_CHECK2:
          {
            int use = !!IsDlgButtonChecked(hwndDlg, IDC_CHECK2);
            EnableWindow(GetDlgItem(hwndDlg,IDC_STOPAFTER_SEC_LBL),use);
            EnableWindow(GetDlgItem(hwndDlg,IDC_STOPAFTER_SEC),use);
          }
        break;
        case IDC_TITLEUSE:
        {
          bool use = !!IsDlgButtonChecked(hwndDlg, IDC_TITLEUSE);
          EnableWindow(GetDlgItem(hwndDlg, IDC_MS), use);
          EnableWindow(GetDlgItem(hwndDlg, IDC_BIGFONT), use);
          EnableWindow(GetDlgItem(hwndDlg, IDC_TITLE), use);
          if (use) SetFocus(GetDlgItem(hwndDlg, IDC_TITLE));
        }
        return 0;
        case IDC_TITLE:
#ifdef _WIN32
          if (HIWORD(wParam) == EN_SETFOCUS)
          {
            SendDlgItemMessage(hwndDlg, IDC_TITLE, EM_SETSEL, 0, -1);
          }  
#endif
        return 0;
#ifdef VIDEO_ENCODER_SUPPORT
        case IDC_BUTTON1:
          DialogBox(g_hInst,MAKEINTRESOURCE(IDD_OPTIONS),hwndDlg,VideoOptionsProc);
        break;
#endif
      }
    return 0;
#ifdef _WIN32
    case WM_ERASEBKGND:
      if (g_dark_ui)
      {
        HDC dc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwndDlg,&rc);
        FillRect(dc,&rc,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
      }
    break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
      LRESULT res;
      if (UI_DialogCtlColor(msg,wParam,&res)) return (UINT_PTR)res;
    }
    break;
#endif
  }
  return 0;
}

void WriteTextFrame(const char* str, int ms, bool isTitle, int w, int h, double bgAlpha=1.0f)
{
  if (isTitle)
    LICE_Clear(g_cap_bm, 0);

  if (str)
  {
    int tw=w;
    int th=h;   

    LICE_IBitmap* tbm = g_cap_bm;
    if (isTitle)
    {
      if (g_prefs&2) 
      {
        tw /= 2;
        th /= 2;
      }
      tbm = LICE_CreateMemBitmap(tw, th); 
      LICE_Clear(tbm, 0);                      
    }
    else
    {
      tbm = LICE_CreateMemBitmap(tw, th); 
      LICE_Copy(tbm, g_cap_bm_txt);
      LICE_FillRect(tbm, 0,0,w,h, LICE_RGBA(0,0,0,255), bgAlpha, LICE_BLIT_MODE_COPY|LICE_BLIT_USE_ALPHA);
    }
    /*
	    LICE_DrawRect(tbm,30,30,150,150,LICE_RGBA(255,0,0,255), 1.0f, LICE_BLIT_MODE_COPY);
	    LICE_DrawRect(tbm,31,31,148,148,LICE_RGBA(255,0,0,255), 1.0f, LICE_BLIT_MODE_COPY);
	    LICE_DrawRect(tbm,32,32,146,146,LICE_RGBA(255,0,0,255), 1.0f, LICE_BLIT_MODE_COPY);
    */
    char buf[4096];
    lstrcpyn_safe(buf, str,sizeof(buf));                    

    int numlines=1;
    char* p = strstr(buf, "\n");
    while (p)
    {
      *p=0;
      p = strstr(p+1, "\n");
      ++numlines;
    }

    p=buf;
    int i;
    for (i = 0; i < numlines; ++i)
    {                   
      int txtw, txth;
      LICE_MeasureText(p, &txtw, &txth);
      LICE_DrawText(tbm, (tw-txtw)/2, (th-txth*numlines*4)/2+txth*i*4, p, LICE_RGBA(255,255,255,255), 1.0f, LICE_BLIT_MODE_COPY);
      p += strlen(p)+1;
    }

    if (tbm != g_cap_bm)
    {
      LICE_ScaledBlit(g_cap_bm, tbm, 0, 0, w, h, 0, 0, tw, th, 1.0f, LICE_BLIT_MODE_COPY);
      delete tbm;
    }
  }

#ifdef TEST_MULTIPLE_MODES
  if (g_cap_gif2)
  {
    g_cap_gif2->frame_finish(); 
    g_cap_gif2->frame_new(g_cap_bm,0,0,g_cap_bm->getWidth(),g_cap_bm->getHeight());
    g_cap_gif2->frame_advancetime(ms);
  }
  if (g_cap_gif3)
  {
    g_cap_gif3->frame_finish(); 
    g_cap_gif3->frame_new(g_cap_bm,0,0,g_cap_bm->getWidth(),g_cap_bm->getHeight());
    g_cap_gif3->frame_advancetime(ms);
  }
#endif

  if (g_cap_gif)
  {
    g_cap_gif->frame_finish(); 
    g_cap_gif->frame_new(g_cap_bm,0,0,g_cap_bm->getWidth(),g_cap_bm->getHeight());
    g_cap_gif->frame_advancetime(ms);
    g_cap_gif_lastsec_written=-1;
  }

#ifdef VIDEO_ENCODER_SUPPORT
  if (g_cap_video)
  {
    int nf=100;
    if (nf>ms) nf=ms;
    EncodeFrameToVideo(g_cap_video,g_cap_bm,true);
    s_audiohook_samples_mutex.Enter();
    s_audiohook_needsilence += nf*0.001;
    s_audiohook_timepos+=nf*0.001;
    s_audiohook_samples_mutex.Leave();

    EncodeFrameToVideo(g_cap_video,g_cap_bm,true);
    s_audiohook_samples_mutex.Enter();
    s_audiohook_needsilence += (ms-nf)*0.001;
    s_audiohook_timepos+=(ms-nf)*0.001;
    s_audiohook_samples_mutex.Leave();
  }
#endif

#ifndef NO_LCF_SUPPORT
  if (g_cap_lcf) 
  {
    int del=0;
    g_cap_lcf->OnFrame(g_cap_bm, del); 
    if (!isTitle)
    {
      del = g_pause_time-g_last_frame_capture_time;
      del += ms;
    }
    g_cap_lcf->OnFrame(g_cap_bm, del); 
  }
#endif
}

WDL_DLGRET InsertProc(HWND hwnd, UINT Message, WPARAM wParam, LPARAM lParam)
{
	switch(Message)
	{
    case WM_INITDIALOG :
		  {
        UI_ThemeDialog(hwnd);
        char buf[256];
        sprintf(buf,"Insert (%d)",g_insert_cnt);
        SetDlgItemText(hwnd,IDOK,buf);
        snprintf(buf, sizeof(buf), "%.1f", (double)g_insert_ms/1000.0);
        SetDlgItemText(hwnd, IDC_MS, buf);
        snprintf(buf, sizeof(buf), "%.1f", g_insert_alpha);
        SetDlgItemText(hwnd, IDC_ALPHA, buf);
        SetFocus(GetDlgItem(hwnd,IDC_EDIT));
		  }
		return 0;
    case WM_CLOSE:
	    EndDialog(hwnd,0);
		break;
		case WM_COMMAND:
      switch(LOWORD(wParam))
      {
        case IDC_ALPHA:
        {
				  char buf[256];
				  GetDlgItemText(hwnd, IDC_ALPHA, buf, sizeof(buf));
				  g_insert_alpha = wdl_min(1.0f, atof(buf));
        }
        break;
        case IDC_MS:
				{
				  char buf[256];
				  GetDlgItemText(hwnd, IDC_MS, buf, sizeof(buf));
				  g_insert_ms = (int)(atof(buf)*1000.0);
        }
        break;
        case IDOK:
			    if (g_cap_bm_txt && g_insert_ms)
          {
            char buf[4096];
            GetDlgItemText(hwnd,IDC_EDIT,buf,4096);
				    WriteTextFrame(buf,g_insert_ms,g_insert_alpha<0,g_cap_bm_txt->getWidth(),g_cap_bm_txt->getHeight(), g_insert_alpha >=0 ? g_insert_alpha : 1.0f);
				    g_insert_cnt++;
				    sprintf(buf,"Insert (%d)",g_insert_cnt);
            SetDlgItemText(hwnd,IDOK,buf);
            SetDlgItemText(hwnd,IDC_EDIT,"");
            SetFocus(GetDlgItem(hwnd,IDC_EDIT));
          }
        break;
        case IDCANCEL:
          EndDialog(hwnd,0);
        break;
			}
		break;
#ifdef _WIN32
    case WM_ERASEBKGND:
      if (g_dark_ui)
      {
        HDC dc = (HDC)wParam;
        RECT rc;
        GetClientRect(hwnd,&rc);
        FillRect(dc,&rc,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(BLACK_BRUSH));
        return 1;
      }
    break;
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
      LRESULT res;
      if (UI_DialogCtlColor(Message,wParam,&res)) return (WDL_DLGRET)res;
    }
    break;
#endif
	}
	return 0;
}

#ifndef _WIN32
bool GetScreenData(int xpos, int ypos, LICE_IBitmap *bmOut);

#endif


void SaveConfig(HWND hwndDlg)
{
  char buf[1024];
  RECT r;
  GetWindowRect(hwndDlg,&r);
  sprintf(buf,"%d %d %d %d",(int)r.left,(int)r.top,(int)r.right,(int)r.bottom);
  WritePrivateProfileString("licecap","wnd_r",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_max_fps);
  WritePrivateProfileString("licecap","maxfps",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_prefs);
  WritePrivateProfileString("licecap","prefs",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_titlems);
  WritePrivateProfileString("licecap","titlems",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_gif_loopcount);
  WritePrivateProfileString("licecap","gifloopcnt",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_stop_after_msec);
  WritePrivateProfileString("licecap","stopafter",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_capture_cursor ? 1 : 0);
  WritePrivateProfileString("licecap","cursor",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_always_on_top ? 1 : 0);
  WritePrivateProfileString("licecap","ontop",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_dark_ui ? 1 : 0);
  WritePrivateProfileString("licecap","dark",buf,g_ini_file.Get());
  
  

  WritePrivateProfileString("licecap","title",g_title,g_ini_file.Get());

#ifdef VIDEO_ENCODER_SUPPORT
  sprintf(buf, "%d", g_cap_video_vbr);
  WritePrivateProfileString("licecap","video_vbr",buf,g_ini_file.Get());
  sprintf(buf, "%d", g_cap_video_abr);
  WritePrivateProfileString("licecap","video_abr",buf,g_ini_file.Get());
#endif

}

///////////////////////////////////////////////////////////////////////////////
// GifCam-style UI: flat side toolbar, drop-down menus, customization dialog
///////////////////////////////////////////////////////////////////////////////





static void ShowToolMenu(HWND hwndDlg, int id)
{
  HMENU m = CreatePopupMenu();
  if (!m) return;

  const UINT g_idle = g_cap_state ? MF_GRAYED : 0;
  const bool custom_fps = (g_max_fps != 10 && g_max_fps != 16 && g_max_fps != 30);

  // Settings is the single place for every preference: the Rec / Save / Presets
  // menus only keep their own action instead of repeating "Customization...".
  if (id == IDC_REC)
  {
    AppendMenuA(m, MF_STRING|g_idle, IDM_NEW, "New\tCtrl+N");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|g_idle, IDM_FULLSCREEN, "Full Screen");
  }
  else if (id == IDC_EDITBTN)
  {
    AppendMenuA(m, MF_STRING|g_idle|(g_max_fps==10?MF_CHECKED:0), IDM_FPS10, "10 FPS\tF1");
    AppendMenuA(m, MF_STRING|g_idle|(g_max_fps==16?MF_CHECKED:0), IDM_FPS16, "16 FPS\tF2");
    AppendMenuA(m, MF_STRING|g_idle|(g_max_fps==30?MF_CHECKED:0), IDM_FPS30, "30 FPS\tF3");
    AppendMenuA(m, MF_STRING|g_idle|(custom_fps?MF_CHECKED:0), IDM_FPS_CUSTOM, "Custom FPS...");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|g_idle|(g_capture_cursor?MF_CHECKED:0), IDM_CAPTURE_CURSOR, "Capture Cursor");
    AppendMenuA(m, MF_STRING|g_idle|((g_prefs&4)?MF_CHECKED:0), IDM_CAPTURE_CLICKS, "Highlight Mouse Clicks");
    AppendMenuA(m, MF_STRING|g_idle|((g_prefs&8)?MF_CHECKED:0), IDM_CAPTURE_TIME, "Show Elapsed Time");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|(g_dark_ui?MF_CHECKED:0), IDM_DARK_THEME, "Dark Theme");
    AppendMenuA(m, MF_STRING|(g_always_on_top?MF_CHECKED:0), IDM_ALWAYS_ON_TOP, "Always on Top");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|g_idle, IDM_DEFAULT_SIZE, "Reset Window Size (512 x 512)");
    AppendMenuA(m, MF_STRING|g_idle, IDM_FULLSCREEN, "Full Screen");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|g_idle, IDM_CUSTOMIZE, "Settings...\tCtrl+U");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, IDM_ABOUT, "About LICEcap");
  }
  else if (id == IDC_EDITMODE)
  {
    // The label of the button does the first item (or opens a GIF when nothing
    // is pending); the arrow is the way to the other one. While the editor is
    // open the first item would close it, which the strip's Close already does.
    const bool have = g_pending_unsaved && g_pending_fn[0] && !Editor_Active();
    AppendMenuA(m, MF_STRING|(have?0:MF_GRAYED), IDM_EDIT_TOGGLE, "Edit Last Recording");
    AppendMenuA(m, MF_STRING|g_idle, IDM_OPEN_GIF, "Open GIF to Edit...\tCtrl+O");
  }
  else if (id == IDC_SAVE)
  {
    // The bookmark folders are not listed in the menu any more: with a
    // recording waiting to be saved the arrow opens them as a drop-down list
    // under the button (see the IDC_SAVE handler), so the menu keeps only the
    // commands and the entry that reopens that same list.
    AppendMenuA(m, MF_STRING|(g_pending_unsaved?0:MF_GRAYED), IDM_SAVEAS, "Save As...\tCtrl+S");
    AppendMenuA(m, MF_STRING|((g_pending_unsaved||g_last_save_fn[0])?0:MF_GRAYED), IDM_SHOWINFOLDER, "Show in Folder");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|g_idle, IDM_BOOKMARKS_PANEL,
                Bookmarks_Count() ? "Bookmarks..." : "Bookmarks... (none yet)");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING|g_idle, IDM_RECORD_TO_FILE, "Record to File...");
  }
  else
  {
    DestroyMenu(m);
    return;
  }

  RECT r;
  HWND b = GetDlgItem(hwndDlg,id);
  if (!b || !GetWindowRect(b,&r))
  {
    DestroyMenu(m);
    return;
  }

  SetForegroundWindow(hwndDlg);
  int cmd = (int)TrackPopupMenuEx(m, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|TPM_RIGHTBUTTON,
                                  r.left, r.bottom + UI(2), hwndDlg, NULL);
  DestroyMenu(m);
  PostMessage(hwndDlg,WM_NULL,0,0);

  if (cmd) SendMessage(hwndDlg,WM_COMMAND,(WPARAM)cmd,0);
}


// ---------------------------------------------------------------------------
// capture / save flow (GifCam: record first, decide where to save later)
// ---------------------------------------------------------------------------

static void DiscardPending(void)
{
  if (g_pending_unsaved && g_pending_fn[0]) DeleteFileA(g_pending_fn);
  g_pending_unsaved = false;
  g_pending_fn[0] = 0;
  g_open_src_fn[0] = 0;
  g_rec_is_temp = false;
  UpdateDimBoxes(g_hwnd);
}

// ---------------------------------------------------------------------------
// open a GIF from disk in the timeline editor
//
// The file is never edited in place: it is copied to a temporary working GIF
// that becomes the pending recording, so Apply rewrites the copy and Save
// moves that wherever the user wants. The original GIF stays untouched.
// ---------------------------------------------------------------------------
static void FolderOfFile(const char *fn, char *dir, int dirsz)
{
  dir[0] = 0;
  if (!fn || !fn[0]) return;
  lstrcpyn_safe(dir,fn,dirsz);
  const char *p = WDL_get_filepart(dir);
  if (p && p != dir) dir[p-dir] = 0;
  else dir[0] = 0;
}

// A GIF must start with the magic in the first six bytes. This is the cheap
// check used before the picker's choice replaces anything.
static bool LooksLikeGif(const char *fn)
{
  char head[6];
  FILE *fp = fopen(fn,"rb");
  if (!fp) return false;
  const bool ok = fread(head,1,6,fp) == 6 &&
                  (!_strnicmp(head,"GIF87a",6) || !_strnicmp(head,"GIF89a",6));
  fclose(fp);
  return ok;
}

static bool MakeOpenWorkingCopy(const char *src, char *dst, int dstsz)
{
  char dir[MAX_PATH];
  dir[0] = 0;
  const DWORD n = GetTempPathA(sizeof(dir),dir);
  if (!n || n >= sizeof(dir)) lstrcpyn_safe(dir,".",sizeof(dir));

  SYSTEMTIME st;
  GetLocalTime(&st);
  const int dl = (int)strlen(dir);
  const char *sep = (dl && (dir[dl-1] == '\\' || dir[dl-1] == '/')) ? "" : "\\";

  char base[MAX_PATH];
  snprintf(base,sizeof(base),"LICEcap_edit_%04d%02d%02d_%02d%02d%02d",
           st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);

  int i;
  for (i = 0; i < 1000; i++)
  {
    if (!i) snprintf(dst,dstsz,"%s%s%s.gif",dir,sep,base);
    else snprintf(dst,dstsz,"%s%s%s_%d.gif",dir,sep,base,i);
    if (GetFileAttributesA(dst) == INVALID_FILE_ATTRIBUTES) break;
  }
  if (i >= 1000) return false;

  if (!CopyFileA(src,dst,FALSE))
  {
    DeleteFileA(dst);
    return false;
  }
  return true;
}

static bool OpenGifInEditor(HWND hwndDlg, const char *src)
{
  if (!src || !src[0]) return false;

  // Refuse anything that is not a GIF before the pending recording is touched,
  // so a wrong file never leaves the window claiming to have it open.
  const DWORD attr = GetFileAttributesA(src);
  if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
  {
    char msg[2600];
    snprintf(msg,sizeof(msg),"Could not open:\n%s",src);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONERROR);
    return false;
  }
  if (!LooksLikeGif(src))
  {
    char msg[2600];
    snprintf(msg,sizeof(msg),"That file is not a GIF:\n%s",src);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONERROR);
    return false;
  }

  char tmp[2048];
  if (!MakeOpenWorkingCopy(src,tmp,sizeof(tmp)))
  {
    char msg[2600];
    snprintf(msg,sizeof(msg),"Could not read:\n%s",src);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONERROR);
    return false;
  }

  if (Editor_Active()) Editor_Close(hwndDlg);
  DiscardPending();

  lstrcpyn_safe(g_pending_fn,tmp,sizeof(g_pending_fn));
  g_pending_unsaved = true;
  lstrcpyn_safe(g_open_src_fn,src,sizeof(g_open_src_fn));
  UpdateDimBoxes(hwndDlg);
  InvalidateRect(hwndDlg,NULL,TRUE);

  // Editor_Open reads the copy: a file that is not a GIF we can parse drops it
  // and leaves nothing pending, so the original stays as it was.
  if (!Editor_Open(hwndDlg))
  {
    DiscardPending();
    InvalidateRect(hwndDlg,NULL,TRUE);
    return false;
  }
  return true;
}

static void OpenGifToEdit(HWND hwndDlg)
{
  if (g_cap_state || Busy_Active()) return;

  // Picker start folder: the GIF opened last, then the last Save As path, then
  // the folder the drop-down under Save points at.
  char initdir[2048];
  if (g_open_src_fn[0]) FolderOfFile(g_open_src_fn,initdir,sizeof(initdir));
  else if (g_last_save_fn[0]) FolderOfFile(g_last_save_fn,initdir,sizeof(initdir));
  else
  {
    initdir[0] = 0;
    const int bm = Bookmarks_LastUsed();
    if (bm >= 0) lstrcpyn_safe(initdir,Bookmarks_Path(bm),sizeof(initdir));
  }

  static const char extlist[] = "GIF files (*.gif)\0*.gif\0All files (*.*)\0*.*\0";
  char *picked = WDL_ChooseFileForOpen(hwndDlg,"Open a GIF to edit",
                                       initdir[0] ? initdir : NULL,NULL,
                                       extlist,".gif",false,false);
  if (!picked || !picked[0])
  {
    if (picked) free(picked);
    return;
  }

  char src[2048];
  lstrcpyn_safe(src,picked,sizeof(src));
  free(picked);

  // One pending recording at a time: the replacement is decided here, after
  // the picker, so cancelling the dialog never touches what is pending.
  if (g_pending_unsaved && g_pending_fn[0])
  {
    if (MessageBoxA(hwndDlg,
          "A recording that has not been saved yet will be replaced.\n\nDiscard it?",
          "LICEcap",MB_YESNO|MB_ICONWARNING) != IDYES)
      return;
  }

  OpenGifInEditor(hwndDlg,src);
}

// What clicking the label of the Edit button does. With a pending recording it
// opens that one; with nothing recorded it goes straight to the file picker,
// because a GIF from disk is the only thing there is to edit.
static void EditButtonAction(HWND hwndDlg)
{
  if (g_cap_state || Busy_Active()) return;
  if (Editor_Active())
  {
    Editor_Close(hwndDlg);
    return;
  }
  if (g_pending_unsaved && g_pending_fn[0])
  {
    Editor_Open(hwndDlg);
    return;
  }
  OpenGifToEdit(hwndDlg);
}

static bool EnsurePauseSnapshot(HWND hwndDlg)
{
  if (!g_cap_bm) return false;
  if (!g_cap_bm_txt)
  {
    g_cap_bm_txt = LICE_CreateSysBitmap(g_cap_bm->getWidth(),g_cap_bm->getHeight());
    LICE_Copy(g_cap_bm_txt,g_cap_bm);
  }
  return g_cap_bm_txt != NULL;
}

static void TogglePause(HWND hwndDlg)
{
  if (g_cap_state == 1)
  {
    g_pause_time = timeGetTime();
    EnsurePauseSnapshot(hwndDlg);
    g_cap_state = 2;
    UpdateDimBoxes(hwndDlg);
  }
  else if (g_cap_state == 2)
  {
    delete g_cap_bm_txt;
    g_cap_bm_txt = NULL;
    g_insert_cnt = 0;
    g_last_frame_capture_time = g_cap_prerolluntil = timeGetTime()+PREROLL_AMT;
    g_cap_state = 1;
    UpdateDimBoxes(hwndDlg);
  }
}

static bool BeginCapture(HWND hwndDlg, const char *fn)
{
  int w,h;
  GetViewRectSize(&w,&h);
  if (w < 2 || h < 2) return false;

  lstrcpyn_safe(g_last_fn,fn,sizeof(g_last_fn));

  delete g_cap_bm;
#ifdef _WIN32
  g_cap_bm = LICE_CreateSysBitmap(w,h);
#else
  delete g_cap_bm_inv;
  g_cap_bm_inv = LICE_CreateSysBitmap(w,h);
  g_cap_bm = new LICE_WrapperBitmap(g_cap_bm_inv->getBits(),g_cap_bm_inv->getWidth(),g_cap_bm_inv->getHeight(),g_cap_bm_inv->getRowSpan(),true);
#endif

  g_dotitle = ((g_prefs&1) && g_titlems);

  if (strlen(g_last_fn)>4 && !stricmp(g_last_fn+strlen(g_last_fn)-4,".gif"))
  {
    void *ctx = LICE_WriteGIFBeginNoFrame(g_last_fn,w,h,(g_prefs&32) ? (-1)&~7 : 0,true);
    if (ctx) g_cap_gif = new gif_encoder(ctx,g_gif_loopcount,0xf8);
    g_cap_gif_lastsec_written = -1;

#ifdef TEST_MULTIPLE_MODES
    char tmp[1024];
    sprintf(tmp,"%s.trans-nostats.gif",g_last_fn);
    ctx = LICE_WriteGIFBeginNoFrame(tmp,w,h,(-1)&~(7|0x100),true);
    if (ctx) g_cap_gif2 = new gif_encoder(ctx,g_gif_loopcount,0xf8);

    sprintf(tmp,"%s.trans-0.gif",g_last_fn);
    ctx = LICE_WriteGIFBeginNoFrame(tmp,w,h,0,true);
    if (ctx) g_cap_gif3 = new gif_encoder(ctx,g_gif_loopcount,0xf8);
#endif
  }
#ifdef VIDEO_ENCODER_SUPPORT
  if (strlen(g_last_fn)>5 && !stricmp(g_last_fn+strlen(g_last_fn)-5,".webm"))
  {
#ifdef REAPER_LICECAP
    static VideoEncoder *(*video_createEncoder2)(const char *);
    if (!video_createEncoder2)
      *(void **)&video_createEncoder2 = reaperAPI_getfunc("video_createEncoder2");

    if (video_createEncoder2) g_cap_video = video_createEncoder2("WEBM");
#endif

    if (g_cap_video)
    {
      if (!g_cap_video->open(g_last_fn,"webm", "vp8", "vorbis", g_cap_video_vbr, 100, w,h, 30, g_cap_video_abr, 44100,2))
      {
        delete g_cap_video;
        g_cap_video=0;
      }
      else
      {
        strcpy(g_cap_video_ext,"WEBM");
#ifdef REAPER_LICECAP
        s_audiohook_needsilence=0.0;
        g_cap_video_lastt=-1.0;
        s_audiohook_timepos=0.0;
        if (!s_audiohook_reg)
        {
          Audio_RegHardwareHook(true,&s_audiohook);
          s_audiohook_reg=true;
        }
        if (!s_audiothread)
        {
          unsigned id;
          s_audiothread = (HANDLE)_beginthreadex(NULL,0,audioEncodeThread,0,0,&id);
        }
#endif
      }
    }
  }
#endif

#ifndef NO_LCF_SUPPORT
  if (strlen(g_last_fn)>4 && !stricmp(g_last_fn+strlen(g_last_fn)-4,".lcf"))
  {
    g_cap_lcf = new LICECaptureCompressor(g_last_fn,w,h);
    if (!g_cap_lcf->IsOpen())
    {
      delete g_cap_lcf;
      g_cap_lcf = NULL;
    }
  }
#endif

  if (!(g_cap_gif
#ifndef NO_LCF_SUPPORT
        || g_cap_lcf
#endif
#ifdef VIDEO_ENCODER_SUPPORT
        || g_cap_video
#endif
      ))
  {
    delete g_cap_bm;
    g_cap_bm = NULL;
    return false;
  }

  if (g_dotitle)
    WriteTextFrame(g_title,g_titlems,true,w,h);

#ifdef _WIN32
  SaveRestoreRecRect(hwndDlg,false);
  g_last_wndstyle = SetWindowLong(hwndDlg,GWL_STYLE,GetWindowLong(hwndDlg,GWL_STYLE)&~WS_THICKFRAME);
  SetWindowPos(hwndDlg,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_DRAWFRAME|SWP_NOACTIVATE);
  SaveRestoreRecRect(hwndDlg,true);
#else
  if (g_capwnd_levelsave < 0)
  {
    g_capwnd_levelsave=SWELL_SetWindowLevel(hwndDlg,1000);
  }
  SWELL_SetWindowResizeable(hwndDlg,false);

#ifdef __APPLE__
  {
    s_has_async_offset=false;

    RECT r,r2;
    if (GetAsyncNSWindowRect(hwndDlg,&r))
    {
      GetWindowRect(GetDlgItem(hwndDlg,IDC_VIEWRECT),&r2);
      s_async_offset.x = r2.left - r.left;
      s_async_offset.y = r2.top - r.top;
      s_has_async_offset = true;
    }
  }
#endif
#endif

  g_insert_cnt = 0;
  g_frate_valid=false;
  g_frate_avg=0.0;
  g_ms_written = 0;

  g_last_frame_capture_time = g_cap_prerolluntil = timeGetTime()+PREROLL_AMT;
  g_cap_state=1;
  UpdateDimBoxes(hwndDlg);
  InvalidateRect(hwndDlg,NULL,TRUE);
  return true;
}

static void StartTempCapture(HWND hwndDlg)
{
  DiscardPending();

  char dir[MAX_PATH];
  dir[0] = 0;
  DWORD n = GetTempPathA(sizeof(dir),dir);
  if (!n || n >= sizeof(dir)) lstrcpyn_safe(dir,".",sizeof(dir));

  SYSTEMTIME st;
  GetLocalTime(&st);
  const int dl = (int)strlen(dir);
  const char *sep = (dl && (dir[dl-1] == '\\' || dir[dl-1] == '/')) ? "" : "\\";

  char fn[MAX_PATH];
  snprintf(fn,sizeof(fn),"%s%sLICEcap_%04d%02d%02d_%02d%02d%02d.gif",dir,sep,
           st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);

  g_rec_is_temp = true;
  if (!BeginCapture(hwndDlg,fn))
  {
    g_rec_is_temp = false;
    DeleteFileA(fn);
    MessageBoxA(hwndDlg,"Could not start the capture.","LICEcap",MB_OK|MB_ICONERROR);
    return;
  }
  lstrcpyn_safe(g_pending_fn,fn,sizeof(g_pending_fn));
  g_pending_unsaved = false;
}

static void RecordToFile(HWND hwndDlg)
{
  const char *tab[][2]={
    { "GIF files (*.gif)\0*.gif\0", ".gif" },
  #ifndef NO_LCF_SUPPORT
    { "LiceCap files (*.lcf)\0*.lcf\0", ".lcf" },
  #endif
  #ifdef VIDEO_ENCODER_SUPPORT
    { "WEBM files (*.webm)\0*.webm\0", ".webm" },
  #endif
    {NULL,NULL},
  };

  WDL_Queue qb;
  int bm=0;
  const int lfnlen=(int)strlen(g_last_fn);
  int x;
  if (lfnlen >= 3) for (x=0;tab[x][0];x++)
  {
    const int tx1l = (int)strlen(tab[x][1]);
    if (lfnlen > tx1l && !stricmp(g_last_fn + lfnlen - tx1l, tab[x][1]))
    {
      bm=x;
      break;
    }
  }
  for (x=bm;tab[x][0];x++)
  {
    const char *p=tab[x][0];
    while (*p)
    {
      int l = (int)strlen(p)+1;
      qb.Add(p,l);
      p+=l;
    }
  }
  for (x=0;x<bm;x++)
  {
    const char *p=tab[x][0];
    while (*p)
    {
      int l = (int)strlen(p)+1;
      qb.Add(p,l);
      p+=l;
    }
  }
  qb.Add("",1);

  char fn[2048];
  lstrcpyn_safe(fn,g_last_fn,sizeof(fn));

  if (!WDL_ChooseFileForSave(hwndDlg, "Choose file for recording", NULL, fn,
      (char*)qb.Get(), tab[bm][1] + 1, false, fn, sizeof(fn),
      MAKEINTRESOURCE(IDD_SAVEOPTS),(void*)SaveOptsProc,
#ifdef _WIN32
      g_hInst
#else
      SWELL_curmodule_dialogresource_head
#endif
      ))
    return;

  DiscardPending();
  WritePrivateProfileString("licecap","lastfn",fn,g_ini_file.Get());

#ifdef _WIN32
  if (g_prefs&16)
  {
    RegisterHotKey(hwndDlg, IDC_REC, MOD_CONTROL|MOD_ALT, 'P');
  }
#endif

  g_rec_is_temp = false;
  if (!BeginCapture(hwndDlg,fn))
    MessageBoxA(hwndDlg,"Could not start the capture.","LICEcap",MB_OK|MB_ICONERROR);
}

static bool HasGifExt(const char *fn)
{
  const int l = (int)strlen(fn);
  return l > 4 && !stricmp(fn+l-4,".gif");
}

static void MakeAutoSaveName(char *dst, int dstsz)
{
  SYSTEMTIME st;
  GetLocalTime(&st);
  snprintf(dst,dstsz,"gifcam_%04d-%02d-%02d_%02d-%02d-%02d.gif",
           st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
}

// Name "Save As" proposes: a GIF opened from disk keeps its name with "-edit"
// appended, so the result lands next to the original and says where it came
// from. A fresh recording keeps the timestamp.
static void MakeSaveNameForPending(char *dst, int dstsz)
{
  if (!g_open_src_fn[0])
  {
    MakeAutoSaveName(dst,dstsz);
    return;
  }

  char base[128];
  lstrcpyn_safe(base,WDL_get_filepart(g_open_src_fn),sizeof(base));
  const int l = (int)strlen(base);
  if (l > 4 && !stricmp(base+l-4,".gif")) base[l-4] = 0;
  if (!base[0]) lstrcpyn_safe(base,"animation",sizeof(base));
  snprintf(dst,dstsz,"%s-edit.gif",base);
}

// A save that lands inside a bookmarked folder makes it the folder the
// drop-down under Save shows (and preselects next time the list opens).
static void RememberBookmarkForFile(const char *fn)
{
  char dir[BOOKMARK_PATH_LEN];
  const char *p;
  int bm;
  if (!fn || !fn[0]) return;
  lstrcpyn_safe(dir,fn,sizeof(dir));
  p = WDL_get_filepart(dir);
  if (!p || p == dir) return;
  dir[p-dir] = 0;
  Bookmark_StripTrailingSlashes(dir);
  bm = Bookmarks_IndexOf(dir);
  if (bm >= 0) Bookmarks_SetLastUsed(bm);
}

static void DoSaveAs(HWND hwndDlg)
{
  if (!g_pending_unsaved || !g_pending_fn[0]) return;

  char initdir[2048];
  initdir[0] = 0;
  {
    const int lastbm = Bookmarks_LastUsed();
    if (g_open_src_fn[0])
    {
      // a GIF opened from disk is saved next to the file it came from
      FolderOfFile(g_open_src_fn,initdir,sizeof(initdir));
    }
    else if (g_last_save_fn[0])
    {
      lstrcpyn_safe(initdir,g_last_save_fn,sizeof(initdir));
      const char *p = WDL_get_filepart(initdir);
      if (p) initdir[p-initdir] = 0;
    }
    else if (lastbm >= 0)
    {
      // start where the drop-down under Save says recordings go
      lstrcpyn_safe(initdir,Bookmarks_Path(lastbm),sizeof(initdir));
    }
    else if (Bookmarks_Count() > 0)
    {
      // first run: start in the first bookmark instead of the default folder
      lstrcpyn_safe(initdir,Bookmarks_Path(0),sizeof(initdir));
    }
  }

  char defname[256];
  MakeSaveNameForPending(defname,sizeof(defname));

  char fn[2048];
  fn[0] = 0;
  static const char extlist[] = "GIF files (*.gif)\0*.gif\0All files (*.*)\0*.*\0";
  if (!WDL_ChooseFileForSave(hwndDlg,"Save the animation as",initdir[0] ? initdir : NULL,defname,
                             extlist,".gif",false,fn,sizeof(fn)))
    return;

  if (!HasGifExt(fn)) lstrcatn(fn,".gif",sizeof(fn));

  // copying to another volume, or into a network bookmark, can block for a
  // while: say so before the call instead of leaving a dead window
  BusySaveJob job = {g_pending_fn, fn, ERROR_NOT_ENOUGH_MEMORY};
  const bool saved = Busy_Run(hwndDlg, "Saving recording...", 0, Busy_Save, &job);
  const DWORD saveerr = job.error;

  if (!saved)
  {
    char msg[2800];
    snprintf(msg,sizeof(msg),"Could not save the animation to:\n%s\n\n(error %d)",fn,(int)saveerr);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONERROR);
    return;
  }

  lstrcpyn_safe(g_last_save_fn,fn,sizeof(g_last_save_fn));
  RememberBookmarkForFile(fn);
  g_pending_fn[0] = 0;
  g_pending_unsaved = false;
  g_open_src_fn[0] = 0;
  g_rec_is_temp = false;
  WritePrivateProfileString("licecap","lastsave",g_last_save_fn,g_ini_file.Get());
  UpdateDimBoxes(hwndDlg);
}

// Blender-style "save here now": writes the pending temp recording into the
// bookmarked folder using an auto-generated, unique file name.
static bool SavePendingToFolder(HWND hwndDlg, const char *folder)
{
  char base[256], fn[2048];
  int n, bl;

  if (!g_pending_unsaved || !g_pending_fn[0] || !folder || !folder[0]) return false;

  MakeAutoSaveName(base,sizeof(base));
  bl = (int)strlen(base);
  if (bl > 4 && !stricmp(base+bl-4,".gif")) base[bl-4] = 0;

  for (n = 1; n < 1000; n++)
  {
    if (n == 1) snprintf(fn,sizeof(fn),"%s\\%s.gif",folder,base);
    else snprintf(fn,sizeof(fn),"%s\\%s-%d.gif",folder,base,n);
    if (GetFileAttributesA(fn) == INVALID_FILE_ATTRIBUTES) break;
  }
  if (n >= 1000) return false;

  BusySaveJob job = {g_pending_fn, fn, ERROR_NOT_ENOUGH_MEMORY};
  const bool saved = Busy_Run(hwndDlg, "Saving recording...", 0, Busy_Save, &job);
  const DWORD saveerr = job.error;

  if (!saved)
  {
    char msg[2800];
    snprintf(msg,sizeof(msg),"Could not save the animation to:\n%s\n\n(error %d)",fn,(int)saveerr);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONERROR);
    return false;
  }

  lstrcpyn_safe(g_last_save_fn,fn,sizeof(g_last_save_fn));
  RememberBookmarkForFile(fn);
  g_pending_fn[0] = 0;
  g_pending_unsaved = false;
  g_open_src_fn[0] = 0;
  g_rec_is_temp = false;
  WritePrivateProfileString("licecap","lastsave",g_last_save_fn,g_ini_file.Get());
  UpdateDimBoxes(hwndDlg);
  return true;
}

static void SaveToBookmark(HWND hwndDlg, int idx)
{
  DWORD attr;
  if (idx < 0 || idx >= Bookmarks_Count()) return;
  attr = GetFileAttributesA(Bookmarks_Path(idx));
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY))
  {
    char msg[1600];
    snprintf(msg,sizeof(msg),"The bookmark folder no longer exists:\n%s\n\nRemove it from the list?",Bookmarks_Path(idx));
    if (MessageBoxA(hwndDlg,msg,"LICEcap",MB_YESNO|MB_ICONQUESTION) == IDYES)
      Bookmarks_Remove(idx);
    return;
  }
  SavePendingToFolder(hwndDlg,Bookmarks_Path(idx));
}

static void AddBookmarkWithFeedback(HWND hwndDlg, const char *dir)
{
  const int r = Bookmarks_Add(dir);
  if (r == 1)
  {
    char msg[1600];
    snprintf(msg,sizeof(msg),"That folder is already in the bookmarks:\n%s",dir);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONINFORMATION);
  }
  else if (r == 3)
  {
    char msg[200];
    snprintf(msg,sizeof(msg),"The bookmark list is full (%d).\nRemove one before adding another.",
             LICECAP_MAX_BOOKMARKS);
    MessageBoxA(hwndDlg,msg,"LICEcap",MB_OK|MB_ICONINFORMATION);
  }
  else if (r == 2)
  {
    MessageBoxA(hwndDlg,"That folder no longer exists.","LICEcap",MB_OK|MB_ICONWARNING);
  }
}


static void ShowInFolder(HWND hwndDlg)
{
  // A GIF opened from disk is edited through a copy that lives in the temp
  // folder; that is not the folder the user means, so show the original.
  const char *fn = g_open_src_fn[0] && g_pending_unsaved && g_pending_fn[0] ? g_open_src_fn
                 : g_pending_unsaved && g_pending_fn[0] ? g_pending_fn : g_last_save_fn;
  if (!fn[0]) return;
  char cmd[2600];
  snprintf(cmd,sizeof(cmd),"explorer.exe /select,\"%s\"",fn);
  WinExec(cmd,SW_SHOWNORMAL);
  (void)hwndDlg;
}


// "/" no esta en la misma tecla en todos los layouts (en-US es VK_OEM_2, en
// es-ES/es-CR es Shift+7). VkKeyScanA('/') dice que tecla y si hace falta
// Shift en el layout activo, asi que la tecla real del usuario siempre vale.
static bool MsgIsLayoutSlashKey(WPARAM vk)
{
  const SHORT scan = VkKeyScanA('/');
  if (scan == -1) return false;
  if ((vk & 0xFF) != (WPARAM)(scan & 0xFF)) return false;
  const bool need_shift = ((scan >> 8) & 1) != 0;
  const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
  return need_shift == shift;
}

static LRESULT CALLBACK LiceKbdHook(int code, WPARAM wParam, LPARAM lParam)
{
  if (code == HC_ACTION && g_hwnd && lParam)
  {
    const MSG *m = (const MSG*)lParam;
    if (m->message == WM_KEYDOWN && m->hwnd &&
        (m->hwnd == g_hwnd || IsChild(g_hwnd,m->hwnd)))
    {
      const bool ctrl = (GetKeyState(VK_CONTROL)&0x8000) != 0;
      // The timeline editor owns the keyboard while it is open (space, arrows,
      // A/B, Enter, Esc), so the toolbar shortcuts do not fire underneath it.
      if (!ctrl && Editor_Active())
      {
        if (Editor_Key(g_hwnd, m->wParam))
        {
          // swallow it: otherwise it still reaches the focused button
          ((MSG *)m)->message = WM_NULL;
          return 1;
        }
      }
      int cmd = 0;
      if (ctrl && m->wParam=='N') cmd = IDM_NEW;
      else if (ctrl && m->wParam=='S') cmd = IDM_SAVEAS;
      else if (ctrl && m->wParam=='O') cmd = IDM_OPEN_GIF;
      else if (ctrl && m->wParam=='U') cmd = IDM_CUSTOMIZE;
      else if (!ctrl && (m->wParam==VK_OEM_2 || m->wParam==VK_DIVIDE || MsgIsLayoutSlashKey(m->wParam))) cmd = IDM_HALF_SIZE;
      else if (!ctrl && m->wParam==VK_F1) cmd = IDM_FPS10;
      else if (!ctrl && m->wParam==VK_F2) cmd = IDM_FPS16;
      else if (!ctrl && m->wParam==VK_F3) cmd = IDM_FPS30;
      else if (!ctrl && m->wParam==VK_F4) cmd = IDM_PAUSE;
      if (cmd)
      {
        PostMessage(g_hwnd,WM_COMMAND,(WPARAM)cmd,0);
        return 1;
      }
    }
  }
  return CallNextHookEx(g_kbd_hook,code,wParam,lParam);
}

static WDL_DLGRET liceCapMainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static POINT s_last_mouse;
  switch (uMsg)
  {
    case WM_INITDIALOG:
    {
      g_hwnd=hwndDlg;
#ifdef _WIN32
      // DialogBox() adds a system caption to the template. The window keeps a
      // region for the capture hole, and Windows draws classic (light) frames
      // for region windows, so the caption is dropped and the app paints its
      // own title bar in both themes.
      {
        LONG st = GetWindowLong(hwndDlg, GWL_STYLE);
        st &= ~(WS_CAPTION | DS_MODALFRAME);
        SetWindowLong(hwndDlg, GWL_STYLE, st);
        SetWindowPos(hwndDlg, NULL, 0, 0, 0, 0,
                     SWP_NOSIZE|SWP_NOMOVE|SWP_NOZORDER|SWP_FRAMECHANGED);
      }
#endif
#ifdef _WIN32
      SetClassLongPtr(hwndDlg,GCLP_HICON,(LPARAM)LoadIcon(GetModuleHandle(NULL),MAKEINTRESOURCE(IDI_ICON1)));
#elif defined(__APPLE__)
      extern void SWELL_SetWindowShadow(HWND, bool);
      void SetNSWindowOpaque(HWND, bool);
      void SetNSWindowTransparent(HWND);
      SWELL_SetWindowShadow(hwndDlg,false);
      SetNSWindowOpaque(hwndDlg,false);
      SetNSWindowTransparent(hwndDlg);
#endif

      g_wndsize.init(hwndDlg);
      g_dark_ui = GetPrivateProfileInt("licecap", "dark", 1, g_ini_file.Get()) != 0;
      UI_ApplyAppMode();
      UI_Init();
      UI_ThemeDialog(hwndDlg);
      SendDlgItemMessage(hwndDlg, IDC_STATUS, WM_SETFONT, (WPARAM)(g_ui_font_small ? g_ui_font_small : GetStockObject(DEFAULT_GUI_FONT)), TRUE);

      ++g_reent;
      g_gif_loopcount = GetPrivateProfileInt("licecap","gifloopcnt",g_gif_loopcount,g_ini_file.Get());
      g_max_fps = GetPrivateProfileInt("licecap", "maxfps", g_max_fps, g_ini_file.Get());
      g_prefs = GetPrivateProfileInt("licecap", "prefs", g_prefs, g_ini_file.Get());
      g_titlems = GetPrivateProfileInt("licecap", "titlems", g_titlems, g_ini_file.Get());
      g_stop_after_msec = GetPrivateProfileInt("licecap", "stopafter", g_stop_after_msec, g_ini_file.Get());
      g_capture_cursor = GetPrivateProfileInt("licecap", "cursor", 1, g_ini_file.Get()) != 0;
      g_always_on_top = GetPrivateProfileInt("licecap", "ontop", 0, g_ini_file.Get()) != 0;
      GetPrivateProfileString("licecap","title","",g_title,sizeof(g_title),g_ini_file.Get());
      GetPrivateProfileString("licecap","lastsave","",g_last_save_fn,sizeof(g_last_save_fn),g_ini_file.Get());
      Bookmarks_Load();
      --g_reent;

      Capture_Finish(hwndDlg);

      SetTimer(hwndDlg,1,30,NULL);

      bool restored=false;
      if (!GetAsyncKeyState(VK_SHIFT))
      {
        char buf[1024];
        GetPrivateProfileString("licecap","wnd_r","",buf,sizeof(buf),g_ini_file.Get());
        int a[4]={0,};
        const char *p=buf;
        int x;
        const int maxcnt  = 4;
        for (x=0;x<maxcnt;x++)
        {
          a[x] = atoi(p);
          if (x==maxcnt-1) break;
          while (*p && *p != ' ') p++;
          while (*p == ' ') p++;
        }
        if (*p && a[2]>a[0] && a[3]!=a[1])
        {
          EnsureNotCompletelyOffscreen((RECT*)a);
          SetWindowPos(hwndDlg,NULL,a[0],a[1],a[2]-a[0],a[3]-a[1],SWP_NOZORDER|SWP_NOACTIVATE);
          restored=true;
        }
      }
      if (!restored)
      {
        RECT wr;
        GetWindowRect(hwndDlg,&wr);
        SetWindowPos(hwndDlg,NULL,wr.left,wr.top,UI(512),UI(512),SWP_NOZORDER|SWP_NOACTIVATE);
      }
      if (g_always_on_top)
        SetWindowPos(hwndDlg,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

      SendMessage(hwndDlg,WM_SIZE,0,0);
      UpdateDimBoxes(hwndDlg);

#ifdef _WIN32
      g_kbd_hook = SetWindowsHookEx(WH_GETMESSAGE, LiceKbdHook, NULL, GetCurrentThreadId());
#endif

#ifdef VIDEO_ENCODER_SUPPORT
      g_cap_video_vbr = GetPrivateProfileInt("licecap", "video_vbr", g_cap_video_vbr, g_ini_file.Get());
      g_cap_video_abr = GetPrivateProfileInt("licecap", "video_abr", g_cap_video_abr, g_ini_file.Get());
#endif

      // A GIF named on the command line is opened once the window exists and
      // its size is applied; the editor needs the finished layout.
      if (g_startup_gif[0]) PostMessage(hwndDlg,WM_APP+1,0,0);

    return 1;
    }
    case WM_APP+1:   // "licecap.exe some.gif": open it in the timeline editor
      if (g_startup_gif[0])
      {
        char fn[2048];
        lstrcpyn_safe(fn,g_startup_gif,sizeof(fn));
        g_startup_gif[0] = 0;
        OpenGifInEditor(hwndDlg,fn);
      }
    break;
    case WM_DESTROY:

      Capture_Finish(hwndDlg);
      Editor_Shutdown();

      KillTimer(hwndDlg,1);
#ifdef _WIN32
      if (g_kbd_hook) { UnhookWindowsHookEx(g_kbd_hook); g_kbd_hook = NULL; }
#endif
      DiscardPending();

      SaveConfig(hwndDlg);

      g_wndsize.init(hwndDlg);
      g_hwnd=NULL;
#ifndef _WIN32
      SWELL_PostQuitMessage(0);
#endif

    break;
    case WM_NCDESTROY:
      UI_Quit();
    break;

    case WM_TIMER:
      if (Busy_Active()) break;
      if (wParam==2)
      {
        Editor_Tick(hwndDlg);
        break;
      }
      if (wParam==1)
      {     
        static bool themed;
        if (!themed)
        {
          // the window has to exist for the theme to reach its popup menus
          themed = true;
          UI_ThemeDialog(hwndDlg);
          UI_SetDarkTitleBar(hwndDlg);
          InvalidateRect(hwndDlg,NULL,TRUE);
        }
        const DWORD now=timeGetTime();
        bool need_stop=false;

        UpdateHoverState(hwndDlg);

        if (g_cap_state==1 && g_cap_bm && (!g_cap_prerolluntil || !WDL_TICKS_IN_RANGE_ENDING_AT(now,g_cap_prerolluntil,MAX_PREROLL_OR_DELAY)))
        {
          if (now-g_last_frame_capture_time >= (1000/(wdl_max(g_max_fps,1))))
          {
            g_ms_written += now-g_last_frame_capture_time;

#ifdef _WIN32
            HWND h = GetDesktopWindow();

            HDC hdc = GetDC(h);
            if (hdc)
            {
              RECT r;
              GetWindowRect(GetDlgItem(hwndDlg,IDC_VIEWRECT),&r);
              int bw = g_cap_bm->getWidth();
              int bh = g_cap_bm->getHeight();

              __LogicalToPhysicalPointForPerMonitorDPI(g_hwnd,(LPPOINT)&r);
              
              LICE_Clear(g_cap_bm,0);
              BitBlt(g_cap_bm->getDC(),0,0,bw,bh,hdc,r.left+1,r.top+1,SRCCOPY);
              ReleaseDC(h,hdc);
              DoMouseCursor(g_cap_bm,h,-(r.left+1),-(r.top+1));                        
#else
            int bw = g_cap_bm->getWidth();
            int bh = g_cap_bm->getHeight();
            RECT r2,r3;
            HWND sub = GetDlgItem(hwndDlg,IDC_VIEWRECT);
            GetWindowRect(sub,&r2);
#ifdef __APPLE__
            if (s_has_async_offset && GetAsyncNSWindowRect(hwndDlg,&r3))
            {
              const int w = r2.right-r2.left;
              const int h = r2.bottom-r2.top;
              r2.left = r3.left + s_async_offset.x;
              r2.top = r3.top + s_async_offset.y;

              r2.bottom = r2.top + h;
              r2.right = r2.left + w;
            }
#endif

            if (GetScreenData(r2.left,wdl_min(r2.top,r2.bottom),g_cap_bm_inv?g_cap_bm_inv:g_cap_bm))
            {
              void DoMouseCursor(LICE_IBitmap *,int,int);
              DoMouseCursor(g_cap_bm_inv?g_cap_bm_inv:g_cap_bm,-(r2.left+1),-(r2.bottom+1));                        
#endif
              const bool dotime = !!(g_prefs&8);

              const int frame_time_in_seconds = g_ms_written/1000;
              
#ifdef VIDEO_ENCODER_SUPPORT
              if (g_cap_video)
              {
                if (dotime) draw_timedisp(g_cap_bm,frame_time_in_seconds,NULL,bw,bh);

                EncodeFrameToVideo(g_cap_video,g_cap_bm);
              }
#endif
#ifndef NO_LCF_SUPPORT
              if (g_cap_lcf)
              {
                if (dotime) draw_timedisp(g_cap_bm,frame_time_in_seconds,NULL,bw,bh);

                int del = (int) (now-g_last_frame_capture_time);
                if (g_dotitle)
                {
                  del += g_titlems;
                  g_dotitle=false;
                }
                g_cap_lcf->OnFrame(g_cap_bm,del);
              }
#endif

              if (g_cap_gif)
              {
                // draw old time display for frame_compare(), so that it finds the portion other than the time display that changes
                int old_time_coords[4]={0,};
                if (dotime && g_cap_gif_lastsec_written>=0)
                  draw_timedisp(g_cap_bm,g_cap_gif_lastsec_written,old_time_coords,bw,bh);
            
                g_cap_gif->frame_advancetime(now-g_last_frame_capture_time);
#ifdef TEST_MULTIPLE_MODES
                if (g_cap_gif2) g_cap_gif2->frame_advancetime(now-g_last_frame_capture_time);
                if (g_cap_gif3) g_cap_gif3->frame_advancetime(now-g_last_frame_capture_time);
#endif

                int diffs[4];
                
                if (g_cap_gif->frame_compare(g_cap_bm,diffs))
                {
                  g_cap_gif->frame_finish();
#ifdef TEST_MULTIPLE_MODES
                  if (g_cap_gif2) g_cap_gif2->frame_finish();
                  if (g_cap_gif3) g_cap_gif3->frame_finish();
#endif

                  if (dotime && frame_time_in_seconds != g_cap_gif_lastsec_written)
                  {
                    int pos[4];
                    draw_timedisp(NULL,frame_time_in_seconds,pos,bw,bh);

                    union_diffs(pos, old_time_coords);

                    if (diffs[0]+diffs[2] >= pos[0] && diffs[1]+diffs[3] >= pos[1])
                    {
                      union_diffs(diffs, pos); // add pos into diffs for display update

                      draw_timedisp(g_cap_bm,frame_time_in_seconds,pos,bw,bh);
                      g_cap_gif_lastsec_written = frame_time_in_seconds;
                    }
                  }

                  g_cap_gif->frame_new(g_cap_bm,diffs[0],diffs[1],diffs[2],diffs[3]);
#ifdef TEST_MULTIPLE_MODES
                  if (g_cap_gif2) g_cap_gif2->frame_new(g_cap_bm,diffs[0],diffs[1],diffs[2],diffs[3]);
                  if (g_cap_gif3) g_cap_gif3->frame_new(g_cap_bm,diffs[0],diffs[1],diffs[2],diffs[3]);
#endif
                }

                if (dotime && frame_time_in_seconds != g_cap_gif_lastsec_written)
                {
                  // time changed and wasn't previously included, so include as a dedicated frame
                  g_cap_gif->frame_finish();
#ifdef TEST_MULTIPLE_MODES
                  if (g_cap_gif2) g_cap_gif2->frame_finish();
                  if (g_cap_gif3) g_cap_gif3->frame_finish();
#endif

                  int pos[4];
                  draw_timedisp(g_cap_bm,frame_time_in_seconds,pos,bw,bh);
                  union_diffs(pos, old_time_coords);

                  g_cap_gif_lastsec_written = frame_time_in_seconds;
                  g_cap_gif->frame_new(g_cap_bm,pos[0],pos[1],pos[2],pos[3]);
#ifdef TEST_MULTIPLE_MODES
                  if (g_cap_gif2) g_cap_gif2->frame_new(g_cap_bm,pos[0],pos[1],pos[2],pos[3]);
                  if (g_cap_gif3) g_cap_gif3->frame_new(g_cap_bm,pos[0],pos[1],pos[2],pos[3]);
#endif
                }
              }

              double fr = 1000.0 / (double) (now - g_last_frame_capture_time);
              if (fr>100.0) fr=100.0;

              if (g_frate_valid) 
              {
                g_frate_avg = g_frate_avg*0.9 + fr*0.1;
              }
              else 
              {
                g_frate_avg=fr;
                g_frate_valid=true;
              }

              g_last_frame_capture_time = now;

              if (g_prefs&64) 
              {
                if (g_ms_written > g_stop_after_msec)
                {
                  need_stop=true;
                }
              }
            }
          }
        }

        bool force_status=false;
        if (g_cap_prerolluntil && g_cap_state==1)
        {
          static DWORD lproll;
          static int lcnt;
          if (lproll != g_cap_prerolluntil || ((g_cap_prerolluntil-now)+999)/1000 != lcnt)
          {
            lcnt=((g_cap_prerolluntil-now)+999)/1000;
            lproll=g_cap_prerolluntil;
            UpdateCaption(hwndDlg);
            force_status=true;
          }
        }
        static DWORD last_status_t;
        if (force_status || now-last_status_t > 500)
        {
          last_status_t=now;
          UpdateStatusText(hwndDlg);
        }
        if (need_stop)
          SendMessage(hwndDlg,WM_COMMAND,IDC_STOP,0);

      }
    break;
#ifdef _WIN32
    case WM_HOTKEY:
      if (lParam == MAKELPARAM(MOD_CONTROL|MOD_ALT, 'P') && (g_prefs&16)) // prefs check not necessary
      {
        SendMessage(hwndDlg, WM_COMMAND, IDC_REC, 0);
      }
    break;   
#else
    case WM_PAINT:

      // osx hook
      {
        PAINTSTRUCT ps;
        if (BeginPaint(hwndDlg,&ps))
        {
          RECT r,r2;
          GetWindowRect(GetDlgItem(hwndDlg,IDC_VIEWRECT),&r);
          ScreenToClient(hwndDlg,(LPPOINT)&r);
          ScreenToClient(hwndDlg,((LPPOINT)&r)+1);
          GetClientRect(hwndDlg,&r2);

          RECT tr = r2;
          tr.right = r.left;
          SWELL_FillDialogBackground(ps.hdc,&tr,0);
          tr.left = r.right; tr.right = r2.right;
          SWELL_FillDialogBackground(ps.hdc,&tr,0);
          tr = r;
          tr.bottom = r.top; tr.top = r2.top;
          SWELL_FillDialogBackground(ps.hdc,&tr,0);
          tr.top = r.bottom; tr.bottom = r2.bottom;
          SWELL_FillDialogBackground(ps.hdc,&tr,0);
          SWELL_FillDialogBackground(ps.hdc,&tr,0);

          HPEN pen = CreatePen(PS_SOLID,0,RGB(128,128,128));
          HGDIOBJ oldPen = SelectObject(ps.hdc,pen);
          
          r.left--;r.top--;
          MoveToEx(ps.hdc,r.left,r.top,NULL);
          LineTo(ps.hdc,r.right,r.top);
          LineTo(ps.hdc,r.right,r.bottom);
          LineTo(ps.hdc,r.left,r.bottom);
          LineTo(ps.hdc,r.left,r.top);
          
          GetClientRect(hwndDlg,&r);
          r.right--;r.bottom--;
          MoveToEx(ps.hdc,r.right,r.top,NULL);
          LineTo(ps.hdc,r.right,r.bottom);
          LineTo(ps.hdc,r.left,r.bottom);
          LineTo(ps.hdc,r.left,r.top);
          
          SelectObject(ps.hdc,oldPen);
          DeleteObject(pen);
          EndPaint(hwndDlg,&ps);
        }
      }      
    break;
#endif

#ifdef _WIN32
    case WM_PAINT:
    {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(hwndDlg,&ps);
      if (dc)
      {
        if (Editor_Active()) Editor_Paint(hwndDlg,dc);
        else UI_PaintCaption(hwndDlg,dc);
      }
      EndPaint(hwndDlg,&ps);
    }
    break;

    case WM_ERASEBKGND:
    {
      HDC dc = (HDC)wParam;
      RECT rc;
      GetClientRect(hwndDlg,&rc);
      FillRect(dc,&rc,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(WHITE_BRUSH));

      RECT vr;
      HWND view = GetDlgItem(hwndDlg,IDC_VIEWRECT);
      if (view && GetWindowRect(view,&vr))
      {
        MapWindowPoints(NULL,hwndDlg,(LPPOINT)&vr,2);
        HPEN pen = CreatePen(PS_SOLID,1,COL_SEP);
        HGDIOBJ op = SelectObject(dc,pen);
        const int x = vr.right + UI(1);
        MoveToEx(dc,x,UI_CaptionHeight(),NULL);
        LineTo(dc,x,rc.bottom);
        SelectObject(dc,op);
        DeleteObject(pen);
      }
      return 1;
    }

    case WM_DRAWITEM:
      if (wParam)
      {
        LPDRAWITEMSTRUCT di = (LPDRAWITEMSTRUCT)lParam;
        if (di->CtlID == IDC_CLOSEBTN) DrawCaptionCloseButton(di);
        else DrawToolButton(di);
        return 1;
      }
    break;

    case WM_CTLCOLORSTATIC:
    {
      HDC dc = (HDC)wParam;
      SetBkMode(dc,TRANSPARENT);
      SetTextColor(dc,COL_TEXT_DIS);
      return (LRESULT)(g_ui_bg_brush ? g_ui_bg_brush : GetStockObject(WHITE_BRUSH));
    }

    case WM_CONTEXTMENU:
    {
      HWND child = (HWND)wParam;
      if (child)
      {
        int id = GetDlgCtrlID(child);
        if (id == IDC_REC || id == IDC_EDITBTN || id == IDC_SAVE || id == IDC_EDITMODE)
        {
          ShowToolMenu(hwndDlg,id);
          return 1;
        }
        if (id == IDC_PRESETS)
        {
          ShowPresetMenu(hwndDlg);
          return 1;
        }
      }
    }
    break;
#endif

    case WM_COMMAND:
      if (Busy_Active()) break;   // a GIF read/write is in flight: ignore input
      switch (LOWORD(wParam))
      {
        case IDC_REC:
          if (Editor_Active()) break;   // the timeline owns the window
          if (ArrowZoneNow(hwndDlg,IDC_REC))
          {
            ShowToolMenu(hwndDlg, IDC_REC);
          }
          else if (!g_cap_state)
          {
            StartTempCapture(hwndDlg);
          }
          else if (g_cap_state == 1)
          {
            SendMessage(hwndDlg,WM_COMMAND,IDC_STOP,0);
          }
          else
          {
            TogglePause(hwndDlg);
          }
        break;

        case IDC_EDITBTN:
          ShowToolMenu(hwndDlg, IDC_EDITBTN);
        break;

        case IDC_EDITMODE:
          // Split button: the arrow drops the menu, the label edits.
          if (ArrowZoneNow(hwndDlg,IDC_EDITMODE)) ShowToolMenu(hwndDlg, IDC_EDITMODE);
          else EditButtonAction(hwndDlg);
        break;

        case IDM_EDIT_TOGGLE:
          EditButtonAction(hwndDlg);
        break;

        case IDM_OPEN_GIF:
          if (!g_cap_state) OpenGifToEdit(hwndDlg);
        break;

        case IDC_BOOKMARKDROP:
          // Drop-down of the saved bookmark folders, right under the Save
          // button. It opens with the folder used last already selected; a row
          // click saves the pending recording there, or only picks the folder
          // when there is nothing waiting to be saved.
          if (!g_cap_state && !Editor_Active()) BookmarkPanel_Show(hwndDlg);
        break;

        case IDC_PRESETS:
          ShowPresetMenu(hwndDlg);
        break;

        case IDC_CLOSEBTN:
          SendMessage(hwndDlg,WM_CLOSE,0,0);
        break;

        case IDC_SAVE:
          if (!ArrowZoneNow(hwndDlg,IDC_SAVE) && g_pending_unsaved)
          {
            DoSaveAs(hwndDlg);
            break;
          }
          // Drop-down list of bookmark folders right under the button: one
          // click on a row saves the pending recording there. It only opens
          // when there is something to save and at least one folder, so a row
          // click always does something; otherwise the command menu comes up.
          if (g_pending_unsaved && !Editor_Active() && Bookmarks_Count() > 0)
          {
            BookmarkPanel_Show(hwndDlg);
            break;
          }
          ShowToolMenu(hwndDlg, IDC_SAVE);
        break;

        case IDM_NEW:
          if (!g_cap_state)
          {
            if (Editor_Active()) Editor_Close(hwndDlg);
            DiscardPending();
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_FPS10:
        case IDM_FPS16:
        case IDM_FPS30:
          if (!g_cap_state)
          {
            g_max_fps = LOWORD(wParam)==IDM_FPS10 ? 10 : LOWORD(wParam)==IDM_FPS16 ? 16 : 30;
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_FPS_CUSTOM:
          if (!g_cap_state) ShowCustomization(hwndDlg);
        break;

        case IDM_CAPTURE_CURSOR:
          if (!g_cap_state)
          {
            g_capture_cursor = !g_capture_cursor;
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_CAPTURE_CLICKS:
          if (!g_cap_state)
          {
            g_prefs ^= 4;
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_CAPTURE_TIME:
          if (!g_cap_state)
          {
            g_prefs ^= 8;
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_FULLSCREEN:
          if (!g_cap_state) SetFullScreen(hwndDlg);
        break;

        case IDM_DEFAULT_SIZE:
          if (!g_cap_state) SetDefaultWindowSize(hwndDlg);
        break;

        case IDM_ALWAYS_ON_TOP:
          g_always_on_top = !g_always_on_top;
          SetWindowPos(hwndDlg,g_always_on_top?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
                       SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
          UpdateDimBoxes(hwndDlg);
        break;

        case IDM_DARK_THEME:
          UI_SetDarkMode(hwndDlg, !g_dark_ui);
        break;

        case IDM_CUSTOMIZE:
          if (!g_cap_state) ShowCustomization(hwndDlg);
        break;

        case IDM_PAUSE:
          TogglePause(hwndDlg);
        break;

        case IDM_INSERT_TEXT:
          if (g_cap_state == 2)
          {
            EnsurePauseSnapshot(hwndDlg);
            DialogBox(g_hInst,MAKEINTRESOURCE(IDD_INSERT),hwndDlg,InsertProc);
            g_last_frame_capture_time=timeGetTime();
          }
        break;

        case IDM_INSERT_TITLE:
          if (g_cap_state == 2 && g_title[0] && g_cap_bm_txt)
          {
            WriteTextFrame(g_title,g_titlems,true,g_cap_bm_txt->getWidth(),g_cap_bm_txt->getHeight());
            g_last_frame_capture_time=timeGetTime();
          }
        break;

        case IDM_CANCEL_REC:
          if (g_cap_state)
          {
            SendMessage(hwndDlg,WM_COMMAND,IDC_STOP,0);
            DiscardPending();
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_SAVEAS:
          DoSaveAs(hwndDlg);
        break;

        case IDM_SHOWINFOLDER:
          ShowInFolder(hwndDlg);
        break;

        case IDM_BOOKMARKS_PANEL:
          if (!g_cap_state) BookmarkPanel_Show(hwndDlg);
        break;

        case IDM_BOOKMARK_ADD:
          if (!g_cap_state)
          {
            char dir[BOOKMARK_PATH_LEN];
            if (Bookmark_LastSaveFolder(dir,sizeof(dir)))
              AddBookmarkWithFeedback(hwndDlg,dir);
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_BOOKMARK_PICK:
          if (!g_cap_state)
          {
            char dir[BOOKMARK_PATH_LEN], start[BOOKMARK_PATH_LEN];
            const bool have_start = Bookmark_LastSaveFolder(start,sizeof(start));
            if (Bookmark_PickFolder(hwndDlg,have_start?start:NULL,dir,sizeof(dir)))
              AddBookmarkWithFeedback(hwndDlg,dir);
            UpdateDimBoxes(hwndDlg);
          }
        break;

        case IDM_RECORD_TO_FILE:
          if (!g_cap_state) RecordToFile(hwndDlg);
        break;

        case IDM_ABOUT:
        {
          char msg[1024];
          snprintf(msg,sizeof(msg),
            "LICEcap %s\n"
            "GifCam-style capture window\n\n"
            "Rec starts a capture right away and keeps it in a temporary GIF.\n"
            "Save writes it wherever you want.\n"
            "Edit trims the last recording, or a GIF opened from disk (Ctrl+O).\n\n"
            "LICEcap is free software, licensed under the GNU GPL.",
            LICECAP_VERSION);
          MessageBoxA(hwndDlg,msg,"About LICEcap",MB_OK|MB_ICONINFORMATION);
        }
        break;

        case IDC_STOP:
          g_insert_cnt=0;
          Capture_Finish(hwndDlg);
          if (g_rec_is_temp && g_last_fn[0])
          {
            lstrcpyn_safe(g_pending_fn,g_last_fn,sizeof(g_pending_fn));
            g_pending_unsaved = true;
          }
          g_rec_is_temp = false;
          UpdateDimBoxes(hwndDlg);
          InvalidateRect(hwndDlg,NULL,TRUE);

#ifdef _WIN32
          if (g_prefs&16)
          {          
            UnregisterHotKey(hwndDlg, IDC_REC);
          }
#endif
        break;

        case IDC_INSERT:
          if (g_cap_state == 2)
          {
            EnsurePauseSnapshot(hwndDlg);
            DialogBox(g_hInst,MAKEINTRESOURCE(IDD_INSERT),hwndDlg,InsertProc);
            g_last_frame_capture_time=timeGetTime();
          }
        break;

        case IDCANCEL:
          if (!g_cap_state) SendMessage(hwndDlg,WM_CLOSE,0,0);
        break;

        default:
          {
            const int cmd = LOWORD(wParam);
            if (cmd >= IDM_PRESET_FIRST && cmd < IDM_PRESET_FIRST + PresetCount())
            {
              ApplyPreset(hwndDlg, cmd - IDM_PRESET_FIRST);
            }
            else if (cmd == IDM_HALF_SIZE)
            {
              // Resizing during a capture would break the GIF, same as the
              // greyed-out presets while recording.
              if (!g_cap_state) SetPresetDiv(hwndDlg, g_preset_div == 2 ? 1 : 2);
            }
            else if (cmd >= IDM_BOOKMARK_FIRST && cmd < IDM_BOOKMARK_FIRST + Bookmarks_Count())
            {
              SaveToBookmark(hwndDlg, cmd - IDM_BOOKMARK_FIRST);
            }
            else if (cmd >= IDM_BOOKMARK_REMOVE_FIRST && cmd < IDM_BOOKMARK_REMOVE_FIRST + Bookmarks_Count())
            {
              Bookmarks_Remove(cmd - IDM_BOOKMARK_REMOVE_FIRST);
              UpdateDimBoxes(hwndDlg);
            }
            else if (cmd >= IDM_BOOKMARK_UP_FIRST && cmd < IDM_BOOKMARK_UP_FIRST + Bookmarks_Count())
            {
              Bookmarks_Move(cmd - IDM_BOOKMARK_UP_FIRST, -1);
              UpdateDimBoxes(hwndDlg);
            }
            else if (cmd >= IDM_BOOKMARK_DOWN_FIRST && cmd < IDM_BOOKMARK_DOWN_FIRST + Bookmarks_Count())
            {
              Bookmarks_Move(cmd - IDM_BOOKMARK_DOWN_FIRST, 1);
              UpdateDimBoxes(hwndDlg);
            }
          }
        break;
      }
    break;
    case WM_CLOSE:
      if (Busy_Active()) return 1;
      BookmarkPanel_Close();
      if (!g_cap_state)
      {
        if (g_pending_unsaved)
        {
          int r = MessageBoxA(hwndDlg,
            "There is a recording that has not been saved yet.\n\nSave it now?",
            "LICEcap",MB_YESNOCANCEL|MB_ICONQUESTION);
          if (r == IDCANCEL) break;
          if (r == IDYES)
          {
            DoSaveAs(hwndDlg);
            if (g_pending_unsaved) break;
          }
          else
          {
            DiscardPending();
          }
        }

        #if defined(_WIN32) || defined(REAPER_LICECAP)
                EndDialog(hwndDlg,0);
        #else
                DestroyWindow(hwndDlg);
        #endif
      }
      else
      {
        int r = MessageBoxA(hwndDlg,
          "A capture is in progress.\n\nStop it and continue?",
          "LICEcap",MB_YESNO|MB_ICONQUESTION);
        if (r == IDYES)
        {
          SendMessage(hwndDlg,WM_COMMAND,IDC_STOP,0);
          PostMessage(hwndDlg,WM_CLOSE,0,0);
        }
      }
    break;
    case WM_GETMINMAXINFO:
      if (lParam)
      {
        LPMINMAXINFO l = (LPMINMAXINFO)lParam;
        l->ptMinTrackSize.x = UI(MIN_SIZE_X) + UI(UI_BTN_COL_W) + UI(8);
        l->ptMinTrackSize.y = UI(150) + UI_CaptionHeight();
      }
    break;
#ifndef _WIN32
    // win32 uses WM_NCHITTEST
    case WM_LBUTTONDOWN:
      SetCapture(hwndDlg);
      GetCursorPos(&s_last_mouse);
    break;
    case WM_MOUSEMOVE:
      if (GetCapture()==hwndDlg)
      {
        POINT p;
        GetCursorPos(&p);
        int dx= p.x - s_last_mouse.x;
        int dy= p.y - s_last_mouse.y;
        if (dx||dy)
        {
          s_last_mouse=p;
          RECT r;
          GetWindowRect(hwndDlg,&r);
          SetWindowPos(hwndDlg,NULL,r.left+dx,r.top+dy,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
      }
    break;
    case WM_LBUTTONUP:
      ReleaseCapture();
    break;
#endif
    case WM_MOVE:
    break;
    case WM_SIZE:

      if (wParam != SIZE_MINIMIZED)
      {
        LayoutControls(hwndDlg);
#ifdef _WIN32
        ApplyWindowRegion(hwndDlg);
#endif
        UpdateDimBoxes(hwndDlg);

        InvalidateRect(hwndDlg,NULL,TRUE);

      }
    break;
#if _WIN32
    // The editor strip and its preview are client areas, not the draggable
    // custom caption the rest of the window uses.
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
      if (Editor_Active() &&
          Editor_Mouse(hwndDlg, WM_LBUTTONDOWN, (short)LOWORD(lParam), (short)HIWORD(lParam)))
      {
        SetFocus(hwndDlg);
        // Close/Apply can leave edit mode inside Editor_Mouse. Do not capture
        // the mouse afterwards: there would be no active editor to release it.
        if (Editor_Active()) SetCapture(hwndDlg);
        return 1;
      }
    break;

    case WM_MOUSEMOVE:
      if (Editor_Active() &&
          Editor_Mouse(hwndDlg, WM_MOUSEMOVE, (short)LOWORD(lParam), (short)HIWORD(lParam)))
        return 1;
    break;

    case WM_LBUTTONUP:
      if (GetCapture() == hwndDlg) ReleaseCapture();
      if (Editor_Active())
      {
        const bool handled = Editor_Mouse(hwndDlg, WM_LBUTTONUP,
                                          (short)LOWORD(lParam), (short)HIWORD(lParam));
        if (handled) return 1;
      }
    break;

    case WM_SETCURSOR:
      if (Busy_Active())
      {
        SetCursor(LoadCursor(NULL, IDC_WAIT));
        return 1;
      }
      if (Editor_Active() && LOWORD(lParam) == HTCLIENT)
      {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hwndDlg, &pt);
        const int cur = Editor_Cursor(hwndDlg, pt.x, pt.y);
        if (cur != EDITOR_CUR_NONE)
        {
          SetCursor(LoadCursor(NULL, cur == EDITOR_CUR_HANDLE ? IDC_SIZEWE : IDC_HAND));
          return 1;
        }
      }
    break;

    case WM_NCHITTEST: /* allow dragging the dialog without mouse capture */
        {
            POINT pt;
            RECT rc;
            GetCursorPos(&pt);
            GetClientRect(hwndDlg, &rc);
            ScreenToClient(hwndDlg, &pt);
            if (PtInRect(&rc, pt))
            {
                if (Editor_Active() && Editor_HitTest(hwndDlg, pt.x, pt.y))
                {
                    SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, HTCLIENT);
                    return 1;
                }
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, HTCAPTION);
                return 1;
            }
        }
        break;
#endif
  }
  return 0;
}



#ifndef REAPER_LICECAP

#ifdef _WIN32

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nShowCmd)
{
  bool want_appdata = true;
  g_ini_file.Set("licecap.ini");

  // if exepath\licecap.ini is present, use it
  {
    char exepath[2048];
    exepath[0]=0;
    GetModuleFileName(NULL,exepath,sizeof(exepath));
    char *p=exepath;
    while (*p) p++;
    while (p > exepath && *p != '\\') p--; *p=0;

    if (exepath[0])
    {
      g_ini_file.Set(exepath);
      g_ini_file.Append("\\licecap.ini");
      FILE *fp=fopen(g_ini_file.Get(),"r");
      if (fp) 
      {
        fclose(fp);
        want_appdata = false;
      }
    }
  }

  // use appdata/licecap.ini
  if (want_appdata)
  {
    HKEY k;
    if (RegOpenKeyEx(HKEY_CURRENT_USER,"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",0,KEY_READ,&k) == ERROR_SUCCESS)
    {
      char buf[1024];
      DWORD b=sizeof(buf);
      DWORD t=REG_SZ;
      if (RegQueryValueEx(k,"AppData",0,&t,(unsigned char *)buf,&b) == ERROR_SUCCESS && t == REG_SZ)
      {
        g_ini_file.Set(buf);
        g_ini_file.Append("\\licecap.ini");
      }
      RegCloseKey(k);
    }
  }


  GetPrivateProfileString("licecap","lastfn","",g_last_fn,sizeof(g_last_fn),g_ini_file.Get());
  g_dark_ui = GetPrivateProfileInt("licecap", "dark", 1, g_ini_file.Get()) != 0;
  UI_ApplyAppMode();

  // "licecap.exe D:\path\anim.gif" opens that GIF in the timeline editor, the
  // same thing the Edit button offers.
  if (lpCmdLine)
  {
    char arg[2048];
    int n = 0;
    const char *p = lpCmdLine;
    while (*p == ' ' || *p == '\t') p++;
    const char quote = (*p == '"') ? '"' : 0;
    if (quote) p++;
    while (*p && n < (int)sizeof(arg)-1 && (quote ? *p != quote : (*p != ' ' && *p != '\t')))
      arg[n++] = *p++;
    arg[n] = 0;
    if (n > 0)
    {
      char full[2048];
      if (GetFullPathNameA(arg,sizeof(full),full,NULL) && full[0])
        lstrcpyn_safe(g_startup_gif,full,sizeof(g_startup_gif));
      else
        lstrcpyn_safe(g_startup_gif,arg,sizeof(g_startup_gif));
    }
  }

  g_hInst = hInstance;
  DialogBox(hInstance,MAKEINTRESOURCE(IDD_DIALOG1),GetDesktopWindow(),liceCapMainProc);
  return 0;
}

#else
INT_PTR SWELLAppMain(int msg, INT_PTR parm1, INT_PTR parm2)
{
  switch (msg)
  {
    case SWELLAPP_ONLOAD:
      {
        char exepath[2048];
        exepath[0]=0;
        GetModuleFileName(NULL,exepath,sizeof(exepath));
        char *p=exepath;
        while (*p) p++;
        while (p > exepath && *p != '/') p--; 
        *p=0;
      
        g_ini_file.Set(exepath);
        g_ini_file.Append("/licecap.ini");
        FILE *fp = fopen(g_ini_file.Get(),"r");
        if (fp) 
        {
          fclose(fp);
        }
        else
        {
          char *p=getenv("HOME");
          if (p && *p)
          {
            g_ini_file.Set(p);
            g_ini_file.Append("/Library/Application Support/LICEcap");
            mkdir(g_ini_file.Get(),0777);
            
            g_ini_file.Append("/licecap.ini");
            fp=fopen(g_ini_file.Get(),"a");
            if (fp)
            {
              fclose(fp);
            }
            else
            {
              g_ini_file.Set(exepath);
              g_ini_file.Append("/licecap.ini");
            }
          }
        }
        GetPrivateProfileString("licecap","lastfn","",g_last_fn,sizeof(g_last_fn),g_ini_file.Get());
      }
    break;
    case SWELLAPP_LOADED:
    
      CreateDialog(hInstance,MAKEINTRESOURCE(IDD_DIALOG1),NULL,liceCapMainProc);
      ShowWindow(g_hwnd,SW_SHOW);
    break;
    case SWELLAPP_ONCOMMAND:
      if (g_hwnd) SendMessage(g_hwnd,WM_COMMAND,parm1&0xffff,0);
      break;
    case SWELLAPP_DESTROY:
      if (g_hwnd && !g_cap_state) DestroyWindow(g_hwnd);
      break;
  //  case SWELLAPP_PROCESSMESSAGE:
  //    if (MainProcessMessage((MSG*)parm1)>0) return 1;
  //    return 0;
      
  }
  return 0;
}

#endif

// end of standalone
#endif //!REAPER_LICECAP

#ifdef REAPER_LICECAP

// reaper plugin

static bool (*__WDL_ChooseFileForSave)(HWND parent, const char *text, const char *initialdir, const char *initialfile, const char *extlist, const char *defext, bool preservecwd, char *fn, int fnsize, const char *dlgid, void *dlgProc, void *hi);
static void *(*__LICE_WriteGIFBeginNoFrame)(const char *filename, int w, int h, int transparent_alpha, bool dither, bool is_append);
static bool (*__LICE_WriteGIFFrame)(void *handle, LICE_IBitmap *frame, int xpos, int ypos, bool perImageColorMap, int frame_delay, int nreps);
static bool (*__LICE_WriteGIFEnd)(void *handle);


unsigned WINAPI audioEncodeThread(void *p)
{
  WDL_TypedBuf<ReaSample> tmp;
  while (s_audiohook_reg)
  {
    int ns=0;
    if (s_audiohook_samples.Available())
    {
      s_audiohook_samples_mutex.Enter();
      ns = s_audiohook_samples.Available();      
      if (tmp.Resize(ns) && tmp.GetSize()==ns)
      {
        memcpy(tmp.Get(),s_audiohook_samples.Get(),ns*sizeof(ReaSample));
        s_audiohook_samples.Advance(ns);
        s_audiohook_samples.Compact();
      }
      else
      {
        ns=0;
      }
      s_audiohook_samples_mutex.Leave();
    }
    if (!ns) Sleep(1);
    else
    {
      // encode block
      if (g_cap_video)
      {
        ReaSample *s[2] = { tmp.Get(), tmp.Get()+1};
        g_cap_video->encodeAudio(s,ns/2,2,0,2);
      }
    }
  }
  return 0;
}


void OnAudioBuffer(bool isPost, int len, double srate, struct audio_hook_register_t *reg) // called twice per frame, isPost being false then true
{
  if (isPost)
  {
    ReaSample *s1 = reg->GetBuffer(true,0);
    ReaSample *s2 = reg->GetBuffer(true,1);
    if (!s2) s2=s1;
    if (s1)
    {
      s_audiohook_samples_mutex.Enter();
      if (s_audiohook_needsilence>0.0)
      {
        int n = (int) (s_audiohook_needsilence * srate + 0.5);
        if (n>0)
        {
          ReaSample *fo = s_audiohook_samples.Add(NULL,n*2);
          if (fo) memset(fo,0,sizeof(*fo)*n*2);
        }
        s_audiohook_needsilence=0.0;
      }

      if (g_cap_state == 1 && !g_cap_prerolluntil && s_audiohook_samples.GetSize() < 44100*2 * 4)
      {
        s_audiohook_timepos += len/srate;

        if (!s_rs)
          s_rs=Resampler_Create();

        if (s_rs)
        {
          s_rs->SetRates(srate,44100.0);
          s_rs->Extended(RESAMPLE_EXT_SETFEEDMODE,(void*)(INT_PTR)1,0,0);
          ReaSample *bptr=NULL;
          int a=s_rs->ResamplePrepare(len,2,&bptr);
          if (a>len) a=len;
          if (bptr && a>0)
          {
            int x;
            for(x=0;x<a;x++)
            {
              *bptr++ = *s1++;
              *bptr++ = *s2++;
            }

            ReaSample *fo = s_audiohook_samples.Add(NULL,2*len);
            if (fo)
            {
              int b=s_rs->ResampleOut(fo,a,len,2);
              if (b < len)
                s_audiohook_samples.Add(NULL,2*(b - len)); // back up
            }
          }
        }
      }
      s_audiohook_samples_mutex.Leave();
    }
  }
}

void *LICE_WriteGIFBeginNoFrame(const char *filename, int w, int h, int transparent_alpha, bool dither, bool is_append)
{
  if (!__LICE_WriteGIFBeginNoFrame || !__LICE_WriteGIFFrame || !__LICE_WriteGIFEnd)
  {
    *(void **)&__LICE_WriteGIFBeginNoFrame = reaperAPI_getfunc("LICE_WriteGIFBeginNoFrame2");
    *(void **)&__LICE_WriteGIFFrame = reaperAPI_getfunc("LICE_WriteGIFFrame");
    *(void **)&__LICE_WriteGIFEnd = reaperAPI_getfunc("LICE_WriteGIFEnd");
    if (!__LICE_WriteGIFBeginNoFrame || !__LICE_WriteGIFFrame || !__LICE_WriteGIFEnd)
      return NULL;
  }
  return __LICE_WriteGIFBeginNoFrame(filename,w,h,transparent_alpha,dither,is_append);
}

bool LICE_WriteGIFFrame(void *handle, LICE_IBitmap *frame, int xpos, int ypos, bool perImageColorMap, int frame_delay, int nreps)
{
  return __LICE_WriteGIFFrame(handle,frame,xpos,ypos,perImageColorMap,frame_delay,nreps);
}

bool LICE_WriteGIFEnd(void *handle)
{
  return __LICE_WriteGIFEnd(handle);
}


bool WDL_ChooseFileForSave(HWND parent, const char *text, const char *initialdir, const char *initialfile, const char *extlist, const char *defext, bool preservecwd, char *fn, int fnsize, const char *dlgid, void *dlgProc,  void *hi)
{
  if (__WDL_ChooseFileForSave)
    return __WDL_ChooseFileForSave(parent,text,initialdir,initialfile,extlist,defext,preservecwd,fn,fnsize,dlgid,dlgProc,hi);
  return false;
}

int g_command_id;

static unsigned WINAPI uiThread(void *p)
{
  DialogBox(g_hInst,MAKEINTRESOURCE(IDD_DIALOG1),GetDesktopWindow(),liceCapMainProc);
  return 0;
}

static bool hookCommandProc(int command, int flag)
{
  if (g_command_id && command == g_command_id)
  {
    if (!g_hwnd)
    {
      unsigned id;
      HANDLE th=(HANDLE)_beginthreadex(NULL,0,uiThread,NULL,0,&id);
      CloseHandle(th);
      Sleep(100);
    }
    else
      SetForegroundWindow(g_hwnd);
    return true;
  }
  return false;
}


extern "C"
{

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(REAPER_PLUGIN_HINSTANCE hInstance, reaper_plugin_info_t *rec)
{
  g_hInst=hInstance;
  if (rec)
  {
    if (rec->caller_version != REAPER_PLUGIN_VERSION || !rec->GetFunc)
      return 0;

    IMPORT_LICE_RPLUG(rec);

    const char *(*get_ini_file)();    
    *(void **)&get_ini_file = rec->GetFunc("get_ini_file");
    *(void **)&Audio_RegHardwareHook = rec->GetFunc("Audio_RegHardwareHook");
    *(void **)&__WDL_ChooseFileForSave = rec->GetFunc("WDL_ChooseFileForSave");
    *(void **)&Resampler_Create = rec->GetFunc("Resampler_Create");
    
    
    if (!get_ini_file || !Audio_RegHardwareHook || !Resampler_Create || !__WDL_ChooseFileForSave || !VERIFY_LICE_IMPORTED() || !__LICE_MeasureText || !__LICE_DrawText) return 0;

    reaperAPI_getfunc = rec->GetFunc;
    g_ini_file.Set(get_ini_file());
    GetPrivateProfileString("licecap","lastfn","",g_last_fn,sizeof(g_last_fn),g_ini_file.Get());

    g_command_id = rec->Register("command_id","reaper_licecap_window");
    if (g_command_id)
    {
      static gaccel_register_t acc;
      acc.accel.cmd = g_command_id;
      acc.desc = "Show REAPER_LICEcap window";
      rec->Register("gaccel",&acc);
      rec->Register("hookcommand",(void*)hookCommandProc);
    }

    return 1;
  }
  else
  {
    //unload
    if(g_hwnd)
    {
      PostMessage(g_hwnd, WM_CLOSE, 0, 0);
      DWORD t = GetTickCount();
      while(g_hwnd && (GetTickCount()-t)<5000)
        Sleep(100);
    }

  }
  return 0;
}


};


#endif //REAPER_LICECAP







#ifndef _WIN32

#include "../WDL/swell/swell-dlggen.h"
#include "licecap.rc_mac_dlg"

#endif






