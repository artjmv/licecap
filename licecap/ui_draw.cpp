/*
    ui_draw.cpp - geometry, window region and painting of the main window.

    Split out of licecap_ui.cpp (fork-only GifCam-style UI), code unchanged.
    The window keeps a region (the capture hole) and paints its own caption and
    toolbar, so all of that geometry lives here.
*/

#include "licecap_ui.h"
#include "../WDL/wdlcstring.h"

// hover/focus tracking for the owner-drawn side toolbar
static int g_hot_btn, g_focus_btn;
int UI_CaptionHeight(void)
{
  return UI(UI_CAPTION_H);
}

void UI_CaptionRect(HWND hwndDlg, RECT *r)
{
  GetClientRect(hwndDlg,r);
  r->bottom = r->top + UI_CaptionHeight();
}

// The window keeps a region (the capture hole), and Windows draws classic
// frames for region windows, so the title bar is painted by the app in both
// themes instead of relying on the system caption.
void UI_PaintCaption(HWND hwndDlg, HDC dc)
{
  RECT cr;
  UI_CaptionRect(hwndDlg,&cr);

  FillRect(dc,&cr,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(WHITE_BRUSH));

  const int isz = UI(16);
  HICON ic = (HICON)(INT_PTR)SendMessage(hwndDlg,WM_GETICON,ICON_SMALL,0);
  if (!ic) ic = (HICON)(INT_PTR)GetClassLongPtr(hwndDlg,GCLP_HICON);
  int left = UI(9);
  if (ic)
  {
    DrawIconEx(dc,left,(cr.bottom-isz)/2,ic,isz,isz,0,NULL,DI_NORMAL);
    left += isz + UI(8);
  }

  char title[512];
  title[0] = 0;
  GetWindowTextA(hwndDlg,title,sizeof(title));

  RECT tr = cr;
  tr.left = left;
  tr.right = cr.right - UI(UI_CAPTION_H) - UI(6);

  SetBkMode(dc,TRANSPARENT);
  HFONT f = g_ui_font ? g_ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HGDIOBJ of = SelectObject(dc,f);
  SetTextColor(dc,COL_TEXT);
  DrawTextA(dc,title,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  SelectObject(dc,of);

  HPEN pen = CreatePen(PS_SOLID,1,COL_SEP);
  HGDIOBJ op = SelectObject(dc,pen);
  MoveToEx(dc,cr.left,cr.bottom-1,NULL);
  LineTo(dc,cr.right,cr.bottom-1);
  SelectObject(dc,op);
  DeleteObject(pen);
}

void DrawCaptionCloseButton(LPDRAWITEMSTRUCT di)
{
  HDC dc = di->hDC;
  RECT r = di->rcItem;
  const bool hot = (g_hot_btn == IDC_CLOSEBTN);
  const bool pressed = (di->itemState & ODS_SELECTED) != 0;

  HBRUSH hb = CreateSolidBrush(pressed ? COL_BTN_DOWN : hot ? COL_BTN_HOT : COL_BG);
  FillRect(dc,&r,hb);
  DeleteObject(hb);

  const int m = UI(5);
  HPEN p = CreatePen(PS_SOLID,1, hot ? COL_TEXT : COL_TEXT_DIS);
  HGDIOBJ op = SelectObject(dc,p);
  MoveToEx(dc,r.left+m,r.top+m,NULL);
  LineTo(dc,r.right-m-1,r.bottom-m-1);
  MoveToEx(dc,r.right-m-1,r.top+m,NULL);
  LineTo(dc,r.left+m,r.bottom-m-1);
  SelectObject(dc,op);
  DeleteObject(p);
}

void LayoutControls(HWND hwndDlg)
{
  RECT rc;
  GetClientRect(hwndDlg,&rc);

  const int tw = UI(UI_BTN_COL_W);
  const int pad = UI(UI_PAD);
  const int caph = UI_CaptionHeight();
  int gap = UI(UI_BTN_GAP);
  int bh = UI(UI_BTN_H);

  // The drop-down under Save is a compact row glued to the button instead of a
  // full-size toolbar button.
  const int selh = UI(18);
  const int selgap = UI(2);

  static const int ids[] = { IDC_REC, IDC_EDITBTN, IDC_SAVE, IDC_BOOKMARKDROP, IDC_EDITMODE, IDC_PRESETS };
  const int count = (int)(sizeof(ids)/sizeof(ids[0]));
  const int nbtn = count - 1;   // every entry except the compact drop-down row

  // On very small windows the buttons shrink and the status block gives way.
  const int avail = rc.bottom - caph - 2*pad;
  const int need = nbtn*bh + selh + (nbtn-1)*gap + selgap + UI(22);
  if (need > avail)
  {
    gap = UI(4);
    bh = (avail - (nbtn-1)*gap - selgap - selh) / nbtn;
    if (bh < UI(16)) bh = UI(16);
  }

  int x = rc.right - tw + pad;
  int w = tw - 2*pad;
  if (w < UI(40)) w = UI(40);

  int y = caph + pad;
  int i;
  for (i = 0; i < count; i++)
  {
    const bool drop = (ids[i] == IDC_BOOKMARKDROP);
    if (i) y += drop ? selgap : gap;
    {
      HWND b = GetDlgItem(hwndDlg, ids[i]);
      const int h = drop ? selh : bh;
      if (b) MoveWindow(b, x, y, w, h, TRUE);
      y += h;
    }
  }

  HWND st = GetDlgItem(hwndDlg, IDC_STATUS);
  if (st)
  {
    int sh = rc.bottom - y - pad;
    if (sh >= UI(18))
    {
      MoveWindow(st, x, y, w, sh, TRUE);
      ShowWindow(st, SW_SHOWNA);
    }
    else
    {
      ShowWindow(st, SW_HIDE);
    }
  }

  HWND cl = GetDlgItem(hwndDlg, IDC_CLOSEBTN);
  if (cl)
  {
    int cs = caph - UI(6);
    if (cs < UI(14)) cs = UI(14);
    MoveWindow(cl, rc.right - pad - cs, (caph-cs)/2, cs, cs, TRUE);
  }

  HWND vr = GetDlgItem(hwndDlg, IDC_VIEWRECT);
  if (vr)
  {
    int cw = rc.right - tw - UI(2);
    int ch = rc.bottom - caph - UI(1);
    if (cw < 1) cw = 1;
    if (ch < 1) ch = 1;
    SetWindowPos(vr, NULL, UI(1), caph, cw, ch, SWP_NOZORDER|SWP_NOACTIVATE);
    // While the timeline editor is open the capture hole becomes a preview
    // painted by the main window, so the static child has to go away.
    ShowWindow(vr, Editor_Active() ? SW_HIDE : SW_SHOWNA);
  }
}

// Cuts the capture area out of the window so the desktop shows through while
// keeping the caption, the 1px frame and the toolbar clickable.
void ApplyWindowRegion(HWND hwndDlg)
{
  // In edit mode the whole window is opaque: the hole is covered by the
  // preview of the recording, which the main window paints itself.
  if (Editor_Active())
  {
    SetWindowRgn(hwndDlg,NULL,TRUE);
    return;
  }

  HWND vr = GetDlgItem(hwndDlg, IDC_VIEWRECT);
  if (!vr) return;

  RECT wr, vr2;
  GetWindowRect(hwndDlg,&wr);
  GetWindowRect(vr,&vr2);
  OffsetRect(&vr2,-wr.left,-wr.top);
  InflateRect(&vr2,-UI(1),-UI(1));

  if (vr2.right <= vr2.left || vr2.bottom <= vr2.top)
  {
    SetWindowRgn(hwndDlg,NULL,TRUE);
    return;
  }

  RECT full = {0,0,wr.right-wr.left,wr.bottom-wr.top};
  HRGN rgn = CreateRectRgnIndirect(&full);
  HRGN hole = CreateRectRgnIndirect(&vr2);
  CombineRgn(rgn,rgn,hole,RGN_DIFF);
  DeleteObject(hole);
  SetWindowRgn(hwndDlg,rgn,TRUE);
}

bool ButtonIsLive(int id)
{
  if (id == IDC_REC) return !Editor_Active();
  if (id == IDC_SAVE) return g_pending_unsaved && !Editor_Active();
  if (id == IDC_BOOKMARKDROP) return !g_cap_state && !Editor_Active();
  if (id == IDC_PRESETS) return !g_cap_state && !Editor_Active();
  // Edit stays live while idle: with a pending recording it trims it, and with
  // nothing recorded the label opens a GIF from disk (the arrow has both).
  if (id == IDC_EDITMODE) return !g_cap_state;
  if (id == IDC_EDITBTN) return !Editor_Active();
  return true;
}

// Drop-down row under Save: shows the bookmark folder recordings go to. It is
// painted like a field (recessed background) so it reads as a value selector
// and not as one more toolbar button.
void DrawBookmarkDrop(LPDRAWITEMSTRUCT di)
{
  HDC dc = di->hDC;
  RECT r = di->rcItem;
  const bool hot = (g_hot_btn == IDC_BOOKMARKDROP);
  const bool focused = (GetFocus() == di->hwndItem);
  const bool pressed = (di->itemState & ODS_SELECTED) != 0;
  const bool live = ButtonIsLive(IDC_BOOKMARKDROP);
  const int sel = Bookmarks_LastUsed();

  FillRect(dc,&r,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(WHITE_BRUSH));

  RECT br = r;
  br.right--;
  br.bottom--;

  COLORREF fill, border, text;
  if (!live)
  {
    fill = COL_BTN_DIS;
    border = COL_BORDER;
    text = COL_TEXT_DIS;
  }
  else if (pressed)
  {
    fill = COL_BTN_DOWN;
    border = COL_ACCENT;
    text = COL_TEXT_HOT;
  }
  else if (hot)
  {
    fill = COL_BTN_HOT;
    border = COL_BORDER_HOT;
    text = COL_TEXT_HOT;
  }
  else
  {
    fill = COL_EDIT_BG;
    border = focused ? COL_ACCENT : COL_BORDER;
    text = COL_TEXT;
  }

  HBRUSH hb = CreateSolidBrush(fill);
  HPEN hp = CreatePen(PS_SOLID,1,border);
  HGDIOBJ ob = SelectObject(dc,hb);
  HGDIOBJ op = SelectObject(dc,hp);
  RoundRect(dc,br.left,br.top,br.right,br.bottom,UI(4),UI(4));
  SelectObject(dc,ob);
  SelectObject(dc,op);
  DeleteObject(hb);
  DeleteObject(hp);

  const int glyph = UI(11);
  Bookmark_FolderGlyph(dc,r.left + UI(6),(r.top + r.bottom - glyph)/2 + UI(1),glyph,COL_TEXT_DIS);

  SetBkMode(dc,TRANSPARENT);
  HFONT f = g_ui_font_small ? g_ui_font_small : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HGDIOBJ of = SelectObject(dc,f);
  SetTextColor(dc,text);

  RECT tr = r;
  tr.left += UI(6) + glyph + UI(5);
  tr.right -= UI(12);
  if (tr.right > tr.left)
  {
    const char *label = sel >= 0 ? Bookmark_Label(Bookmarks_Path(sel)) : "Add folder...";
    DrawTextA(dc,label,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  }

  {
    const int cx = r.right - UI(7);
    const int cy = (r.top + r.bottom - 1)/2;
    POINT pts[3] = { {cx-UI(3),cy-UI(1)}, {cx+UI(3),cy-UI(1)}, {cx,cy+UI(3)} };
    HBRUSH ab = CreateSolidBrush(text);
    HGDIOBJ oab = SelectObject(dc,ab);
    Polygon(dc,pts,3);
    SelectObject(dc,oab);
    DeleteObject(ab);
  }

  SelectObject(dc,of);
}

void DrawToolButton(LPDRAWITEMSTRUCT di)
{
  if (!di || di->CtlType != ODT_BUTTON) return;
  if (di->CtlID == IDC_BOOKMARKDROP)
  {
    DrawBookmarkDrop(di);
    return;
  }

  HDC dc = di->hDC;
  const int id = (int)di->CtlID;
  RECT r = di->rcItem;
  const bool hot = (g_hot_btn == id);
  const bool focused = (GetFocus() == di->hwndItem);
  const bool pressed = (di->itemState & ODS_SELECTED) != 0;
  const bool split = (id == IDC_REC || id == IDC_SAVE || id == IDC_EDITMODE);
  const bool live = ButtonIsLive(id);
  const bool rec = (id == IDC_REC && g_cap_state != 0);

  FillRect(dc,&r,g_ui_bg_brush ? g_ui_bg_brush : (HBRUSH)GetStockObject(WHITE_BRUSH));

  RECT br = r;
  br.right--;
  br.bottom--;

  COLORREF fill, border, text;
  if (rec)
  {
    fill = hot ? COL_REC_HOT : COL_REC_BG;
    border = focused ? COL_ACCENT : COL_REC_BORDER;
    text = COL_REC;
  }
  else if (!live)
  {
    fill = COL_BTN_DIS;
    border = COL_BORDER;
    text = COL_TEXT_DIS;
  }
  else if (pressed)
  {
    fill = COL_BTN_DOWN;
    border = COL_ACCENT;
    text = COL_TEXT_HOT;
  }
  else if (hot)
  {
    fill = COL_BTN_HOT;
    border = COL_BORDER_HOT;
    text = COL_TEXT_HOT;
  }
  else
  {
    fill = COL_BTN;
    border = focused ? COL_ACCENT : COL_BORDER;
    text = COL_TEXT;
  }

  HBRUSH hb = CreateSolidBrush(fill);
  HPEN hp = CreatePen(PS_SOLID,1,border);
  HGDIOBJ ob = SelectObject(dc,hb);
  HGDIOBJ op = SelectObject(dc,hp);
  RoundRect(dc,br.left,br.top,br.right,br.bottom,UI(6),UI(6));
  SelectObject(dc,ob);
  SelectObject(dc,op);
  DeleteObject(hb);
  DeleteObject(hp);

  SetBkMode(dc,TRANSPARENT);
  HFONT f = g_ui_font ? g_ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
  HGDIOBJ of = SelectObject(dc,f);
  SetTextColor(dc,text);

  char label[64];
  label[0] = 0;
  GetWindowTextA(di->hwndItem,label,sizeof(label));
  if (id == IDC_PRESETS) lstrcpyn_safe(label,g_preset_div == 2 ? "Presets /2" : "Presets",sizeof(label));
  if (!label[0]) lstrcpyn_safe(label,id == IDC_REC ? "Rec" : id == IDC_EDITBTN ? "Settings" : id == IDC_SAVE ? "Save" : "Presets",sizeof(label));

  RECT tr = r;
  tr.left += UI(9);
  tr.right -= split ? UI(16) : UI(7);
  DrawTextA(dc,label,-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);

  if (split)
  {
    int x = r.right - UI(16);
    HPEN sp = CreatePen(PS_SOLID,1,hot ? COL_SPLIT_HOT : COL_BORDER);
    HGDIOBJ osp = SelectObject(dc,sp);
    MoveToEx(dc,x,r.top+UI(5),NULL);
    LineTo(dc,x,r.bottom-UI(5));
    SelectObject(dc,osp);
    DeleteObject(sp);

    int cx = r.right - UI(8);
    int cy = (r.top + r.bottom - 1)/2 + UI(1);
    POINT pts[3] = { {cx-UI(3),cy-UI(2)}, {cx+UI(3),cy-UI(2)}, {cx,cy+UI(2)} };
    HBRUSH ab = CreateSolidBrush(text);
    HGDIOBJ oab = SelectObject(dc,ab);
    Polygon(dc,pts,3);
    SelectObject(dc,oab);
    DeleteObject(ab);
  }

  SelectObject(dc,of);
}

void UpdateHoverState(HWND hwndDlg)
{
  int hot = 0;
  if (GetForegroundWindow() == hwndDlg)
  {
    POINT p;
    GetCursorPos(&p);
    static const int ids[] = { IDC_REC, IDC_EDITBTN, IDC_SAVE, IDC_BOOKMARKDROP, IDC_EDITMODE, IDC_PRESETS, IDC_CLOSEBTN };
    int i;
    for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++)
    {
      HWND b = GetDlgItem(hwndDlg, ids[i]);
      RECT r;
      if (b && IsWindowVisible(b) && GetWindowRect(b,&r) && PtInRect(&r,p))
      {
        hot = ids[i];
        break;
      }
    }
  }
  if (hot != g_hot_btn)
  {
    HWND oldb = g_hot_btn ? GetDlgItem(hwndDlg,g_hot_btn) : NULL;
    HWND newb = hot ? GetDlgItem(hwndDlg,hot) : NULL;
    g_hot_btn = hot;
    if (oldb) InvalidateRect(oldb,NULL,TRUE);
    if (newb) InvalidateRect(newb,NULL,TRUE);
  }

  HWND f = GetFocus();
  int fid = f ? GetDlgCtrlID(f) : 0;
  if (fid != IDC_REC && fid != IDC_EDITBTN && fid != IDC_SAVE && fid != IDC_BOOKMARKDROP && fid != IDC_EDITMODE && fid != IDC_PRESETS && fid != IDC_CLOSEBTN) fid = 0;
  if (fid != g_focus_btn)
  {
    HWND oldb = g_focus_btn ? GetDlgItem(hwndDlg,g_focus_btn) : NULL;
    g_focus_btn = fid;
    if (oldb) InvalidateRect(oldb,NULL,TRUE);
    if (f && fid) InvalidateRect(f,NULL,TRUE);
  }
}

// Dialogs never receive WM_PARENTNOTIFY, so the split-button hit area is
// resolved from the cursor position while the button is being clicked.
bool ArrowZoneNow(HWND hwndDlg, int id)
{
  HWND b = GetDlgItem(hwndDlg,id);
  if (!b) return false;
  if (id != IDC_REC && id != IDC_SAVE && id != IDC_EDITMODE) return true;
  RECT r;
  POINT p;
  if (!GetWindowRect(b,&r)) return false;
  GetCursorPos(&p);
  if (!PtInRect(&r,p)) return false;
  return p.x >= r.right - UI(18);
}
