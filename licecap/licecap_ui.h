/*
    LICEcap - GifCam-style Windows UI (fork-only code)

    Shared interface between licecap_ui.cpp (application, capture engine wiring,
    main window procedure) and the ui_*.cpp modules:

      ui_theme.cpp      palette, DPI scaling, dark mode, dialog theming
      ui_draw.cpp       main window geometry, region and painting
      ui_bookmarks.cpp  output-folder bookmarks (model + popup panel)
      ui_presets.cpp    capture size presets and the Settings dialog

    Everything declared here used to live inside a single 5000-line translation
    unit; the modules only give that code its own files. Behaviour is unchanged.
*/

#ifndef LICECAP_UI_H
#define LICECAP_UI_H

#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#else
#include "../WDL/swell/swell.h"
#endif

#include "../WDL/wdlstring.h"
#include "resource.h"

// ---------------------------------------------------------------------------
// geometry shared by the layout, the size presets and the capture rect
// ---------------------------------------------------------------------------
#define MIN_SIZE_X 96
#define MIN_SIZE_Y 96

// side toolbar metrics; every value is DPI-scaled through UI()
#define UI_BTN_COL_W 88
#define UI_PAD       6
#define UI_BTN_GAP   6
#define UI_BTN_H     26
#define UI_CAPTION_H 26

// buffer size for a bookmark path, used by the menu and the picker callers
#define BOOKMARK_PATH_LEN 1024

// ---------------------------------------------------------------------------
// palette: two sets of colours with the same field names, so the paint code
// only knows the COL_* macros and switching theme is swapping g_col
// ---------------------------------------------------------------------------
struct LiceUIColors
{
  COLORREF bg, btn, btn_hot, btn_down, btn_dis;
  COLORREF border, border_hot, accent;
  COLORREF text, text_dis, text_hot;
  COLORREF sep;
  COLORREF rec, rec_bg, rec_border, rec_hot;
  COLORREF split_hot;
  COLORREF edit_bg, edit_text;
};

extern LiceUIColors g_col;

#define COL_BG          g_col.bg
#define COL_BTN         g_col.btn
#define COL_BTN_HOT     g_col.btn_hot
#define COL_BTN_DOWN    g_col.btn_down
#define COL_BTN_DIS     g_col.btn_dis
#define COL_BORDER      g_col.border
#define COL_BORDER_HOT  g_col.border_hot
#define COL_ACCENT      g_col.accent
#define COL_TEXT        g_col.text
#define COL_TEXT_DIS    g_col.text_dis
#define COL_TEXT_HOT    g_col.text_hot
#define COL_SEP         g_col.sep
#define COL_REC         g_col.rec
#define COL_REC_BG      g_col.rec_bg
#define COL_REC_BORDER  g_col.rec_border
#define COL_REC_HOT     g_col.rec_hot
#define COL_SPLIT_HOT   g_col.split_hot
#define COL_EDIT_BG     g_col.edit_bg
#define COL_EDIT_TEXT   g_col.edit_text

// ---------------------------------------------------------------------------
// state that the modules share with the application file
// ---------------------------------------------------------------------------
extern bool g_dark_ui;                 // ui_theme.cpp
extern int g_ui_dpi;                   // ui_theme.cpp
extern HFONT g_ui_font;                // ui_theme.cpp
extern HFONT g_ui_font_small;          // ui_theme.cpp
extern HBRUSH g_ui_bg_brush;           // ui_theme.cpp
extern HBRUSH g_ui_edit_brush;         // ui_theme.cpp

extern int g_preset_div;               // ui_presets.cpp
extern int g_half_base_w;              // ui_presets.cpp
extern int g_half_base_h;              // ui_presets.cpp

