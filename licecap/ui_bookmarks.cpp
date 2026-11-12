/*
    ui_bookmarks.cpp - Blender-style favourite output folders.

    Two halves that used to be separated by the capture/save flow in
    licecap_ui.cpp: the list model (ini storage, add/remove/reorder, folder
    picker) and the small popup panel that shows it under the Save button.

    Split out of licecap_ui.cpp (fork-only code), unchanged.
*/

#include "licecap_ui.h"
#ifdef _WIN32
#include <shlobj.h>   // IFileOpenDialog / SHBrowseForFolder
#endif
#include "../WDL/wdlcstring.h"
// ---------------------------------------------------------------------------
// save bookmarks (Blender-style favourite output folders, stored in the ini)
// ---------------------------------------------------------------------------

static char g_bookmarks[LICECAP_MAX_BOOKMARKS][BOOKMARK_PATH_LEN];
static int g_bookmark_count;
// Path of the folder used last, kept as a path (not an index) so reordering or
// removing other entries cannot turn it into a different folder.
static char g_bookmark_last[BOOKMARK_PATH_LEN];

// Read-only view of the list for the toolbar menu, so the application file
// never touches the storage directly.
int Bookmarks_Count(void)
{
  return g_bookmark_count;
}

const char *Bookmarks_Path(int idx)
{
  if (idx < 0 || idx >= g_bookmark_count) return "";
  return g_bookmarks[idx];
}

void Bookmarks_Load(void)
{
  int i;
  g_bookmark_count = 0;
  memset(g_bookmarks, 0, sizeof(g_bookmarks));
  g_bookmark_last[0] = 0;
  // A gap in the keys must not hide the rest of the list: read every bN and
  // compact the entries on the way in.
  for (i = 0; i < LICECAP_MAX_BOOKMARKS; i++)
  {
    char key[32], val[BOOKMARK_PATH_LEN];
    snprintf(key, sizeof(key), "b%d", i + 1);
    val[0] = 0;
    GetPrivateProfileString("bookmarks", key, "", val, sizeof(val), g_ini_file.Get());
    if (!val[0]) continue;
    lstrcpyn_safe(g_bookmarks[g_bookmark_count], val, BOOKMARK_PATH_LEN);
    g_bookmark_count++;
  }
  {
    char last[BOOKMARK_PATH_LEN];
    last[0] = 0;
    GetPrivateProfileString("bookmarks", "last", "", last, sizeof(last), g_ini_file.Get());
    // A stale path (the entry was removed by hand or by an older build) is
    // dropped instead of leaving the drop-down pointing at nothing.
    if (last[0] && Bookmarks_IndexOf(last) >= 0)
      lstrcpyn_safe(g_bookmark_last, last, BOOKMARK_PATH_LEN);
  }
}

// Rewrites the whole section so removed or reordered entries do not linger.
void Bookmarks_Save(void)
{
  int i;
  char key[32], val[32];
  snprintf(val, sizeof(val), "%d", g_bookmark_count);
  WritePrivateProfileString("bookmarks", "count", val, g_ini_file.Get());
  for (i = 0; i < LICECAP_MAX_BOOKMARKS; i++)
  {
    snprintf(key, sizeof(key), "b%d", i + 1);
    // NULL deletes the key, so the ini keeps no empty "bN=" leftovers
    WritePrivateProfileString("bookmarks", key,
        i < g_bookmark_count ? g_bookmarks[i] : NULL, g_ini_file.Get());
  }
  WritePrivateProfileString("bookmarks", "last", g_bookmark_last[0] ? g_bookmark_last : NULL,
                            g_ini_file.Get());
  // Force the file out now: the profile API keeps a per-process cache and a
  // crash or a second instance used to leave stale bookmarks on disk.
  WritePrivateProfileString(NULL, NULL, NULL, g_ini_file.Get());
}

// "E:\Proyectos\gif" -> "gif"; fall back to the path for drive roots.
const char *Bookmark_Label(const char *path)
{
  const char *p = WDL_get_filepart(path);
  if (p && *p) return p;
  return path;
}

void Bookmark_StripTrailingSlashes(char *p)
{
  int l = (int)strlen(p);
  // keep drive roots such as "C:\" intact
  while (l > 3 && (p[l - 1] == '\\' || p[l - 1] == '/')) p[--l] = 0;
}

