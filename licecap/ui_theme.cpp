/*
    ui_theme.cpp - palette, DPI scaling, dark mode and dialog theming.

    Split out of licecap_ui.cpp (fork-only GifCam-style UI). Code moved
    verbatim; only linkage changed (helpers are no longer file-static because
    the other modules and the legacy dialogs use them).
*/

#include "licecap_ui.h"

// theme state, declared in licecap_ui.h so the dialogs and the other modules
// can read it without duplicating it
bool g_dark_ui = true;
int g_ui_dpi = 96;
HFONT g_ui_font, g_ui_font_small;
HBRUSH g_ui_bg_brush, g_ui_edit_brush;
// Two palettes, same field names: the UI code below only knows the COL_*
// macros, so switching theme at runtime is just swapping g_col.
static const LiceUIColors s_colors_light =
{
  RGB(250,250,250), RGB(245,246,248), RGB(234,244,253), RGB(214,232,250), RGB(246,246,246),
  RGB(200,204,209), RGB(140,180,225), RGB(47,127,209),
  RGB(40,40,40), RGB(175,175,175), RGB(20,90,170),
  RGB(224,226,229),
  RGB(216,58,48), RGB(253,236,235), RGB(228,158,152), RGB(252,226,224),
  RGB(202,222,242),
  RGB(255,255,255), RGB(30,30,30),
};

static const LiceUIColors s_colors_dark =
{
  RGB(30,30,32), RGB(46,46,50), RGB(61,61,68), RGB(75,75,84), RGB(38,38,42),
  RGB(78,78,86), RGB(96,148,208), RGB(83,163,236),
  RGB(232,232,236), RGB(126,126,133), RGB(152,207,255),
  RGB(58,58,64),
  RGB(240,98,88), RGB(58,32,32), RGB(124,62,58), RGB(76,41,41),
  RGB(88,96,110),
  RGB(24,24,27), RGB(233,233,237),
};

// current palette: the paint code only knows the COL_* macros (licecap_ui.h)
LiceUIColors g_col = s_colors_light;





int UI(int px)
{
  return MulDiv(px, g_ui_dpi > 0 ? g_ui_dpi : 96, 96);
}

void UI_SelectPalette(bool dark)
{
  g_dark_ui = dark;
  g_col = dark ? s_colors_dark : s_colors_light;
}

// "Dark Mode" is an undocumented uxtheme feature: SetPreferredAppMode makes
// standard controls and popup menus follow the app preference on Windows 10
// 1809+, and AllowDarkModeForWindow/SetWindowTheme switch a single window.
typedef int (WINAPI *UI_SetPreferredAppMode_t)(int);
typedef BOOL (WINAPI *UI_AllowDarkModeForWindow_t)(HWND, BOOL);
typedef HRESULT (WINAPI *UI_SetWindowTheme_t)(HWND, LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *UI_DwmSetWindowAttribute_t)(HWND, DWORD, LPCVOID, DWORD);

static UI_SetPreferredAppMode_t UI_pfnSetPreferredAppMode;
static UI_AllowDarkModeForWindow_t UI_pfnAllowDarkModeForWindow;
static UI_SetWindowTheme_t UI_pfnSetWindowTheme;
static UI_DwmSetWindowAttribute_t UI_pfnDwmSetWindowAttribute;
static bool UI_theme_api_init;

void UI_LoadThemeAPI(void)
{
#ifdef _WIN32
  if (UI_theme_api_init) return;
  UI_theme_api_init = true;

  HMODULE ux = LoadLibraryA("uxtheme.dll");
  if (ux)
  {
    // ordinals, no import library available for these
    UI_pfnSetPreferredAppMode = (UI_SetPreferredAppMode_t)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    UI_pfnAllowDarkModeForWindow = (UI_AllowDarkModeForWindow_t)GetProcAddress(ux, MAKEINTRESOURCEA(133));
    UI_pfnSetWindowTheme = (UI_SetWindowTheme_t)GetProcAddress(ux, "SetWindowTheme");
  }
  HMODULE dwm = LoadLibraryA("dwmapi.dll");
  if (dwm) UI_pfnDwmSetWindowAttribute = (UI_DwmSetWindowAttribute_t)GetProcAddress(dwm, "DwmSetWindowAttribute");
#endif
}

// 0 = let Windows decide, 1 = allow dark, 2 = force dark, 3 = force light
void UI_ApplyAppMode(void)
{
#ifdef _WIN32
  UI_LoadThemeAPI();
  if (UI_pfnSetPreferredAppMode) UI_pfnSetPreferredAppMode(g_dark_ui ? 2 : 3);
#endif
}

