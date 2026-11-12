/*
    ui_presets.cpp - social-media capture sizes and the Settings dialog.

    Split out of licecap_ui.cpp (fork-only GifCam-style UI), code unchanged.
*/

#include "licecap_ui.h"

// Preset divisor: the "/" key (or the "Half Size" menu entry) picks the same
// presets at half dimensions, which is handy for running the GUI on a scaled
// desktop and still emitting exactly 1080-ish pixels per side.
int g_preset_div = 1;
int g_half_base_w, g_half_base_h;
#include "../WDL/wdlcstring.h"
// Social-media capture sizes. Selecting one resizes the capture area exactly.
struct LicePreset
{
  const char *name;
  const char *dims;
  int w, h;
};

static const LicePreset s_presets[] =
{
  { "Instagram post",        "1080 x 1080", 1080, 1080 },
  { "Instagram portrait",    "1080 x 1350", 1080, 1350 },
  { "Instagram / TikTok",    "1080 x 1920", 1080, 1920 },
  { NULL, NULL, 0, 0 },
  { "X / Twitter post",      "1200 x 675",  1200,  675 },
  { "X / YouTube HD",        "1280 x 720",  1280,  720 },
  { "YouTube / web",         "1920 x 1080", 1920, 1080 },
  { NULL, NULL, 0, 0 },
  { "Facebook / LinkedIn",   "1200 x 630",  1200,  630 },
  { "LinkedIn post",         "1200 x 627",  1200,  627 },
  { NULL, NULL, 0, 0 },
  { "Discord / Slack emoji", "128 x 128",   128,   128 },
  { "Sticker / avatar",      "320 x 320",   320,   320 },
  { NULL, NULL, 0, 0 },
  { "Classic screen",        "640 x 480",   640,   480 },
  { "Classic screen",        "800 x 600",   800,   600 },
  { NULL, NULL, 0, 0 },
  { "Square",                "1024 x 1024", 1024, 1024 },
};

#define PRESET_COUNT ((int)(sizeof(s_presets)/sizeof(s_presets[0])))

// Number of entries, separators included (the menu walks the same table).
int PresetCount(void)
{
  return PRESET_COUNT;
}

const char *PresetNameFor(int w, int h)
{
  int i;
  for (i = 0; i < PRESET_COUNT; i++)
    if (s_presets[i].name && s_presets[i].w == w && s_presets[i].h == h)
      return s_presets[i].name;
  return NULL;
}