const char *Bookmark_ShortPath(const char *path, char *buf, int bufsz)
{
  const int l = (int)strlen(path);
  if (bufsz < 8 || l < bufsz - 4)
  {
    lstrcpyn_safe(buf, path, bufsz);
    return buf;
  }
  snprintf(buf, bufsz, "...%s", path + (l - (bufsz - 4)));
  return buf;
}

int Bookmarks_IndexOf(const char *path)
{
  int i;
  for (i = 0; i < g_bookmark_count; i++)
    if (!stricmp(g_bookmarks[i], path)) return i;
  return -1;
}

// -1 only when the list is empty: a fresh list (or one from a build that did
// not store a selection yet) falls back to the first entry, which is also where
// Save As starts when there is no last save path.
int Bookmarks_LastUsed(void)
{
  if (!g_bookmark_count) return -1;
  if (!g_bookmark_last[0]) return 0;
  return Bookmarks_IndexOf(g_bookmark_last);
}

void Bookmarks_SetLastUsed(int idx)
{
  if (idx < 0 || idx >= g_bookmark_count)
  {
    if (!g_bookmark_last[0]) return;
    g_bookmark_last[0] = 0;
    Bookmarks_Save();
    return;
  }
  if (!stricmp(g_bookmark_last, g_bookmarks[idx])) return;
  lstrcpyn_safe(g_bookmark_last, g_bookmarks[idx], BOOKMARK_PATH_LEN);
  Bookmarks_Save();
}

// 0 added, 1 already there, 2 not a folder, 3 list full.
int Bookmarks_Add(const char *path)
{
  char tmp[BOOKMARK_PATH_LEN];
  DWORD attr;
  lstrcpyn_safe(tmp, path ? path : "", BOOKMARK_PATH_LEN);
  Bookmark_StripTrailingSlashes(tmp);
  if (!tmp[0]) return 2;
  attr = GetFileAttributesA(tmp);
  if (attr == INVALID_FILE_ATTRIBUTES || !(attr & FILE_ATTRIBUTE_DIRECTORY)) return 2;
  if (Bookmarks_IndexOf(tmp) >= 0) return 1;
  if (g_bookmark_count >= LICECAP_MAX_BOOKMARKS) return 3;
  lstrcpyn_safe(g_bookmarks[g_bookmark_count], tmp, BOOKMARK_PATH_LEN);
  g_bookmark_count++;
  // The folder just added becomes the drop-down selection: it is the one the
  // user was working with.
  lstrcpyn_safe(g_bookmark_last, tmp, BOOKMARK_PATH_LEN);
  Bookmarks_Save();
  return 0;
}

void Bookmarks_Remove(int idx)
{
  int i;
  if (idx < 0 || idx >= g_bookmark_count) return;
  for (i = idx; i < g_bookmark_count - 1; i++)
    lstrcpyn_safe(g_bookmarks[i], g_bookmarks[i + 1], BOOKMARK_PATH_LEN);
  g_bookmark_count--;
  memset(g_bookmarks[g_bookmark_count], 0, BOOKMARK_PATH_LEN);
  // Removing the selected folder moves the selection to its neighbour; the
  // path is resolved again so no other removal can shift it.
  if (g_bookmark_last[0] && Bookmarks_IndexOf(g_bookmark_last) < 0)
  {
    g_bookmark_last[0] = 0;
    if (g_bookmark_count)
      lstrcpyn_safe(g_bookmark_last,
                    g_bookmarks[idx < g_bookmark_count ? idx : g_bookmark_count - 1],
                    BOOKMARK_PATH_LEN);
  }
  Bookmarks_Save();
}

void Bookmarks_Move(int idx, int delta)
{
  const int j = idx + delta;
  char tmp[BOOKMARK_PATH_LEN];
  if (idx < 0 || idx >= g_bookmark_count || j < 0 || j >= g_bookmark_count) return;
  lstrcpyn_safe(tmp, g_bookmarks[idx], sizeof(tmp));
  lstrcpyn_safe(g_bookmarks[idx], g_bookmarks[j], BOOKMARK_PATH_LEN);
  lstrcpyn_safe(g_bookmarks[j], tmp, BOOKMARK_PATH_LEN);
  Bookmarks_Save();
}

// Folder of the last real save (temp recordings are not bookmark material).
bool Bookmark_LastSaveFolder(char *dst, int dstsz)
{
  const char *p;
  dst[0] = 0;
  if (!g_last_save_fn[0]) return false;
  lstrcpyn_safe(dst, g_last_save_fn, dstsz);
  p = WDL_get_filepart(dst);
  if (!p || p == dst) { dst[0] = 0; return false; }
  dst[p - dst] = 0;
  Bookmark_StripTrailingSlashes(dst);
  return dst[0] != 0;
}