void UI_SetDarkTitleBar(HWND hwnd)
{
#ifdef _WIN32
  UI_LoadThemeAPI();
  if (!hwnd || !UI_pfnDwmSetWindowAttribute) return;
  BOOL on = g_dark_ui ? TRUE : FALSE;
  if (FAILED(UI_pfnDwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &on, sizeof(on))))
    UI_pfnDwmSetWindowAttribute(hwnd, 19 /*older builds*/, &on, sizeof(on));
#endif
}

void UI_ThemeControl(HWND hwnd)
{
#ifdef _WIN32
  if (!hwnd) return;
  UI_LoadThemeAPI();
  if (UI_pfnAllowDarkModeForWindow) UI_pfnAllowDarkModeForWindow(hwnd, g_dark_ui ? TRUE : FALSE);
  if (UI_pfnSetWindowTheme)
    UI_pfnSetWindowTheme(hwnd, g_dark_ui ? L"DarkMode_Explorer" : L"", g_dark_ui ? NULL : L"");
#endif
}

#ifdef _WIN32
// uxtheme's dark mode recolours the frame and the glyph of the BUTTON class
// but not its label (and it never handles BS_GROUPBOX), so on a dark dialog
// those texts end up dark-on-dark and cannot be read. Owner-drawing the
// controls is not an option: BS_OWNERDRAW drops BM_GETCHECK/BM_SETCHECK and
// the automatic toggling the rest of this file relies on. Instead the controls
// keep their real style and are subclassed to paint themselves with the app
// palette (the button still handles clicks, state and keyboard).
#define UI_DARKCTL_PROP "licecap_darkctl"
enum { UI_DARKCTL_GROUP = 1, UI_DARKCTL_CHECK, UI_DARKCTL_RADIO };

HBRUSH UI_BgBrush(void)
{
  return g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(BLACK_BRUSH);
}

HFONT UI_SelectCtlFont(HDC dc, HWND hwnd)
{
  HFONT f = (HFONT)SendMessage(hwnd, WM_GETFONT, 0, 0);
  return f ? (HFONT)SelectObject(dc, f) : NULL;
}

void UI_PaintToggle(HDC dc, HWND hwnd, int kind)
{
  RECT rc;
  GetClientRect(hwnd, &rc);

  const int state = (int)SendMessage(hwnd, BM_GETCHECK, 0, 0);
  const bool checked = state == BST_CHECKED;
  const bool indeterminate = state == BST_INDETERMINATE;
  const bool disabled = !IsWindowEnabled(hwnd);
  const bool focused = GetFocus() == hwnd;

  FillRect(dc, &rc, UI_BgBrush());

  HFONT oldfont = UI_SelectCtlFont(dc, hwnd);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, disabled ? COL_TEXT_DIS : COL_TEXT);

  const int box = UI(13);
  const int cy = (rc.top + rc.bottom) / 2;
  RECT g = { rc.left + UI(1), cy - box / 2, rc.left + UI(1) + box, cy - box / 2 + box };
  const bool on = checked || indeterminate;
  const COLORREF edge = (on && !disabled) ? COL_ACCENT : COL_BORDER;

  HBRUSH fill = CreateSolidBrush((on && !disabled) ? COL_ACCENT : COL_EDIT_BG);
  HPEN pen = CreatePen(PS_SOLID, 1, edge);
  HGDIOBJ oldbrush = SelectObject(dc, fill);
  HGDIOBJ oldpen = SelectObject(dc, pen);

  if (kind == UI_DARKCTL_RADIO)
  {
    Ellipse(dc, g.left, g.top, g.right, g.bottom);
    if (on)
    {
      HBRUSH dot = CreateSolidBrush(disabled ? COL_TEXT_DIS : COL_ACCENT);
      HGDIOBJ oldb = SelectObject(dc, dot);
      HGDIOBJ oldp = SelectObject(dc, GetStockObject(NULL_PEN));
      Ellipse(dc, g.left + UI(4), g.top + UI(4), g.right - UI(4), g.bottom - UI(4));
      SelectObject(dc, oldp);
      SelectObject(dc, oldb);
      DeleteObject(dot);
    }
  }
  else
  {
    RoundRect(dc, g.left, g.top, g.right, g.bottom, UI(4), UI(4));
    if (on)
    {
      HPEN tick = CreatePen(PS_SOLID, UI(2), disabled ? COL_TEXT_DIS : RGB(255,255,255));
      HGDIOBJ p = SelectObject(dc, tick);
      if (indeterminate)
      {
        MoveToEx(dc, g.left + UI(4), cy, NULL);
        LineTo(dc, g.right - UI(4), cy);
      }
      else
      {
        POINT pts[3] = { { g.left + UI(3), g.top + UI(7) },
                         { g.left + UI(5), g.top + UI(9) },
                         { g.left + UI(10), g.top + UI(4) } };
        Polyline(dc, pts, 3);
      }
      SelectObject(dc, p);
      DeleteObject(tick);
    }
  }
  SelectObject(dc, oldbrush);
  SelectObject(dc, oldpen);
  DeleteObject(fill);
  DeleteObject(pen);

  char text[512];
  text[0] = 0;
  GetWindowTextA(hwnd, text, sizeof(text));
  if (text[0])
  {
    RECT tr = rc;
    tr.left = g.right + UI(6);
    DrawTextA(dc, text, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    if (focused)
    {
      RECT calc = { 0, 0, 0, 0 };
      DrawTextA(dc, text, -1, &calc, DT_CALCRECT | DT_SINGLELINE | DT_NOPREFIX);
      RECT fr = { g.right + UI(5), cy - calc.bottom / 2 - UI(1),
                  g.right + UI(5) + calc.right + UI(2), cy - calc.bottom / 2 + calc.bottom + UI(1) };
      DrawFocusRect(dc, &fr);
    }
  }

  if (oldfont) SelectObject(dc, oldfont);
}