// application state, defined in licecap_ui.cpp
extern HINSTANCE g_hInst;
extern WDL_String g_ini_file;
extern HWND g_hwnd;
extern int g_cap_state;                // 1=rec, 2=pause
extern bool g_capture_cursor;
extern bool g_always_on_top;
extern int g_prefs;
extern int g_max_fps;
extern int g_stop_after_msec;
extern int g_gif_loopcount;
extern int g_titlems;
extern char g_title[4096];
extern bool g_dotitle;
extern char g_pending_fn[2048];        // finished capture, not saved yet
extern bool g_pending_unsaved;         // g_pending_fn is a temporary file
extern char g_last_save_fn[2048];      // last path used by "Save As"

// ---------------------------------------------------------------------------
// ui_theme.cpp - palette, DPI, dark mode, dialog theming
// ---------------------------------------------------------------------------
int UI(int px);                        // DPI-scale a design pixel value
void UI_SelectPalette(bool dark);

#ifdef _WIN32
void UI_LoadThemeAPI(void);
void UI_ApplyAppMode(void);
void UI_SetDarkTitleBar(HWND hwnd);
void UI_ThemeControl(HWND hwnd);
HBRUSH UI_BgBrush(void);
HFONT UI_SelectCtlFont(HDC dc, HWND hwnd);
void UI_PaintToggle(HDC dc, HWND hwnd, int kind);
void UI_PaintGroupBox(HDC dc, HWND hwnd);
void UI_DarkPaintControls(HWND hwndDlg);
#endif

void UI_ThemeDialog(HWND hwndDlg);
bool UI_DialogCtlColor(UINT msg, WPARAM wParam, LRESULT *res);
void UI_CreateBrushes(void);
void UI_Init(void);
void UI_Quit(void);
void UI_SetDarkMode(HWND hwndDlg, bool dark);

// ---------------------------------------------------------------------------
// ui_draw.cpp - main window geometry, region and painting
// ---------------------------------------------------------------------------
int UI_CaptionHeight(void);
void UI_CaptionRect(HWND hwndDlg, RECT *r);
void UI_PaintCaption(HWND hwndDlg, HDC dc);
void DrawCaptionCloseButton(LPDRAWITEMSTRUCT di);
void LayoutControls(HWND hwndDlg);
void ApplyWindowRegion(HWND hwndDlg);
bool ButtonIsLive(int id);
void DrawToolButton(LPDRAWITEMSTRUCT di);
void UpdateHoverState(HWND hwndDlg);
bool ArrowZoneNow(HWND hwndDlg, int id);

// ---------------------------------------------------------------------------
// ui_bookmarks.cpp - favourite output folders
// ---------------------------------------------------------------------------
void Bookmarks_Load(void);
void Bookmarks_Save(void);
const char *Bookmark_Label(const char *path);
const char *Bookmark_ShortPath(const char *path, char *buf, int bufsz);
void Bookmark_StripTrailingSlashes(char *p);
int Bookmarks_IndexOf(const char *path);
int Bookmarks_Add(const char *path);         // 0 added, 1 present, 2 no folder, 3 full
void Bookmarks_Remove(int idx);
void Bookmarks_Move(int idx, int delta);
int Bookmarks_Count(void);
const char *Bookmarks_Path(int idx);
// Folder used most recently: the one a recording was last saved into, or the
// one picked in the drop-down under Save. Stored by path in the ini, so
// reordering or removing other entries cannot point it at the wrong folder.
int Bookmarks_LastUsed(void);              // index, -1 when there is none
void Bookmarks_SetLastUsed(int idx);       // idx < 0 forgets it
void Bookmark_FolderGlyph(HDC dc, int x, int y, int sz, COLORREF c);
bool Bookmark_LastSaveFolder(char *dst, int dstsz);
bool Bookmark_PickFolder(HWND owner, const char *initialdir, char *out, int outsz);
void BookmarkPanel_Show(HWND hwndDlg);
void BookmarkPanel_Refresh(void);
void BookmarkPanel_Close(void);