void ResizeCaptureArea(int cw, int ch)
{
  if (!g_hwnd || cw < MIN_SIZE_X || ch < MIN_SIZE_Y) return;
  int oldw=0, oldh=0;
  GetViewRectSize(&oldw,&oldh);
  RECT r;
  GetWindowRect(g_hwnd,&r);
  int w = (r.right-r.left) + (cw-oldw);
  int h = (r.bottom-r.top) + (ch-oldh);

  // keep the window anchored to the screen when a large preset is chosen
  RECT scr;
  my_getViewport(&scr,&r,true);
  int x = r.left, y = r.top;
  if (x + w > scr.right) x = scr.right - w;
  if (y + h > scr.bottom) y = scr.bottom - h;
  if (x < scr.left) x = scr.left;
  if (y < scr.top) y = scr.top;

  SetWindowPos(g_hwnd,NULL,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
}

// Switches between full and half preset sizes. The full size is remembered so
// turning the mode off puts the capture area back where it was.
void SetPresetDiv(HWND hwndDlg, int div)
{
  if (div != 2) div = 1;
  if (div == g_preset_div) return;

  int cw = 0, ch = 0;
  GetViewRectSize(&cw,&ch);

  if (div == 2)
  {
    g_half_base_w = cw;
    g_half_base_h = ch;
  }

  int nw = cw, nh = ch;
  if (div == 2)
  {
    nw = cw/2;
    nh = ch/2;
    if (nw < MIN_SIZE_X) nw = MIN_SIZE_X;
    if (nh < MIN_SIZE_Y) nh = MIN_SIZE_Y;
  }
  else
  {
    if (g_half_base_w >= MIN_SIZE_X) nw = g_half_base_w;
    if (g_half_base_h >= MIN_SIZE_Y) nh = g_half_base_h;
    g_half_base_w = g_half_base_h = 0;
  }

  g_preset_div = div;
  ResizeCaptureArea(nw,nh);
  UpdateDimBoxes(hwndDlg);
}

void ApplyPreset(HWND hwndDlg, int idx)
{
  if (idx < 0 || idx >= PRESET_COUNT) return;
  const LicePreset *p = &s_presets[idx];
  if (!p->name || g_cap_state) return;

  if (g_preset_div == 2)
  {
    const int w = p->w/2, h = p->h/2;
    if (w < MIN_SIZE_X || h < MIN_SIZE_Y) return;
    g_half_base_w = p->w;
    g_half_base_h = p->h;
    ResizeCaptureArea(w,h);
  }
  else
  {
    g_half_base_w = g_half_base_h = 0;
    ResizeCaptureArea(p->w,p->h);
  }
  UpdateDimBoxes(hwndDlg);
}

// ---------------------------------------------------------------------------
// Settings dialog (IDD_CUSTOMIZATION, shown by the Settings button)
// ---------------------------------------------------------------------------

void CustomizeInit(HWND d)
{
  CheckRadioButton(d,IDC_FPS10,IDC_FPSCUSTOM,
                   g_max_fps==10?IDC_FPS10 : g_max_fps==16?IDC_FPS16 : g_max_fps==30?IDC_FPS30 : IDC_FPSCUSTOM);
  SetDlgItemInt(d,IDC_MAXFPS2,g_max_fps,FALSE);
  EnableWindow(GetDlgItem(d,IDC_MAXFPS2), g_max_fps!=10 && g_max_fps!=16 && g_max_fps!=30);

  CheckDlgButton(d,IDC_CAP_CURSOR,g_capture_cursor?BST_CHECKED:BST_UNCHECKED);
  CheckDlgButton(d,IDC_CAP_CLICKS,(g_prefs&4)?BST_CHECKED:BST_UNCHECKED);
  CheckDlgButton(d,IDC_CAP_TIME,(g_prefs&8)?BST_CHECKED:BST_UNCHECKED);
  CheckDlgButton(d,IDC_CAP_STOPAFTER,(g_prefs&64)?BST_CHECKED:BST_UNCHECKED);

  char buf[256];
  snprintf(buf,sizeof(buf),"%.1f",(double)g_stop_after_msec/1000.0);
  SetDlgItemTextA(d,IDC_STOPAFTER2,buf);
  EnableWindow(GetDlgItem(d,IDC_STOPAFTER2),(g_prefs&64)!=0);

  SetDlgItemInt(d,IDC_LOOPCNT2,g_gif_loopcount,FALSE);
  CheckDlgButton(d,IDC_TRANSPARENT2,(g_prefs&32)?BST_CHECKED:BST_UNCHECKED);
  CheckDlgButton(d,IDC_TITLEUSE2,(g_prefs&1)?BST_CHECKED:BST_UNCHECKED);
  CheckDlgButton(d,IDC_BIGFONT2,(g_prefs&2)?BST_CHECKED:BST_UNCHECKED);

  snprintf(buf,sizeof(buf),"%.1f",(double)g_titlems/1000.0);
  SetDlgItemTextA(d,IDC_TITLEMS2,buf);
  SetDlgItemTextA(d,IDC_TITLE2,g_title[0]?g_title:"Title");
  EnableWindow(GetDlgItem(d,IDC_TITLE2),(g_prefs&1)!=0);
  EnableWindow(GetDlgItem(d,IDC_TITLEMS2),(g_prefs&1)!=0);
  EnableWindow(GetDlgItem(d,IDC_BIGFONT2),(g_prefs&1)!=0);

  int cw=0, ch=0;
  GetViewRectSize(&cw,&ch);
  SetDlgItemInt(d,IDC_WINSIZE_W,cw,FALSE);
  SetDlgItemInt(d,IDC_WINSIZE_H,ch,FALSE);

  CheckDlgButton(d,IDC_SET_DARK,g_dark_ui?BST_CHECKED:BST_UNCHECKED);
  CheckDlgButton(d,IDC_SET_TOPMOST,g_always_on_top?BST_CHECKED:BST_UNCHECKED);
}

void CustomizeDefaults(HWND d)
{
  CheckRadioButton(d,IDC_FPS10,IDC_FPSCUSTOM,IDC_FPS10);
  SetDlgItemInt(d,IDC_MAXFPS2,10,FALSE);
  EnableWindow(GetDlgItem(d,IDC_MAXFPS2),FALSE);
  CheckDlgButton(d,IDC_CAP_CURSOR,BST_CHECKED);
  CheckDlgButton(d,IDC_CAP_CLICKS,BST_CHECKED);
  CheckDlgButton(d,IDC_CAP_TIME,BST_UNCHECKED);
  CheckDlgButton(d,IDC_CAP_STOPAFTER,BST_UNCHECKED);
  SetDlgItemTextA(d,IDC_STOPAFTER2,"10.0");
  EnableWindow(GetDlgItem(d,IDC_STOPAFTER2),FALSE);
  SetDlgItemInt(d,IDC_LOOPCNT2,0,FALSE);
  CheckDlgButton(d,IDC_TRANSPARENT2,BST_CHECKED);
  CheckDlgButton(d,IDC_TITLEUSE2,BST_UNCHECKED);
  CheckDlgButton(d,IDC_BIGFONT2,BST_UNCHECKED);
  SetDlgItemTextA(d,IDC_TITLEMS2,"1.8");
  SetDlgItemTextA(d,IDC_TITLE2,"Title");
  EnableWindow(GetDlgItem(d,IDC_TITLE2),FALSE);
  EnableWindow(GetDlgItem(d,IDC_TITLEMS2),FALSE);
  EnableWindow(GetDlgItem(d,IDC_BIGFONT2),FALSE);
  SetDlgItemInt(d,IDC_WINSIZE_W,512,FALSE);
  SetDlgItemInt(d,IDC_WINSIZE_H,512,FALSE);
  CheckDlgButton(d,IDC_SET_DARK,BST_CHECKED);
  CheckDlgButton(d,IDC_SET_TOPMOST,BST_UNCHECKED);
}

void CustomizeApply(HWND d)
{
  if (IsDlgButtonChecked(d,IDC_FPS10)) g_max_fps = 10;
  else if (IsDlgButtonChecked(d,IDC_FPS16)) g_max_fps = 16;
  else if (IsDlgButtonChecked(d,IDC_FPS30)) g_max_fps = 30;
  else
  {
    BOOL t=FALSE;
    int a=GetDlgItemInt(d,IDC_MAXFPS2,&t,FALSE);
    if (t && a>=1 && a<=60) g_max_fps=a;
  }

  g_capture_cursor = IsDlgButtonChecked(d,IDC_CAP_CURSOR)!=0;
  g_prefs = (g_prefs & ~(1|2|4|8|32|64))
          | (IsDlgButtonChecked(d,IDC_CAP_CLICKS)?4:0)
          | (IsDlgButtonChecked(d,IDC_CAP_TIME)?8:0)
          | (IsDlgButtonChecked(d,IDC_TRANSPARENT2)?32:0)
          | (IsDlgButtonChecked(d,IDC_CAP_STOPAFTER)?64:0)
          | (IsDlgButtonChecked(d,IDC_TITLEUSE2)?1:0)
          | (IsDlgButtonChecked(d,IDC_BIGFONT2)?2:0);

  char buf[256];
  GetDlgItemTextA(d,IDC_STOPAFTER2,buf,sizeof(buf));
  if (buf[0]) g_stop_after_msec = (int)(atof(buf)*1000.0);
  GetDlgItemTextA(d,IDC_TITLEMS2,buf,sizeof(buf));
  if (buf[0]) g_titlems = (int)(atof(buf)*1000.0);
  GetDlgItemTextA(d,IDC_TITLE2,g_title,sizeof(g_title));
  if (!strcmp(g_title,"Title")) g_title[0]=0;

  BOOL t=FALSE;
  int a=GetDlgItemInt(d,IDC_LOOPCNT2,&t,FALSE);
  if (t) g_gif_loopcount=(a>0&&a<65536)?a:0;

  int cw = (int)GetDlgItemInt(d,IDC_WINSIZE_W,&t,FALSE);
  if (!t) cw = 0;
  int ch = (int)GetDlgItemInt(d,IDC_WINSIZE_H,&t,FALSE);
  if (!t) ch = 0;
  if (cw>=MIN_SIZE_X && ch>=MIN_SIZE_Y) ResizeCaptureArea(cw,ch);

  // Appearance: the same live switches the Settings menu offers, but here they
  // stay in one dialog so nothing is configured in two different places.
  const bool dark = IsDlgButtonChecked(d,IDC_SET_DARK)!=0;
  if (dark != g_dark_ui)
  {
    UI_SetDarkMode(g_hwnd, dark);
    UI_SetDarkTitleBar(d);
    RedrawWindow(d,NULL,NULL,RDW_INVALIDATE|RDW_ERASE|RDW_ALLCHILDREN);
  }

  g_always_on_top = IsDlgButtonChecked(d,IDC_SET_TOPMOST)!=0;
  SetWindowPos(g_hwnd,g_always_on_top?HWND_TOPMOST:HWND_NOTOPMOST,0,0,0,0,
               SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

  UpdateDimBoxes(g_hwnd);
}

WDL_DLGRET CustomizeProc(HWND hwndDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
  switch (msg)
  {
    case WM_INITDIALOG:
      UI_ThemeDialog(hwndDlg);
      CustomizeInit(hwndDlg);
      return 1;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_FPS10: case IDC_FPS16: case IDC_FPS30: case IDC_FPSCUSTOM:
          EnableWindow(GetDlgItem(hwndDlg,IDC_MAXFPS2),
                       IsDlgButtonChecked(hwndDlg,IDC_FPSCUSTOM)!=0);
          return 1;

        case IDC_TITLEUSE2:
        {
          bool use = IsDlgButtonChecked(hwndDlg,IDC_TITLEUSE2)!=0;
          EnableWindow(GetDlgItem(hwndDlg,IDC_TITLE2),use);
          EnableWindow(GetDlgItem(hwndDlg,IDC_TITLEMS2),use);
          EnableWindow(GetDlgItem(hwndDlg,IDC_BIGFONT2),use);
          if (use) SetFocus(GetDlgItem(hwndDlg,IDC_TITLE2));
        }
        return 1;

        case IDC_CAP_STOPAFTER:
          EnableWindow(GetDlgItem(hwndDlg,IDC_STOPAFTER2),
                       IsDlgButtonChecked(hwndDlg,IDC_CAP_STOPAFTER)!=0);
          return 1;

        case IDC_DEFAULTS:
          CustomizeDefaults(hwndDlg);
          return 1;

        case IDOK:
          CustomizeApply(hwndDlg);
          EndDialog(hwndDlg,1);
          return 1;

        case IDCANCEL:
          EndDialog(hwndDlg,0);
          return 1;
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
      return 0;

    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
      LRESULT res;
      if (UI_DialogCtlColor(msg,wParam,&res)) return (WDL_DLGRET)res;
    }
    return 0;
#endif
  }
  return 0;
}

void ShowCustomization(HWND hwndDlg)
{
  DialogBox(g_hInst,MAKEINTRESOURCE(IDD_CUSTOMIZATION),hwndDlg,CustomizeProc);
}

void SetFullScreen(HWND hwndDlg)
{
  RECT wr, scr;
  GetWindowRect(hwndDlg,&wr);
  my_getViewport(&scr,&wr,false);
  SetWindowPos(hwndDlg,NULL,scr.left,scr.top,scr.right-scr.left,scr.bottom-scr.top,
               SWP_NOZORDER|SWP_NOACTIVATE);
}

void SetDefaultWindowSize(HWND hwndDlg)
{
  RECT wr;
  GetWindowRect(hwndDlg,&wr);
  SetWindowPos(hwndDlg,NULL,wr.left,wr.top,UI(512),UI(512),SWP_NOZORDER|SWP_NOACTIVATE);
}

void ShowPresetMenu(HWND hwndDlg)
{
  HMENU m = CreatePopupMenu();
  if (!m) return;

  HWND b = GetDlgItem(hwndDlg,IDC_PRESETS);
  RECT r;
  if (!b || !GetWindowRect(b,&r))
  {
    DestroyMenu(m);
    return;
  }

  int cw = 0, ch = 0;
  GetViewRectSize(&cw,&ch);

  const int div = g_preset_div == 2 ? 2 : 1;

  int i;
  for (i = 0; i < PRESET_COUNT; i++)
  {
    const LicePreset *p = &s_presets[i];
    if (!p->name)
    {
      AppendMenuA(m,MF_SEPARATOR,0,NULL);
      continue;
    }
    UINT flags = MF_STRING;
    if (g_cap_state) flags |= MF_GRAYED;
    // A half size below the minimum capture area cannot be honoured.
    if (div == 2 && (p->w/2 < MIN_SIZE_X || p->h/2 < MIN_SIZE_Y)) flags |= MF_GRAYED;
    if (p->w/div == cw && p->h/div == ch) flags |= MF_CHECKED;
    char text[160];
    if (div == 2) snprintf(text,sizeof(text),"%s\t%d x %d",p->name,p->w/2,p->h/2);
    else snprintf(text,sizeof(text),"%s\t%s",p->name,p->dims);
    AppendMenuA(m,flags,IDM_PRESET_FIRST+i,text);
  }

  AppendMenuA(m,MF_SEPARATOR,0,NULL);
  AppendMenuA(m,MF_STRING|(g_cap_state?MF_GRAYED:0)|(div==2?MF_CHECKED:0),IDM_HALF_SIZE,
              "Half Size (/)\t/");
  AppendMenuA(m,MF_STRING|(g_cap_state?MF_GRAYED:0),IDM_CUSTOMIZE,"Settings...\tCtrl+U");
  AppendMenuA(m,MF_STRING|(g_cap_state?MF_GRAYED:0),IDM_FULLSCREEN,"Full Screen");

  SetForegroundWindow(hwndDlg);
  int cmd = (int)TrackPopupMenuEx(m, TPM_LEFTALIGN|TPM_TOPALIGN|TPM_RETURNCMD|TPM_RIGHTBUTTON,
                                  r.left, r.bottom + UI(2), hwndDlg, NULL);
  DestroyMenu(m);
  PostMessage(hwndDlg,WM_NULL,0,0);
  if (cmd) SendMessage(hwndDlg,WM_COMMAND,(WPARAM)cmd,0);
}