#ifdef _WIN32
// Modern folder picker (IFileOpenDialog + FOS_PICKFOLDERS), with
// SHBrowseForFolder as a fallback if the shell dialog is unavailable.
bool Bookmark_PickFolder(HWND owner, const char *initialdir, char *out, int outsz)
{
  bool ok = false;
  bool com_inited = SUCCEEDED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED|COINIT_DISABLE_OLE1DDE));
  IFileOpenDialog *fd = NULL;
  out[0] = 0;

  if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, NULL, CLSCTX_INPROC_SERVER,
                                 IID_IFileOpenDialog, (void **)&fd)))
  {
    DWORD opts = 0;
    if (SUCCEEDED(fd->GetOptions(&opts)))
      fd->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_NOCHANGEDIR);
    fd->SetTitle(L"Choose a folder to bookmark");
    if (initialdir && initialdir[0])
    {
      wchar_t wdir[BOOKMARK_PATH_LEN];
      if (MultiByteToWideChar(CP_ACP, 0, initialdir, -1, wdir, BOOKMARK_PATH_LEN) > 0)
      {
        IShellItem *start = NULL;
        if (SUCCEEDED(SHCreateItemFromParsingName(wdir, NULL, IID_IShellItem, (void **)&start)))
        {
          fd->SetFolder(start);
          start->Release();
        }
      }
    }
    if (SUCCEEDED(fd->Show(owner)))
    {
      IShellItem *it = NULL;
      if (SUCCEEDED(fd->GetResult(&it)))
      {
        PWSTR wide = NULL;
        if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &wide)) && wide)
        {
          if (WideCharToMultiByte(CP_ACP, 0, wide, -1, out, outsz, NULL, NULL) > 0 && out[0])
            ok = true;
          CoTaskMemFree(wide);
        }
        it->Release();
      }
    }
    fd->Release();
  }

  if (!ok)
  {
    char path[MAX_PATH];
    BROWSEINFOA bi;
    memset(&bi, 0, sizeof(bi));
    bi.hwndOwner = owner;
    bi.lpszTitle = "Choose a folder to bookmark";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    PIDLIST_ABSOLUTE id = SHBrowseForFolderA(&bi);
    if (id)
    {
      if (SHGetPathFromIDListA(id, path))
      {
        lstrcpyn_safe(out, path, outsz);
        ok = out[0] != 0;
      }
      CoTaskMemFree(id);
    }
  }

  if (com_inited) CoUninitialize();
  return ok;
}
#else
bool Bookmark_PickFolder(HWND owner, const char *initialdir, char *out, int outsz)
{
  (void)owner; (void)initialdir; (void)outsz;
  out[0] = 0;
  return false;
}
#endif

// ---------------------------------------------------------------------------
// Blender-style bookmark panel: a small popup that holds the list and the
// + / browse / - / up / down buttons in its right column.
// ---------------------------------------------------------------------------

static HWND s_bmp_hwnd;
static HWND s_bmp_owner;
static int s_bmp_sel = -1;
static int s_bmp_top;
static int s_bmp_hotrow = -1;
static int s_bmp_hotbtn = -1;
bool s_bmp_modal;    // a nested dialog (message box, picker) is up
static bool s_bmp_closing;

enum { BMP_BTN_ADD, BMP_BTN_PICK, BMP_BTN_REMOVE, BMP_BTN_UP, BMP_BTN_DOWN, BMP_BTN_COUNT };

int BMP_Pad(void)    { return UI(6); }
int BMP_RowH(void)   { return UI(24); }
int BMP_BtnW(void)   { return UI(20); }
int BMP_BtnH(void)   { return UI(18); }
int BMP_BtnGap(void) { return UI(3); }
int BMP_TitleH(void) { return UI(18); }
int BMP_Width(void)  { return UI(250); }
int BMP_MinRows(void){ return 3; }
int BMP_MaxRows(void){ return 9; }

// Rows the panel is tall enough to show, independent of the bookmark count.
int BMP_VisibleRows(void)
{
  int n = g_bookmark_count;
  if (n < BMP_MinRows()) n = BMP_MinRows();
  if (n > BMP_MaxRows()) n = BMP_MaxRows();
  return n;
}