// ---------------------------------------------------------------------------
// ui_gifedit.cpp - read a capture GIF, compose frames, write a trimmed range
// ---------------------------------------------------------------------------
void *GIFEdit_Load(const char *fn);
void GIFEdit_Free(void *h);
bool GIFEdit_Valid(void *h);
int GIFEdit_Frames(void *h);
int GIFEdit_Width(void *h);
int GIFEdit_Height(void *h);
int GIFEdit_DelayMs(void *h, int idx);
int GIFEdit_StartMs(void *h, int idx);
int GIFEdit_TotalMs(void *h);
int GIFEdit_IndexAtMs(void *h, int ms);
unsigned int *GIFEdit_Compose(void *h, int idx);   // BGRA canvas, DIB ready
bool GIFEdit_WriteRange(void *h, int first, int last, const char *outfn);
// Write the concatenation of several inclusive [firsts[i],lasts[i]] ranges.
bool GIFEdit_WriteRanges(void *h, const int *firsts, const int *lasts, int nranges,
                         const char *outfn);
// Reading and writing are long calls on the caller's thread. A progress
// callback lets them report how far they are: done/total are frames, and
// total <= 0 means "unknown" (the reader only knows the frames so far).
typedef void (*GIFEditProgressFn)(void *ctx, int done, int total);
void GIFEdit_SetProgress(GIFEditProgressFn fn, void *ctx);

// ---------------------------------------------------------------------------
// ui_editor.cpp - timeline trim editor for the last recording
// ---------------------------------------------------------------------------
bool Editor_Active(void);
bool Editor_Open(HWND hwndDlg);
void Editor_Close(HWND hwndDlg);
bool Editor_Apply(HWND hwndDlg);
void Editor_Paint(HWND hwndDlg, HDC dc);
bool Editor_Mouse(HWND hwndDlg, UINT msg, int x, int y);
bool Editor_HitTest(HWND hwndDlg, int x, int y);
// cursor shape the editor wants over a point (platform neutral)
enum { EDITOR_CUR_NONE = 0, EDITOR_CUR_HAND, EDITOR_CUR_HANDLE };
int Editor_Cursor(HWND hwndDlg, int x, int y);
bool Editor_Key(HWND hwndDlg, WPARAM vk);
bool Editor_Tick(HWND hwndDlg);
void Editor_Shutdown(void);

// ---------------------------------------------------------------------------
// ui_presets.cpp - capture sizes and the Settings dialog
// ---------------------------------------------------------------------------
const char *PresetNameFor(int w, int h);
void ResizeCaptureArea(int cw, int ch);
void SetPresetDiv(HWND hwndDlg, int div);
void ApplyPreset(HWND hwndDlg, int idx);
void ShowPresetMenu(HWND hwndDlg);
int PresetCount(void);
void ShowCustomization(HWND hwndDlg);
void SetFullScreen(HWND hwndDlg);
void SetDefaultWindowSize(HWND hwndDlg);

// ---------------------------------------------------------------------------
// application file (licecap_ui.cpp) entry points used by the modules
// ---------------------------------------------------------------------------
void UpdateDimBoxes(HWND hwndDlg);
void UpdateCaption(HWND hwndDlg);
void UpdateStatusText(HWND hwndDlg);

// ---------------------------------------------------------------------------
// Run one worker while a modal progress window keeps the UI message loop alive.
// The task must own its mutable data: painting continues on the UI thread.
// No UI calls are allowed from the task or its progress callback.
// ---------------------------------------------------------------------------
typedef bool (*BusyWorkFn)(void *ctx);
bool Busy_Run(HWND owner, const char *label, int total, BusyWorkFn fn, void *ctx);
void Busy_Progress(void *ctx, int done, int total);
bool Busy_Active(void);
void GetViewRectSize(int *w, int *h);
void my_getViewport(RECT *r, RECT *sr, bool wantWork);

#endif // LICECAP_UI_H