void UI_PaintGroupBox(HDC dc, HWND hwnd)
{
  RECT rc;
  GetClientRect(hwnd, &rc);
  FillRect(dc, &rc, UI_BgBrush());

  char text[512];
  text[0] = 0;
  GetWindowTextA(hwnd, text, sizeof(text));

  HFONT oldfont = UI_SelectCtlFont(dc, hwnd);
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, COL_TEXT);

  TEXTMETRIC tm;
  GetTextMetrics(dc, &tm);
  const int labelx = rc.left + UI(8);
  // The label sits on the top border, half of it above and half below.
  const int top = rc.top + (text[0] ? tm.tmHeight / 2 : 0);

  HPEN pen = CreatePen(PS_SOLID, 1, COL_BORDER);
  HGDIOBJ oldpen = SelectObject(dc, pen);
  HGDIOBJ oldbrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
  RoundRect(dc, rc.left, top, rc.right - 1, rc.bottom - 1, UI(6), UI(6));
  SelectObject(dc, oldbrush);
  SelectObject(dc, oldpen);
  DeleteObject(pen);

  if (text[0])
  {
    SIZE ts = { 0, 0 };
    GetTextExtentPoint32A(dc, text, (int)strlen(text), &ts);
    RECT gap = { labelx - UI(3), top, labelx + ts.cx + UI(3), top + UI(2) };
    FillRect(dc, &gap, UI_BgBrush());
    RECT tr = { labelx, rc.top, rc.right, rc.top + tm.tmHeight + UI(1) };
    DrawTextA(dc, text, -1, &tr, DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
  }

  if (oldfont) SelectObject(dc, oldfont);
}