void BMP_ListRect(HWND w, RECT *list)
{
  RECT rc;
  GetClientRect(w,&rc);
  list->left = BMP_Pad();
  list->right = rc.right - BMP_Pad() - BMP_BtnW() - UI(6);
  list->top = BMP_Pad() + BMP_TitleH() + UI(4);
  list->bottom = rc.bottom - BMP_Pad();
}

void BMP_BtnRect(HWND w, int i, RECT *b)
{
  RECT rc;
  GetClientRect(w,&rc);
  b->right = rc.right - BMP_Pad();
  b->left = b->right - BMP_BtnW();
  b->top = BMP_Pad() + BMP_TitleH() + UI(4) + i * (BMP_BtnH() + BMP_BtnGap());
  b->bottom = b->top + BMP_BtnH();
}

void BMP_ClientSize(int *cw, int *ch)
{
  const int rows = BMP_VisibleRows();
  const int btns = BMP_BTN_COUNT * BMP_BtnH() + (BMP_BTN_COUNT - 1) * BMP_BtnGap();
  int body = rows * BMP_RowH();
  if (btns > body) body = btns;
  *cw = BMP_Width();
  *ch = BMP_Pad() + BMP_TitleH() + UI(4) + body + BMP_Pad();
}

bool BMP_BtnEnabled(int i)
{
  char dir[BOOKMARK_PATH_LEN];
  switch (i)
  {
    case BMP_BTN_ADD:    return Bookmark_LastSaveFolder(dir,sizeof(dir));
    case BMP_BTN_PICK:   return true;
    case BMP_BTN_REMOVE: return s_bmp_sel >= 0 && s_bmp_sel < g_bookmark_count;
    case BMP_BTN_UP:     return s_bmp_sel > 0;
    case BMP_BTN_DOWN:   return s_bmp_sel >= 0 && s_bmp_sel < g_bookmark_count - 1;
  }
  return false;
}

void BMP_Fill(HDC dc, const RECT *r, COLORREF c)
{
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc,r,b);
  DeleteObject(b);
}

void BMP_Frame(HDC dc, const RECT *r, COLORREF c)
{
  HPEN pen = CreatePen(PS_SOLID,1,c);
  HGDIOBJ op = SelectObject(dc,pen);
  HGDIOBJ ob = SelectObject(dc,GetStockObject(NULL_BRUSH));
  Rectangle(dc,r->left,r->top,r->right,r->bottom);
  SelectObject(dc,ob);
  SelectObject(dc,op);
  DeleteObject(pen);
}

void BMP_Folder(HDC dc, int x, int y, int sz, COLORREF c)
{
  Bookmark_FolderGlyph(dc,x,y,sz,c);
}

// Folder silhouette shared with the drop-down row under Save (ui_draw.cpp).
void Bookmark_FolderGlyph(HDC dc, int x, int y, int sz, COLORREF c)
{
  RECT tab, body;
  tab.left = x + 1;
  tab.right = x + sz/2 + 1;
  tab.top = y + sz/5;
  tab.bottom = y + sz/2;
  body.left = x;
  body.right = x + sz;
  body.top = y + sz/2 - 1;
  body.bottom = y + sz - 1;
  BMP_Fill(dc,&tab,c);
  BMP_Fill(dc,&body,c);
}

void BMP_Glyph(HDC dc, const RECT *b, int kind, COLORREF c)
{
  const int cx = (b->left + b->right) / 2;
  const int cy = (b->top + b->bottom) / 2;
  const int s = UI(3);

  if (kind == BMP_BTN_PICK)
  {
    BMP_Folder(dc,cx - UI(7),cy - UI(6),UI(13),c);
    return;
  }

  if (kind == BMP_BTN_UP || kind == BMP_BTN_DOWN)
  {
    POINT p[3];
    HBRUSH fb;
    HGDIOBJ ob, op;
    p[0].x = cx - s;
    p[0].y = kind == BMP_BTN_UP ? cy + UI(2) : cy - UI(2);
    p[1].x = cx + s;
    p[1].y = p[0].y;
    p[2].x = cx;
    p[2].y = kind == BMP_BTN_UP ? cy - UI(2) : cy + UI(2);
    fb = CreateSolidBrush(c);
    ob = SelectObject(dc,fb);
    op = SelectObject(dc,GetStockObject(NULL_PEN));
    Polygon(dc,p,3);
    SelectObject(dc,op);
    SelectObject(dc,ob);
    DeleteObject(fb);
    return;
  }

  {
    HPEN pen = CreatePen(PS_SOLID,1,c);
    HGDIOBJ op = SelectObject(dc,pen);
    MoveToEx(dc,cx-s,cy,NULL);
    LineTo(dc,cx+s+1,cy);
    if (kind == BMP_BTN_ADD)
    {
      MoveToEx(dc,cx,cy-s,NULL);
      LineTo(dc,cx,cy+s+1);
    }
    SelectObject(dc,op);
    DeleteObject(pen);
  }
}

void BMP_Paint(HWND w)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(w,&ps);
  RECT rc, list, tr;
  GetClientRect(w,&rc);

  BMP_Fill(dc,&rc,COL_BG);
  BMP_Frame(dc,&rc,COL_BORDER);

  {
    HFONT f = g_ui_font ? g_ui_font : (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    HGDIOBJ of = SelectObject(dc,f);
    SetBkMode(dc,TRANSPARENT);

    tr.left = rc.left + BMP_Pad() + UI(2);
    tr.right = rc.right - BMP_Pad();
    tr.top = rc.top + BMP_Pad() - UI(2);
    tr.bottom = tr.top + BMP_TitleH();
    SetTextColor(dc,COL_TEXT);
    DrawTextA(dc,"Bookmarks",-1,&tr,DT_LEFT|DT_VCENTER|DT_SINGLELINE);

    BMP_ListRect(w,&list);

    if (!g_bookmark_count)
    {
      RECT r = list;
      r.top += UI(4);
      SetTextColor(dc,COL_TEXT_DIS);
      DrawTextA(dc,"No bookmarks yet",-1,&r,DT_LEFT|DT_TOP|DT_SINGLELINE);
      r.top += UI(20);
      DrawTextA(dc,"+ adds the last save folder,",-1,&r,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
      r.top += UI(17);
      DrawTextA(dc,"the folder button browses for one.",-1,&r,DT_LEFT|DT_TOP|DT_SINGLELINE|DT_END_ELLIPSIS);
    }
    else
    {
      const int rowh = BMP_RowH();
      const int visible = (list.bottom - list.top) / rowh;
      int i;

      for (i = 0; i < visible; i++)
      {
        RECT r;
        int idx = s_bmp_top + i;
        if (idx >= g_bookmark_count) break;
        r.left = list.left;
        r.right = list.right;
        r.top = list.top + i * rowh;
        r.bottom = r.top + rowh;
        if (idx == s_bmp_sel) BMP_Fill(dc,&r,COL_BTN_DOWN);
        else if (idx == s_bmp_hotrow) BMP_Fill(dc,&r,COL_BTN_HOT);

        BMP_Folder(dc,r.left + UI(5),(r.top + r.bottom - UI(14)) / 2,UI(14),
                   idx == s_bmp_sel ? COL_TEXT : COL_TEXT_DIS);

        r.left += UI(25);
        r.right -= UI(3);
        SetTextColor(dc,COL_TEXT);
        DrawTextA(dc,Bookmark_Label(g_bookmarks[idx]),-1,&r,
                  DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      }

      if (g_bookmark_count > visible)
      {
        RECT trk, th;
        int h, y;
        const int span = g_bookmark_count - visible;
        trk.left = list.right + UI(1);
        trk.right = trk.left + UI(3);
        trk.top = list.top;
        trk.bottom = list.top + visible * rowh;
        BMP_Fill(dc,&trk,COL_BTN);
        h = (trk.bottom - trk.top) * visible / g_bookmark_count;
        if (h < UI(16)) h = UI(16);
        y = trk.top + (trk.bottom - trk.top - h) * s_bmp_top / (span ? span : 1);
        th.left = trk.left;
        th.right = trk.right;
        th.top = y;
        th.bottom = y + h;
        BMP_Fill(dc,&th,COL_TEXT_DIS);
      }
    }

    {
      int i;
      for (i = 0; i < BMP_BTN_COUNT; i++)
      {
        RECT b;
        const bool en = BMP_BtnEnabled(i);
        const bool hot = en && s_bmp_hotbtn == i;
        BMP_BtnRect(w,i,&b);
        BMP_Fill(dc,&b,hot ? COL_BTN_HOT : COL_BTN);
        BMP_Frame(dc,&b,hot ? COL_BORDER_HOT : COL_BORDER);
        BMP_Glyph(dc,&b,i,en ? (hot ? COL_TEXT_HOT : COL_TEXT) : COL_TEXT_DIS);
      }
    }

    SelectObject(dc,of);
  }

  EndPaint(w,&ps);
}

int BMP_HitRow(HWND w, POINT pt)
{
  RECT list;
  int rowh, visible, i;
  BMP_ListRect(w,&list);
  if (!PtInRect(&list,pt)) return -1;
  rowh = BMP_RowH();
  visible = (list.bottom - list.top) / rowh;
  i = (pt.y - list.top) / rowh;
  if (i < 0 || i >= visible) return -1;
  i += s_bmp_top;
  return i < g_bookmark_count ? i : -1;
}

int BMP_HitBtn(HWND w, POINT pt)
{
  int i;
  for (i = 0; i < BMP_BTN_COUNT; i++)
  {
    RECT b;
    BMP_BtnRect(w,i,&b);
    if (PtInRect(&b,pt)) return i;
  }
  return -1;
}

void BookmarkPanel_Close(void)
{
  HWND w = s_bmp_hwnd;
  if (!w) return;
  s_bmp_hwnd = NULL;
  s_bmp_closing = true;
  if (GetCapture() == w) ReleaseCapture();
  DestroyWindow(w);
  s_bmp_closing = false;
  s_bmp_modal = false;
  s_bmp_hotrow = -1;
  s_bmp_hotbtn = -1;
}

void BookmarkPanel_Refresh(void)
{
  int cw, ch;
  if (!s_bmp_hwnd) return;
  if (s_bmp_sel >= g_bookmark_count) s_bmp_sel = g_bookmark_count - 1;
  if (s_bmp_sel < -1) s_bmp_sel = -1;
  if (s_bmp_top > g_bookmark_count - BMP_VisibleRows()) s_bmp_top = g_bookmark_count - BMP_VisibleRows();
  if (s_bmp_top < 0) s_bmp_top = 0;
  BMP_ClientSize(&cw,&ch);
  SetWindowPos(s_bmp_hwnd,NULL,0,0,cw,ch,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
  InvalidateRect(s_bmp_hwnd,NULL,FALSE);
}

// Keeps the selected row inside the visible part of the list.
void BMP_EnsureVisible(void)
{
  const int visible = BMP_VisibleRows();
  if (s_bmp_sel < s_bmp_top) s_bmp_top = s_bmp_sel;
  if (s_bmp_sel >= s_bmp_top + visible) s_bmp_top = s_bmp_sel - visible + 1;
  if (s_bmp_top < 0) s_bmp_top = 0;
}

// Runs a bookmark command on the main window, surviving the modal dialogs it
// may open (message box, folder picker).
void BMP_Send(int cmd)
{
  if (!s_bmp_owner) return;
  // The modal flag also covers the focus/capture restore: SetFocus can make
  // this window lose the capture for a moment and that must not close it.
  s_bmp_modal = true;
  SendMessage(s_bmp_owner,WM_COMMAND,MAKEWPARAM(cmd,0),0);
  if (s_bmp_hwnd)
  {
    SetFocus(s_bmp_hwnd);
    SetCapture(s_bmp_hwnd);
  }
  s_bmp_modal = false;
}

void BMP_PressButton(int i)
{
  const int sel = s_bmp_sel;
  switch (i)
  {
    case BMP_BTN_ADD:
    case BMP_BTN_PICK:
      BMP_Send(i == BMP_BTN_ADD ? IDM_BOOKMARK_ADD : IDM_BOOKMARK_PICK);
      if (g_bookmark_count)
      {
        s_bmp_sel = g_bookmark_count - 1;
        BMP_EnsureVisible();
      }
    break;
    case BMP_BTN_REMOVE:
      if (g_bookmark_count)
      {
        // Only act on a real row: with no selection fall back to the last one
        // instead of swallowing the click.
        int idx = (sel >= 0 && sel < g_bookmark_count) ? sel : g_bookmark_count - 1;
        s_bmp_sel = idx < g_bookmark_count - 1 ? idx : idx - 1;
        BMP_Send(IDM_BOOKMARK_REMOVE_FIRST + idx);
      }
    break;
    case BMP_BTN_UP:
      if (sel > 0)
      {
        s_bmp_sel = sel - 1;
        BMP_Send(IDM_BOOKMARK_UP_FIRST + sel);
      }
    break;
    case BMP_BTN_DOWN:
      if (sel >= 0 && sel < g_bookmark_count - 1)
      {
        s_bmp_sel = sel + 1;
        BMP_Send(IDM_BOOKMARK_DOWN_FIRST + sel);
      }
    break;
  }
  BookmarkPanel_Refresh();
}

LRESULT CALLBACK BMP_WndProc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
  switch (msg)
  {
    case WM_ERASEBKGND:
      return 1;

    case WM_PAINT:
      BMP_Paint(w);
      return 0;

    case WM_MOUSEACTIVATE:
      return MA_ACTIVATE;

    case WM_SETCURSOR:
      if (LOWORD(lp) == HTCLIENT) SetCursor(LoadCursor(NULL,IDC_ARROW));
      return TRUE;

    case WM_MOUSEMOVE:
    {
      POINT pt;
      int row, btn;
      pt.x = (short)LOWORD(lp);
      pt.y = (short)HIWORD(lp);
      row = BMP_HitRow(w,pt);
      btn = BMP_HitBtn(w,pt);
      if (row != s_bmp_hotrow || btn != s_bmp_hotbtn)
      {
        s_bmp_hotrow = row;
        s_bmp_hotbtn = btn;
        InvalidateRect(w,NULL,FALSE);
      }
    }
    return 0;

    case WM_LBUTTONDOWN:
    {
      POINT pt;
      RECT rc;
      int row, btn;
      pt.x = (short)LOWORD(lp);
      pt.y = (short)HIWORD(lp);
      GetClientRect(w,&rc);
      if (!PtInRect(&rc,pt))
      {
        BookmarkPanel_Close();
        return 0;
      }
      btn = BMP_HitBtn(w,pt);
      if (btn >= 0)
      {
        if (BMP_BtnEnabled(btn)) BMP_PressButton(btn);
        return 0;
      }
      row = BMP_HitRow(w,pt);
      if (row >= 0)
      {
        s_bmp_sel = row;
        s_bmp_hotrow = row;
        InvalidateRect(w,NULL,FALSE);
        if (g_pending_unsaved)
        {
          BMP_Send(IDM_BOOKMARK_FIRST + row);
          if (!g_pending_unsaved) BookmarkPanel_Close();   // saved, nothing left
          else BookmarkPanel_Refresh();
        }
        else
        {
          // Nothing to save: the click just picks the destination, so the
          // drop-down under Save shows it from now on.
          HWND owner = s_bmp_owner;
          Bookmarks_SetLastUsed(row);
          if (owner) InvalidateRect(GetDlgItem(owner,IDC_BOOKMARKDROP),NULL,FALSE);
          BookmarkPanel_Close();
        }
      }
    }
    return 0;

    case WM_MOUSEWHEEL:
    {
      const int visible = BMP_VisibleRows();
      const int delta = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? -1 : 1;
      const int before = s_bmp_top;
      s_bmp_top += delta;
      if (s_bmp_top > g_bookmark_count - visible) s_bmp_top = g_bookmark_count - visible;
      if (s_bmp_top < 0) s_bmp_top = 0;
      if (s_bmp_top != before) InvalidateRect(w,NULL,FALSE);
    }
    return 0;

    case WM_KEYDOWN:
    {
      const int visible = BMP_VisibleRows();
      switch (wp)
      {
        case VK_ESCAPE:
          BookmarkPanel_Close();
        return 0;

        case VK_UP:
          if (s_bmp_sel > 0) s_bmp_sel--;
          else if (s_bmp_sel < 0 && g_bookmark_count) s_bmp_sel = 0;
          BMP_EnsureVisible();
          InvalidateRect(w,NULL,FALSE);
        return 0;

        case VK_DOWN:
          if (s_bmp_sel < g_bookmark_count - 1) s_bmp_sel++;
          else if (s_bmp_sel < 0 && g_bookmark_count) s_bmp_sel = 0;
          BMP_EnsureVisible();
          InvalidateRect(w,NULL,FALSE);
        return 0;

        case VK_PRIOR:
          s_bmp_top -= visible;
          if (s_bmp_top < 0) s_bmp_top = 0;
          s_bmp_sel = s_bmp_top;
          InvalidateRect(w,NULL,FALSE);
        return 0;

        case VK_NEXT:
          s_bmp_top += visible;
          if (s_bmp_top > g_bookmark_count - visible) s_bmp_top = g_bookmark_count - visible;
          if (s_bmp_top < 0) s_bmp_top = 0;
          s_bmp_sel = s_bmp_top + visible - 1;
          if (s_bmp_sel >= g_bookmark_count) s_bmp_sel = g_bookmark_count - 1;
          InvalidateRect(w,NULL,FALSE);
        return 0;

        case VK_RETURN:
          if (s_bmp_sel >= 0 && s_bmp_sel < g_bookmark_count)
          {
            if (g_pending_unsaved)
            {
              BMP_Send(IDM_BOOKMARK_FIRST + s_bmp_sel);
              if (!g_pending_unsaved) BookmarkPanel_Close();
              else BookmarkPanel_Refresh();
            }
            else
            {
              const int sel = s_bmp_sel;
              HWND owner = s_bmp_owner;
              Bookmarks_SetLastUsed(sel);
              if (owner) InvalidateRect(GetDlgItem(owner,IDC_BOOKMARKDROP),NULL,FALSE);
              BookmarkPanel_Close();
            }
          }
        return 0;

        case VK_DELETE:
          if (BMP_BtnEnabled(BMP_BTN_REMOVE)) BMP_PressButton(BMP_BTN_REMOVE);
        return 0;

        case VK_INSERT:
          if (BMP_BtnEnabled(BMP_BTN_ADD)) BMP_PressButton(BMP_BTN_ADD);
        return 0;
      }
    }
    return 0;

    // The panel stays open while the user works on it: only a click that the
    // capture sends us from outside, Esc, or losing the whole app closes it.
    case WM_ACTIVATE:
      if (LOWORD(wp) == WA_INACTIVE && !s_bmp_modal && !s_bmp_closing)
      {
        HWND a = GetActiveWindow();
        DWORD pid = 0;
        if (a) GetWindowThreadProcessId(a,&pid);
        if (!a || pid != GetCurrentProcessId()) BookmarkPanel_Close();
      }
    return 0;

    case WM_CAPTURECHANGED:
      if (!s_bmp_closing && !s_bmp_modal) BookmarkPanel_Close();
    return 0;

    case WM_DESTROY:
      if (s_bmp_hwnd == w) s_bmp_hwnd = NULL;
    return 0;
  }
  return DefWindowProcA(w,msg,wp,lp);
}

static bool s_bmp_registered;

void BookmarkPanel_Show(HWND hwndDlg)
{
  int cw, ch, x, y;
  RECT br;
  HWND b;
  static const char *cls = "LiceBookmarkPanel";

  if (s_bmp_hwnd)
  {
    BookmarkPanel_Close();
    return;
  }

  if (!s_bmp_registered)
  {
    WNDCLASSA wc;
    memset(&wc,0,sizeof(wc));
    wc.lpfnWndProc = BMP_WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursor(NULL,IDC_ARROW);
    wc.lpszClassName = cls;
    wc.style = CS_DROPSHADOW;
    if (!RegisterClassA(&wc)) return;
    s_bmp_registered = true;
  }

  BMP_ClientSize(&cw,&ch);
  // The list hangs from the drop-down row under Save, which is where the
  // bookmarks live now; older layouts without that control fall back to Save.
  b = GetDlgItem(hwndDlg,IDC_BOOKMARKDROP);
  if (!b) b = GetDlgItem(hwndDlg,IDC_SAVE);
  if (b && GetWindowRect(b,&br))
  {
    x = br.right - cw;
    y = br.bottom + UI(2);
  }
  else
  {
    POINT p;
    GetCursorPos(&p);
    x = p.x - cw / 2;
    y = p.y;
  }

  {
    RECT self, work;
    GetWindowRect(hwndDlg,&self);
    my_getViewport(&work,&self,true);
    if (x + cw > work.right) x = work.right - cw;
    if (x < work.left) x = work.left;
    if (y + ch > work.bottom)
    {
      // no room below the Save button: open upwards
      if (b && GetWindowRect(b,&br)) y = br.top - ch - UI(2);
      if (y < work.top) y = work.top;
    }
  }

  s_bmp_owner = hwndDlg;
  // Open with the folder used last already selected (and scrolled into view),
  // instead of always landing on the first row.
  s_bmp_sel = Bookmarks_LastUsed();
  if (s_bmp_sel < 0) s_bmp_sel = g_bookmark_count ? 0 : -1;
  s_bmp_top = 0;
  BMP_EnsureVisible();
  s_bmp_hotrow = -1;
  s_bmp_hotbtn = -1;
  s_bmp_modal = false;

  s_bmp_hwnd = CreateWindowExA(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls,"",WS_POPUP,
                               x,y,cw,ch,hwndDlg,NULL,GetModuleHandleA(NULL),NULL);
  if (!s_bmp_hwnd) return;

  ShowWindow(s_bmp_hwnd,SW_SHOWNA);
  SetFocus(s_bmp_hwnd);
  SetCapture(s_bmp_hwnd);
  InvalidateRect(s_bmp_hwnd,NULL,FALSE);
}