LRESULT CALLBACK UI_DarkCtlProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam,
                                       UINT_PTR id, DWORD_PTR ref)
{
  const int kind = (int)(INT_PTR)GetPropA(hwnd, UI_DARKCTL_PROP);
  switch (msg)
  {
    case WM_PAINT:
      if (kind)
      {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        if (kind == UI_DARKCTL_GROUP) UI_PaintGroupBox(dc, hwnd);
        else UI_PaintToggle(dc, hwnd, kind);
        EndPaint(hwnd, &ps);
        return 0;
      }
      break;

    case WM_ERASEBKGND:
      if (kind) return 1; // WM_PAINT fills the whole client area
      break;

    case WM_THEMECHANGED:
    case WM_SYSCOLORCHANGE:
      if (kind) InvalidateRect(hwnd, NULL, TRUE);
      break;
  }
  return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Group frames and toggle labels of a dark dialog paint themselves.
void UI_DarkPaintControls(HWND hwndDlg)
{
  if (!hwndDlg) return;
  for (HWND c = GetWindow(hwndDlg, GW_CHILD); c; c = GetWindow(c, GW_HWNDNEXT))
  {
    char cls[64];
    cls[0] = 0;
    GetClassNameA(c, cls, sizeof(cls));
    if (stricmp(cls, "Button")) continue;

    const int type = (int)(GetWindowLong(c, GWL_STYLE) & 0xFL);
    int kind = 0;
    if (type == BS_GROUPBOX) kind = UI_DARKCTL_GROUP;
    else if (type == BS_AUTOCHECKBOX || type == BS_CHECKBOX ||
             type == BS_AUTO3STATE || type == BS_3STATE) kind = UI_DARKCTL_CHECK;
    else if (type == BS_AUTORADIOBUTTON || type == BS_RADIOBUTTON) kind = UI_DARKCTL_RADIO;
    if (!kind || GetPropA(c, UI_DARKCTL_PROP)) continue;

    SetPropA(c, UI_DARKCTL_PROP, (HANDLE)(INT_PTR)kind);
    SetWindowSubclass(c, UI_DarkCtlProc, 1, 0);
    InvalidateRect(c, NULL, TRUE);
  }
}
#endif

// Themed common controls draw their frame themselves, but dialog text and edit
// fields still ask the parent for colours.
void UI_ThemeDialog(HWND hwndDlg)
{
#ifdef _WIN32
  if (!hwndDlg) return;
  UI_SetDarkTitleBar(hwndDlg);
  UI_ThemeControl(hwndDlg);
  HWND c = GetWindow(hwndDlg, GW_CHILD);
  while (c)
  {
    UI_ThemeControl(c);
    c = GetWindow(c, GW_HWNDNEXT);
  }
  // Must come after UI_ThemeControl: the subclass paints instead of the theme.
  if (g_dark_ui) UI_DarkPaintControls(hwndDlg);
#endif
}

#ifdef _WIN32
// returns true when the message was a colour request and *res is the answer
bool UI_DialogCtlColor(UINT msg, WPARAM wParam, LRESULT *res)
{
  if (!g_dark_ui) return false;
  HDC dc = (HDC)wParam;
  if (!dc) return false;

  switch (msg)
  {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, COL_TEXT);
      *res = (LRESULT)(g_ui_bg_brush ? g_ui_bg_brush : GetStockObject(BLACK_BRUSH));
      return true;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
      SetBkColor(dc, COL_EDIT_BG);
      SetTextColor(dc, COL_EDIT_TEXT);
      *res = (LRESULT)(g_ui_edit_brush ? g_ui_edit_brush : (HBRUSH)GetStockObject(BLACK_BRUSH));
      return true;
  }
  return false;
}
#endif

void UI_CreateBrushes(void)
{
  if (!g_ui_bg_brush) g_ui_bg_brush = CreateSolidBrush(COL_BG);
  if (!g_ui_edit_brush) g_ui_edit_brush = CreateSolidBrush(COL_EDIT_BG);
}

void UI_Init(void)
{
  UI_SelectPalette(g_dark_ui);

  HDC dc = GetDC(NULL);
  if (dc)
  {
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    if (dpi >= 72 && dpi <= 480) g_ui_dpi = dpi;
  }
  UI_CreateBrushes();
  if (!g_ui_font)
    g_ui_font = CreateFontA(-MulDiv(9,g_ui_dpi,72),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                            DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
  if (!g_ui_font_small)
    g_ui_font_small = CreateFontA(-MulDiv(8,g_ui_dpi,72),0,0,0,FW_NORMAL,0,0,0,DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,
                            DEFAULT_PITCH|FF_DONTCARE,"Segoe UI");
}

void UI_Quit(void)
{
  if (g_ui_font) { DeleteObject(g_ui_font); g_ui_font = NULL; }
  if (g_ui_font_small) { DeleteObject(g_ui_font_small); g_ui_font_small = NULL; }
  if (g_ui_bg_brush) { DeleteObject(g_ui_bg_brush); g_ui_bg_brush = NULL; }
  if (g_ui_edit_brush) { DeleteObject(g_ui_edit_brush); g_ui_edit_brush = NULL; }
}

// Switches theme while the window is up: palette, brushes, title bar and the
// uxtheme preference for controls created afterwards (dialogs, menus).
void UI_SetDarkMode(HWND hwndDlg, bool dark)
{
  if (g_dark_ui == dark) return;

  UI_SelectPalette(dark);
  if (g_ui_bg_brush) { DeleteObject(g_ui_bg_brush); g_ui_bg_brush = NULL; }
  if (g_ui_edit_brush) { DeleteObject(g_ui_edit_brush); g_ui_edit_brush = NULL; }
  UI_CreateBrushes();
  UI_ApplyAppMode();
  UI_SetDarkTitleBar(hwndDlg);
  UI_ThemeDialog(hwndDlg);

  if (hwndDlg)
  {
    RedrawWindow(hwndDlg, NULL, NULL, RDW_INVALIDATE|RDW_ERASE|RDW_FRAME|RDW_ALLCHILDREN);
  }

  WritePrivateProfileString("licecap","dark",dark ? "1" : "0",g_ini_file.Get());
}
