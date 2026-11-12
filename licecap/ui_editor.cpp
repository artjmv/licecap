/*
    ui_editor.cpp - timeline trim editor for the pending recording or an opened
    GIF (fork-only).

    Opening the editor replaces the capture hole with a preview of the GIF and
    adds a strip at the bottom of the capture area with:

      - play/pause,
      - a timeline with one tick per frame,
      - one or more kept ranges, each with draggable A/B handles, so several
        pieces of the recording can be joined: [A,B] + [C,D] + ...,
      - "+" to add a range (free space after the playhead, or a split of the
        range under it when everything is still kept) and "-" to drop the
        selected one,
      - Apply (rewrite the recording with only the kept ranges, in order) and
        Close.

    Ranges are kept sorted and never overlap: dragging a handle into a
    neighbour pulls the neighbour back, and a range left with no frames is
    dropped. Only the selected range shows the A/B flags; the others are drawn
    dimmer with plain bars.

    The window keeps painting itself (custom caption + region), so everything is
    drawn from Editor_Paint and hit-tested here. The GIF itself is read and
    written by ui_gifedit.cpp.

    The editor always works on the pending recording (g_pending_fn). A GIF
    opened from disk is copied to a temporary file that becomes that pending
    recording (see OpenGifInEditor in licecap_ui.cpp), so Apply never rewrites
    the file the user opened.
*/

#include "licecap_ui.h"
#include "../WDL/wdlcstring.h"

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------
#define EDIT_MAX_RANGES 12

static void *s_gif;
static bool s_open;
static int s_cur;                          // frame under the playhead
static int s_ra[EDIT_MAX_RANGES];           // inclusive kept ranges, sorted
static int s_rb[EDIT_MAX_RANGES];
static int s_nr;                            // ranges in use
static int s_sel;                           // selected range (owns the A/B handles)
static bool s_play;
static DWORD s_next_tick;
static int s_drag;                          // 0 none, 1 = A, 2 = B, 3 = playhead
static int s_hot;                           // hovered element, for the button highlight
static char s_src[2048];

#define SEG_A s_ra[s_sel]
#define SEG_B s_rb[s_sel]

struct EDITLAYOUT
{
  RECT preview;
  RECT strip;
  RECT track;
  RECT play;
  RECT add;
  RECT del;
  RECT apply;
  RECT close;
};

// ---------------------------------------------------------------------------
// helpers
// ---------------------------------------------------------------------------
static void edit_fill(HDC dc, const RECT *r, COLORREF c)
{
  HBRUSH b = CreateSolidBrush(c);
  FillRect(dc, r, b);
  DeleteObject(b);
}

static void edit_frame(HDC dc, const RECT *r, COLORREF c)
{
  HPEN p = CreatePen(PS_SOLID, 1, c);
  HGDIOBJ op = SelectObject(dc, p);
  HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
  Rectangle(dc, r->left, r->top, r->right, r->bottom);
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(p);
}

static void edit_round(HDC dc, const RECT *r, COLORREF fill, COLORREF border)
{
  HBRUSH b = CreateSolidBrush(fill);
  HPEN p = CreatePen(PS_SOLID, 1, border);
  HGDIOBJ ob = SelectObject(dc, b);
  HGDIOBJ op = SelectObject(dc, p);
  RoundRect(dc, r->left, r->top, r->right, r->bottom, UI(6), UI(6));
  SelectObject(dc, ob);
  SelectObject(dc, op);
  DeleteObject(b);
  DeleteObject(p);
}

static void edit_text(HDC dc, const RECT *r, const char *text, COLORREF col, HFONT f, UINT fmt)
{
  SetBkMode(dc, TRANSPARENT);
  SetTextColor(dc, col);
  HGDIOBJ of = SelectObject(dc, f ? f : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
  RECT tr = *r;
  DrawTextA(dc, text, -1, &tr, fmt | DT_SINGLELINE | DT_NOPREFIX);
  SelectObject(dc, of);
}

// blend b into a; pcta is the weight of a in percent
static COLORREF edit_blend(COLORREF a, COLORREF b, int pcta)
{
  if (pcta < 0) pcta = 0;
  if (pcta > 100) pcta = 100;
  return RGB((GetRValue(a) * pcta + GetRValue(b) * (100 - pcta)) / 100,
             (GetGValue(a) * pcta + GetGValue(b) * (100 - pcta)) / 100,
             (GetBValue(a) * pcta + GetBValue(b) * (100 - pcta)) / 100);
}

static void edit_time_str(int ms, char *buf, int bufsz)
{
  if (ms < 0) ms = 0;
  const int tenths = (ms + 50) / 100;
  snprintf(buf, bufsz, "%d:%02d.%d", tenths / 600, (tenths / 10) % 60, tenths % 10);
}

static int edit_frames(void) { return s_gif ? GIFEdit_Frames(s_gif) : 0; }

static int edit_total_ms(void)
{
  int t = s_gif ? GIFEdit_TotalMs(s_gif) : 0;
  return t > 0 ? t : 1;
}

static int edit_rng_start_ms(int k)
{
  return GIFEdit_StartMs(s_gif, s_ra[k]);
}

static int edit_rng_end_ms(int k)
{
  if (!s_gif) return 0;
  return GIFEdit_StartMs(s_gif, s_rb[k]) + GIFEdit_DelayMs(s_gif, s_rb[k]);
}

static int edit_rng_ms(int k)
{
  const int ms = edit_rng_end_ms(k) - edit_rng_start_ms(k);
  return ms > 0 ? ms : 0;
}

// total duration of everything that Apply would keep
static int edit_kept_ms(void)
{
  int ms = 0, k;
  for (k = 0; k < s_nr; k++) ms += edit_rng_ms(k);
  return ms;
}

// index of the kept range holding a frame, -1 when the frame is dropped
static int edit_rng_at(int frame)
{
  int k;
  for (k = 0; k < s_nr; k++)
    if (frame >= s_ra[k] && frame <= s_rb[k]) return k;
  return -1;
}

static int edit_first_kept(int from)
{
  const int n = edit_frames();
  int k;
  if (from < 0) from = 0;
  for (k = from; k < n; k++)
    if (edit_rng_at(k) >= 0) return k;
  for (k = 0; k < n; k++)
    if (edit_rng_at(k) >= 0) return k;
  return from < n ? from : 0;
}

static int edit_next_kept(int from)
{
  const int n = edit_frames();
  int k;
  for (k = from + 1; k < n; k++)
    if (edit_rng_at(k) >= 0) return k;
  for (k = 0; k < n; k++)
    if (edit_rng_at(k) >= 0) return k;
  return from;
}

static int edit_x_at_ms(const RECT *track, int ms)
{
  const int span = track->right - track->left;
  int x = track->left + (int)((double)ms * (double)span / (double)edit_total_ms() + 0.5);
  if (x < track->left) x = track->left;
  if (x > track->right) x = track->right;
  return x;
}

static int edit_ms_at_x(const RECT *track, int x)
{
  const int span = track->right - track->left;
  if (span <= 0) return 0;
  int ms = (int)((double)(x - track->left) * (double)edit_total_ms() / (double)span + 0.5);
  if (ms < 0) ms = 0;
  if (ms > edit_total_ms()) ms = edit_total_ms();
  return ms;
}

static int edit_frame_at_x(const RECT *track, int x)
{
  return GIFEdit_IndexAtMs(s_gif, edit_ms_at_x(track, x));
}

// ---------------------------------------------------------------------------
// geometry
// ---------------------------------------------------------------------------
static void edit_content(HWND hwndDlg, RECT *r)
{
  RECT rc;
  GetClientRect(hwndDlg, &rc);
  const int tw = UI(UI_BTN_COL_W);
  r->left = UI(1);
  r->top = UI_CaptionHeight() + UI(1);
  r->right = rc.right - tw - UI(2);
  r->bottom = rc.bottom - UI(1);
  if (r->right < r->left + UI(64)) r->right = rc.right - UI(2);
  if (r->bottom < r->top + UI(80)) r->bottom = r->top + UI(80);
}

static void edit_layout(HWND hwndDlg, EDITLAYOUT *l)
{
  RECT c;
  edit_content(hwndDlg, &c);

  const int sh = UI(64);
  l->strip = c;
  l->strip.top = c.bottom - sh;
  if (l->strip.top < c.top + UI(20)) l->strip.top = c.top + UI(20);

  l->preview = c;
  l->preview.bottom = l->strip.top > c.top ? l->strip.top - UI(1) : c.top;

  const int pad = UI(6);
  const int bh = UI(20);
  const int rowb = l->strip.top + UI(8);

  l->play.left = l->strip.left + pad;
  l->play.top = rowb;
  l->play.right = l->play.left + UI(24);
  l->play.bottom = rowb + bh;

  l->add.left = l->play.right + UI(4);
  l->add.top = rowb;
  l->add.right = l->add.left + UI(20);
  l->add.bottom = rowb + bh;

  l->del.left = l->add.right + UI(2);
  l->del.top = rowb;
  l->del.right = l->del.left + UI(20);
  l->del.bottom = rowb + bh;

  l->close.right = l->strip.right - pad;
  l->close.left = l->close.right - UI(44);
  l->close.top = rowb;
  l->close.bottom = rowb + bh;

  l->apply.right = l->close.left - UI(6);
  l->apply.left = l->apply.right - UI(52);
  l->apply.top = rowb;
  l->apply.bottom = rowb + bh;

  l->track.left = l->del.right + UI(8);
  l->track.right = l->apply.left - UI(8);
  if (l->track.right < l->track.left + UI(24)) l->track.right = l->track.left + UI(24);
  l->track.top = l->strip.top + UI(20);
  l->track.bottom = l->strip.top + UI(32);
}

static int edit_handle_x(const EDITLAYOUT *l, int range, int which)
{
  return which == 1 ? edit_x_at_ms(&l->track, edit_rng_start_ms(range))
                    : edit_x_at_ms(&l->track, edit_rng_end_ms(range));
}

// true when x is on the given handle (tolerance is generous: they are 2px wide)
static bool edit_on_handle(const EDITLAYOUT *l, int range, int which, int x)
{
  return abs(x - edit_handle_x(l, range, which)) <= UI(6);
}

// First handle under x. The selected range is tested first so its handles keep
// the clicks when two ranges touch.
static bool edit_handle_hit(const EDITLAYOUT *l, int x, int *range, int *which)
{
  int k;
  if (s_sel >= 0 && s_sel < s_nr)
  {
    if (edit_on_handle(l, s_sel, 1, x)) { *range = s_sel; *which = 1; return true; }
    if (edit_on_handle(l, s_sel, 2, x)) { *range = s_sel; *which = 2; return true; }
  }
  for (k = 0; k < s_nr; k++)
  {
    if (k == s_sel) continue;
    if (edit_on_handle(l, k, 1, x)) { *range = k; *which = 1; return true; }
    if (edit_on_handle(l, k, 2, x)) { *range = k; *which = 2; return true; }
  }
  return false;
}

static bool edit_any_handle(const EDITLAYOUT *l, int x)
{
  int r, w;
  return edit_handle_hit(l, x, &r, &w);
}

// ---------------------------------------------------------------------------
// playback
// ---------------------------------------------------------------------------
// The toolbar greys out while the timeline is open (ButtonIsLive), but the
// owner-drawn buttons only repaint when asked to.
static void edit_repaint_buttons(HWND hwndDlg)
{
  static const int ids[] = { IDC_REC, IDC_EDITBTN, IDC_SAVE, IDC_EDITMODE, IDC_PRESETS };
  int i;
  for (i = 0; i < (int)(sizeof(ids)/sizeof(ids[0])); i++)
  {
    HWND b = GetDlgItem(hwndDlg, ids[i]);
    if (b) InvalidateRect(b, NULL, TRUE);
  }
}

static void edit_invalidate(HWND hwndDlg)
{
  EDITLAYOUT l;
  edit_layout(hwndDlg, &l);
  InvalidateRect(hwndDlg, &l.preview, FALSE);
  InvalidateRect(hwndDlg, &l.strip, FALSE);
}

static void edit_set_cur(HWND hwndDlg, int frame)
{
  const int n = edit_frames();
  if (n <= 0) return;
  if (frame < 0) frame = 0;
  if (frame >= n) frame = n - 1;
  s_cur = frame;
  edit_invalidate(hwndDlg);
}

static void edit_toggle_play(HWND hwndDlg)
{
  if (edit_frames() <= 0) return;
  s_play = !s_play;
  if (s_play)
  {
    if (edit_rng_at(s_cur) < 0) s_cur = edit_first_kept(s_cur);
    s_next_tick = timeGetTime() + (DWORD)wdl_max(10, GIFEdit_DelayMs(s_gif, s_cur));
  }
  edit_invalidate(hwndDlg);
}

static void edit_seek_ms(HWND hwndDlg, int ms)
{
  edit_set_cur(hwndDlg, GIFEdit_IndexAtMs(s_gif, ms));
}

// ---------------------------------------------------------------------------
// ranges
// ---------------------------------------------------------------------------
// Move one handle of the selected range to frame f. A range never overlaps its
// neighbours: pushing into one pulls it back, and a range left with no frames
// is dropped.
static void edit_set_handle(HWND hwndDlg, int which, int f)
{
  const int n = edit_frames();
  if (n <= 0 || s_nr <= 0) return;
  if (f < 0) f = 0;
  if (f > n - 1) f = n - 1;

  if (which == 1)
  {
    if (s_sel > 0 && f <= s_rb[s_sel - 1])
    {
      s_rb[s_sel - 1] = f - 1;
      if (s_rb[s_sel - 1] < s_ra[s_sel - 1])
      {
        int k;
        for (k = s_sel - 1; k < s_nr - 1; k++) { s_ra[k] = s_ra[k + 1]; s_rb[k] = s_rb[k + 1]; }
        s_nr--;
        s_sel--;
      }
    }
    if (f > SEG_B) f = SEG_B;
    SEG_A = f;
  }
  else
  {
    if (s_sel + 1 < s_nr && f >= s_ra[s_sel + 1])
    {
      s_ra[s_sel + 1] = f + 1;
      if (s_ra[s_sel + 1] > s_rb[s_sel + 1])
      {
        int k;
        for (k = s_sel + 1; k < s_nr - 1; k++) { s_ra[k] = s_ra[k + 1]; s_rb[k] = s_rb[k + 1]; }
        s_nr--;
      }
    }
    if (f < SEG_A) f = SEG_A;
    SEG_B = f;
  }

  s_cur = f;
  edit_invalidate(hwndDlg);
}

static int edit_insert_range(int pos, int a, int b)
{
  if (s_nr >= EDIT_MAX_RANGES) return -1;
  if (pos < 0) pos = 0;
  if (pos > s_nr) pos = s_nr;

  int k;
  for (k = s_nr; k > pos; k--) { s_ra[k] = s_ra[k - 1]; s_rb[k] = s_rb[k - 1]; }
  s_ra[pos] = a;
  s_rb[pos] = b;
  s_nr++;
  return pos;
}

// "+": a new range in the first stretch of dropped frames (from the playhead
// on, then from the start); when the whole recording is still kept, the range
// under the playhead is split instead so the new one can be shaped.
static bool edit_add_range(HWND hwndDlg)
{
  const int n = edit_frames();
  if (n <= 0) return false;

  if (s_nr >= EDIT_MAX_RANGES)
  {
    char msg[256];
    snprintf(msg, sizeof(msg),
             "The editor already holds %d ranges. Remove one with the \"-\" button.",
             EDIT_MAX_RANGES);
    MessageBoxA(hwndDlg, msg, "LICEcap", MB_OK | MB_ICONINFORMATION);
    return false;
  }

  int start = -1, k;
  for (k = s_cur; k < n && start < 0; k++)
    if (edit_rng_at(k) < 0) start = k;
  for (k = 0; k < s_cur && start < 0; k++)
    if (edit_rng_at(k) < 0) start = k;

  int a, b, pos;
  if (start >= 0)
  {
    b = start;
    while (b + 1 < n && edit_rng_at(b + 1) < 0) b++;
    a = start;
    pos = 0;
    for (k = 0; k < s_nr; k++)
      if (s_ra[k] < a) pos = k + 1;
  }
  else
  {
    // Everything is kept: split the range under the playhead at the playhead.
    int idx = edit_rng_at(s_cur);
    if (idx < 0) idx = s_sel;
    if (idx < 0 || idx >= s_nr) return false;

    const int oldb = s_rb[idx];
    a = s_cur;
    if (a <= s_ra[idx]) a = s_ra[idx] + (oldb - s_ra[idx] + 1) / 2;
    if (a > oldb) a = oldb;

    if (a <= s_ra[idx])
    {
      MessageBoxA(hwndDlg,
        "Every frame is already in the selection.\n\n"
        "Drag a handle to open a gap first, or press \"+\" after trimming.",
        "LICEcap", MB_OK | MB_ICONINFORMATION);
      return false;
    }

    s_rb[idx] = a - 1;
    b = oldb;
    pos = idx + 1;
  }

  pos = edit_insert_range(pos, a, b);
  if (pos < 0) return false;

  s_sel = pos;
  s_cur = a;
  s_play = false;
  edit_invalidate(hwndDlg);
  return true;
}

// "-": drop the selected range. With a single range it means "keep everything",
// which is the only way back from a fully trimmed recording.
static bool edit_remove_range(HWND hwndDlg)
{
  const int n = edit_frames();
  if (n <= 0 || s_nr <= 0) return false;

  if (s_nr > 1)
  {
    int k;
    for (k = s_sel; k < s_nr - 1; k++) { s_ra[k] = s_ra[k + 1]; s_rb[k] = s_rb[k + 1]; }
    s_nr--;
    if (s_sel >= s_nr) s_sel = s_nr - 1;
    if (s_rb[s_sel] < s_ra[s_sel]) { s_ra[s_sel] = 0; s_rb[s_sel] = n - 1; }
  }
  else
  {
    s_sel = 0;
    s_ra[0] = 0;
    s_rb[0] = n - 1;
  }

  s_play = false;
  edit_invalidate(hwndDlg);
  return true;
}

static void edit_reset_ranges(void)
{
  const int n = edit_frames();
  s_nr = 1;
  s_sel = 0;
  s_ra[0] = 0;
  s_rb[0] = n > 0 ? n - 1 : 0;
}

// ---------------------------------------------------------------------------
// public: mode
// ---------------------------------------------------------------------------
bool Editor_Active(void)
{
  return s_open;
}

// Each worker owns its GIF model. In particular Apply must NOT mutate s_gif:
// the UI continues painting the preview while the output is encoded.
struct EditorLoadJob { const char *path; void *gif; };
static bool Editor_LoadWorker(void *ctx)
{
  EditorLoadJob *job = (EditorLoadJob *)ctx;
  job->gif = GIFEdit_Load(job->path);
  return job->gif != NULL;
}
struct EditorWriteJob
{
  const char *src, *dst;
  const int *firsts, *lasts;
  int count;
};
static bool Editor_WriteWorker(void *ctx)
{
  EditorWriteJob *job = (EditorWriteJob *)ctx;
  void *gif = GIFEdit_Load(job->src);
  if (!gif) return false;
  const bool ok = GIFEdit_WriteRanges(gif, job->firsts, job->lasts, job->count, job->dst);
  GIFEdit_Free(gif);
  return ok;
}

bool Editor_Open(HWND hwndDlg)
{
  if (s_open || g_cap_state || Busy_Active()) return false;

  if (!g_pending_unsaved || !g_pending_fn[0])
  {
    MessageBoxA(hwndDlg,
      "Record something first, or open a GIF from disk.\n\nEdit works on the last "
      "recording that has not been saved yet (the one Save moves to a folder), or "
      "on a GIF opened with Open GIF to Edit (Ctrl+O).",
      "LICEcap", MB_OK | MB_ICONINFORMATION);
    return false;
  }

  EditorLoadJob job = {g_pending_fn, NULL};
  Busy_Run(hwndDlg, "Opening GIF...", 0, Editor_LoadWorker, &job);
  s_gif = job.gif;

  if (!s_gif)
  {
    MessageBoxA(hwndDlg, "Could not read that GIF.", "LICEcap",
                MB_OK | MB_ICONERROR);
    return false;
  }

  lstrcpyn_safe(s_src, g_pending_fn, sizeof(s_src));
  s_open = true;
  s_play = false;
  s_drag = 0;
  s_hot = 0;
  s_cur = 0;
  edit_reset_ranges();

  LayoutControls(hwndDlg);
#ifdef _WIN32
  ApplyWindowRegion(hwndDlg);
  // keep focus off the toolbar so space/arrows belong to the timeline
  SetFocus(hwndDlg);
#endif
  SetTimer(hwndDlg, 2, 15, NULL);
  InvalidateRect(hwndDlg, NULL, TRUE);
  edit_repaint_buttons(hwndDlg);
  UpdateDimBoxes(hwndDlg);
  return true;
}

void Editor_Close(HWND hwndDlg)
{
  if (!s_open) return;
#ifdef _WIN32
  if (GetCapture() == hwndDlg) ReleaseCapture();
#endif

  KillTimer(hwndDlg, 2);
  GIFEdit_Free(s_gif);
  s_gif = NULL;
  s_open = false;
  s_play = false;
  s_drag = 0;
  s_hot = 0;
  s_nr = 0;
  s_sel = 0;
  s_src[0] = 0;

  LayoutControls(hwndDlg);
#ifdef _WIN32
  ApplyWindowRegion(hwndDlg);
#endif
  InvalidateRect(hwndDlg, NULL, TRUE);
  edit_repaint_buttons(hwndDlg);
  UpdateDimBoxes(hwndDlg);
}

bool Editor_Apply(HWND hwndDlg)
{
  if (!s_open || !s_gif || !g_pending_fn[0] || Busy_Active()) return false;

  const int n = edit_frames();
  int firsts[EDIT_MAX_RANGES], lasts[EDIT_MAX_RANGES], cnt = 0, k;
  int keepframes = 0;
  for (k = 0; k < s_nr; k++)
  {
    int a = s_ra[k], b = s_rb[k];
    if (a < 0) a = 0;
    if (b > n - 1) b = n - 1;
    if (b < a) continue;
    firsts[cnt] = a;
    lasts[cnt] = b;
    cnt++;
    keepframes += b - a + 1;
  }

  if (cnt <= 0)
  {
    MessageBoxA(hwndDlg, "The selection is empty; nothing to keep.", "LICEcap",
                MB_OK | MB_ICONINFORMATION);
    return false;
  }

  char tmp[2048];
  snprintf(tmp, sizeof(tmp), "%s.trim.gif", g_pending_fn);
  DeleteFileA(tmp);

  s_play = false;
  EditorWriteJob job = {g_pending_fn, tmp, firsts, lasts, cnt};
  const bool wrote = Busy_Run(hwndDlg, "Applying edits...", keepframes, Editor_WriteWorker, &job);

  if (!wrote)
  {
    DeleteFileA(tmp);
    MessageBoxA(hwndDlg, "Could not write the trimmed GIF.", "LICEcap",
                MB_OK | MB_ICONERROR);
    return false;
  }

  if (!MoveFileExA(tmp, g_pending_fn, MOVEFILE_REPLACE_EXISTING))
  {
    // Never delete the original on replacement failure.
    DeleteFileA(tmp);
    MessageBoxA(hwndDlg, "Could not replace the recording. The original was kept.",
                "LICEcap", MB_OK | MB_ICONERROR);
    return false;
  }

  Editor_Close(hwndDlg);
  return true;
}

void Editor_Shutdown(void)
{
  GIFEdit_Free(s_gif);
  s_gif = NULL;
  s_open = false;
  s_play = false;
  s_nr = 0;
  s_sel = 0;
}

// ---------------------------------------------------------------------------
// public: input
// ---------------------------------------------------------------------------
bool Editor_HitTest(HWND hwndDlg, int x, int y)
{
  if (!s_open) return false;
  EDITLAYOUT l;
  edit_layout(hwndDlg, &l);
  POINT p;
  p.x = x;
  p.y = y;
  return PtInRect(&l.strip, p) || PtInRect(&l.preview, p);
}

int Editor_Cursor(HWND hwndDlg, int x, int y)
{
  if (!s_open) return EDITOR_CUR_NONE;
  EDITLAYOUT l;
  edit_layout(hwndDlg, &l);
  POINT p;
  p.x = x;
  p.y = y;

  if (PtInRect(&l.play, p) || PtInRect(&l.add, p) || PtInRect(&l.del, p) ||
      PtInRect(&l.apply, p) || PtInRect(&l.close, p))
    return EDITOR_CUR_HAND;

  RECT hit = l.track;
  InflateRect(&hit, 0, UI(6));
  if (PtInRect(&hit, p))
  {
    if (edit_any_handle(&l, x)) return EDITOR_CUR_HANDLE;
    return EDITOR_CUR_HAND;
  }
  if (PtInRect(&l.preview, p)) return EDITOR_CUR_HAND;
  return EDITOR_CUR_NONE;
}

bool Editor_Mouse(HWND hwndDlg, UINT msg, int x, int y)
{
  if (!s_open || Busy_Active()) return false;

  EDITLAYOUT l;
  edit_layout(hwndDlg, &l);
  POINT p;
  p.x = x;
  p.y = y;

  if (msg == WM_LBUTTONDOWN)
  {
    if (PtInRect(&l.play, p)) { edit_toggle_play(hwndDlg); return true; }
    if (PtInRect(&l.add, p)) { edit_add_range(hwndDlg); return true; }
    if (PtInRect(&l.del, p)) { edit_remove_range(hwndDlg); return true; }
    if (PtInRect(&l.apply, p)) { Editor_Apply(hwndDlg); return true; }
    if (PtInRect(&l.close, p)) { Editor_Close(hwndDlg); return true; }

    RECT hit = l.track;
    InflateRect(&hit, 0, UI(8));
    if (PtInRect(&hit, p))
    {
      s_play = false;
      int range, which;
      if (edit_handle_hit(&l, x, &range, &which))
      {
        s_sel = range;
        s_drag = which;
        edit_set_handle(hwndDlg, which, edit_frame_at_x(&l.track, edit_handle_x(&l, range, which)));
      }
      else
      {
        s_drag = 3;
        const int f = edit_frame_at_x(&l.track, x);
        const int r = edit_rng_at(f);
        if (r >= 0) s_sel = r;
        edit_set_cur(hwndDlg, f);
      }
      edit_invalidate(hwndDlg);
      return true;
    }

    if (PtInRect(&l.preview, p)) { edit_toggle_play(hwndDlg); return true; }
    return false;
  }

  if (msg == WM_MOUSEMOVE)
  {
    if (s_drag)
    {
      if (s_drag == 3) edit_seek_ms(hwndDlg, edit_ms_at_x(&l.track, x));
      else edit_set_handle(hwndDlg, s_drag, edit_frame_at_x(&l.track, x));
      edit_invalidate(hwndDlg);
      return true;
    }

    int hot = 0;
    if (PtInRect(&l.play, p)) hot = 1;
    else if (PtInRect(&l.apply, p)) hot = 2;
    else if (PtInRect(&l.close, p)) hot = 3;
    else if (PtInRect(&l.add, p)) hot = 5;
    else if (PtInRect(&l.del, p)) hot = 6;
    else
    {
      RECT hit = l.track;
      InflateRect(&hit, 0, UI(8));
      if (PtInRect(&hit, p)) hot = 4;
    }
    if (hot != s_hot)
    {
      s_hot = hot;
      InvalidateRect(hwndDlg, &l.strip, FALSE);
    }
    return PtInRect(&l.strip, p) || PtInRect(&l.preview, p);
  }

  if (msg == WM_LBUTTONUP)
  {
    if (s_drag)
    {
      s_drag = 0;
      edit_invalidate(hwndDlg);
      return true;
    }
    return PtInRect(&l.strip, p) || PtInRect(&l.preview, p);
  }

  return false;
}

bool Editor_Key(HWND hwndDlg, WPARAM vk)
{
  if (!s_open || Busy_Active()) return false;

  const int n = edit_frames();
  switch (vk)
  {
    case VK_SPACE:
      edit_toggle_play(hwndDlg);
      return true;
    case VK_LEFT:
      s_play = false;
      edit_set_cur(hwndDlg, s_cur - 1);
      return true;
    case VK_RIGHT:
      s_play = false;
      edit_set_cur(hwndDlg, s_cur + 1);
      return true;
    case VK_HOME:
      s_play = false;
      edit_set_cur(hwndDlg, 0);
      return true;
    case VK_END:
      s_play = false;
      edit_set_cur(hwndDlg, n - 1);
      return true;
    case 'I':
    case 'A':
      s_play = false;
      edit_set_handle(hwndDlg, 1, s_cur);
      return true;
    case 'O':
    case 'B':
      s_play = false;
      edit_set_handle(hwndDlg, 2, s_cur);
      return true;
    case 'N':
    case VK_INSERT:
      edit_add_range(hwndDlg);
      return true;
    case VK_DELETE:
    case VK_BACK:
      edit_remove_range(hwndDlg);
      return true;
    case VK_TAB:
      if (s_nr > 1)
      {
        s_play = false;
        s_sel = (s_sel + 1) % s_nr;
        s_cur = edit_first_kept(s_ra[s_sel]);
        edit_invalidate(hwndDlg);
      }
      return true;
    case VK_RETURN:
      Editor_Apply(hwndDlg);
      return true;
    case VK_ESCAPE:
      Editor_Close(hwndDlg);
      return true;
  }
  return false;
}

bool Editor_Tick(HWND hwndDlg)
{
  if (!s_open || !s_play || Busy_Active()) return false;

  const DWORD now = timeGetTime();
  if ((int)(now - s_next_tick) < 0) return false;

  const int n = edit_frames();
  if (n <= 0) return false;

  int guard = 0;
  do
  {
    s_cur = edit_next_kept(s_cur);
    s_next_tick += (DWORD)wdl_max(10, GIFEdit_DelayMs(s_gif, s_cur));
    guard++;
  }
  while ((int)(now - s_next_tick) >= 0 && guard < 240);

  edit_invalidate(hwndDlg);
  return true;
}

// ---------------------------------------------------------------------------
// public: painting
// ---------------------------------------------------------------------------
static void edit_draw_play(HDC dc, const RECT *r, bool hot)
{
  edit_round(dc, r, hot ? COL_BTN_HOT : COL_BTN, hot ? COL_BORDER_HOT : COL_BORDER);

  const int cx = (r->left + r->right) / 2;
  const int cy = (r->top + r->bottom) / 2;
  const int s = UI(4);

  if (s_play)
  {
    RECT a, b;
    a.left = cx - s;
    a.right = cx - UI(1);
    a.top = cy - s;
    a.bottom = cy + s;
    b.left = cx + UI(1);
    b.right = cx + s;
    b.top = a.top;
    b.bottom = a.bottom;
    edit_fill(dc, &a, COL_TEXT);
    edit_fill(dc, &b, COL_TEXT);
  }
  else
  {
    POINT pts[3];
    pts[0].x = cx - UI(3);
    pts[0].y = cy - s;
    pts[1].x = cx - UI(3);
    pts[1].y = cy + s;
    pts[2].x = cx + s;
    pts[2].y = cy;
    HBRUSH b = CreateSolidBrush(COL_TEXT);
    HGDIOBJ ob = SelectObject(dc, b);
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, pts, 3);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(b);
  }
}

static void edit_draw_button(HDC dc, const RECT *r, const char *label, bool primary, bool hot)
{
  COLORREF fill = primary ? (hot ? COL_ACCENT : COL_BTN_HOT) : (hot ? COL_BTN_HOT : COL_BTN);
  COLORREF border = primary ? COL_ACCENT : (hot ? COL_BORDER_HOT : COL_BORDER);
  COLORREF text = primary ? COL_TEXT_HOT : COL_TEXT;
  edit_round(dc, r, fill, border);
  edit_text(dc, r, label, text, g_ui_font, DT_CENTER | DT_VCENTER);
}

// Draggable bar for one range edge. Only the selected range gets the taller bar
// and the A/B flag, so a timeline with several ranges stays readable.
static void edit_draw_handle(HDC dc, const EDITLAYOUT *l, int x, int which,
                            bool selected, bool hot)
{
  const int half = UI(1);
  const int grow = selected ? UI(3) : UI(1);

  RECT bar;
  bar.left = x - half;
  bar.right = x + half + 1;
  bar.top = l->track.top - grow;
  bar.bottom = l->track.bottom + grow;
  if (bar.right <= bar.left) bar.right = bar.left + 1;

  // bright bar so it reads on top of the selection colour, accent when hovered
  const COLORREF col = hot ? COL_ACCENT : (selected ? COL_TEXT_HOT : COL_TEXT_DIS);
  edit_fill(dc, &bar, col);

  if (!selected) return;

  // little flag with the letter, under the track
  RECT f;
  f.left = x - UI(7);
  f.right = x + UI(7);
  f.top = l->track.bottom + UI(4);
  f.bottom = f.top + UI(12);
  if (f.left < l->strip.left + UI(2)) {
    const int d = (l->strip.left + UI(2)) - f.left;
    f.left += d;
    f.right += d;
  }
  if (f.right > l->strip.right - UI(2)) {
    const int d = f.right - (l->strip.right - UI(2));
    f.left -= d;
    f.right -= d;
  }
  edit_fill(dc, &f, col);
  edit_text(dc, &f, which == 1 ? "A" : "B", COL_BG, g_ui_font_small,
            DT_CENTER | DT_VCENTER);
}

void Editor_Paint(HWND hwndDlg, HDC dc)
{
  if (!s_open) return;

  EDITLAYOUT l;
  edit_layout(hwndDlg, &l);

  UI_PaintCaption(hwndDlg, dc);

  // preview: the recorded GIF, letterboxed on black
  edit_fill(dc, &l.preview, RGB(0, 0, 0));

  const int gw = GIFEdit_Width(s_gif);
  const int gh = GIFEdit_Height(s_gif);
  unsigned int *canvas = GIFEdit_Compose(s_gif, s_cur);

  if (canvas && gw > 0 && gh > 0)
  {
    const int aw = l.preview.right - l.preview.left - UI(8);
    const int ah = l.preview.bottom - l.preview.top - UI(8);
    if (aw > 0 && ah > 0)
    {
      int dw = aw, dh = (int)((double)aw * (double)gh / (double)gw + 0.5);
      if (dh > ah)
      {
        dh = ah;
        dw = (int)((double)ah * (double)gw / (double)gh + 0.5);
      }
      if (dw < 1) dw = 1;
      if (dh < 1) dh = 1;
      const int dx = l.preview.left + (l.preview.right - l.preview.left - dw) / 2;
      const int dy = l.preview.top + (l.preview.bottom - l.preview.top - dh) / 2;

      BITMAPINFO bi;
      memset(&bi, 0, sizeof(bi));
      bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
      bi.bmiHeader.biWidth = gw;
      bi.bmiHeader.biHeight = -gh;
      bi.bmiHeader.biPlanes = 1;
      bi.bmiHeader.biBitCount = 32;
      bi.bmiHeader.biCompression = BI_RGB;

      SetStretchBltMode(dc, COLORONCOLOR);
      StretchDIBits(dc, dx, dy, dw, dh, 0, 0, gw, gh, canvas, &bi,
                    DIB_RGB_COLORS, SRCCOPY);

      RECT pr;
      pr.left = dx - 1;
      pr.top = dy - 1;
      pr.right = dx + dw + 1;
      pr.bottom = dy + dh + 1;
      edit_frame(dc, &pr, COL_BORDER);
    }
  }

  // strip
  edit_fill(dc, &l.strip, COL_BG);
  {
    RECT line = l.strip;
    line.bottom = line.top + 1;
    edit_fill(dc, &line, COL_SEP);
  }

  edit_draw_play(dc, &l.play, s_hot == 1);
  edit_draw_button(dc, &l.add, "+", false, s_hot == 5);
  edit_draw_button(dc, &l.del, "-", false, s_hot == 6);

  // track: background, kept ranges, frame ticks
  edit_fill(dc, &l.track, COL_EDIT_BG);
  {
    const COLORREF dim = edit_blend(COL_ACCENT, COL_EDIT_BG, 45);
    int k;
    for (k = 0; k < s_nr; k++)
    {
      RECT sel = l.track;
      sel.left = edit_x_at_ms(&l.track, edit_rng_start_ms(k));
      sel.right = edit_x_at_ms(&l.track, edit_rng_end_ms(k));
      if (sel.right <= sel.left) sel.right = sel.left + 1;
      if (sel.right > l.track.right) sel.right = l.track.right;
      edit_fill(dc, &sel, k == s_sel ? COL_ACCENT : dim);
    }

    const int n = edit_frames();
    const int span = l.track.right - l.track.left;
    if (n > 1 && span > 8)
    {
      if (n <= 400)
      {
        int i;
        for (i = 0; i < n; i++)
        {
          const int x = edit_x_at_ms(&l.track, GIFEdit_StartMs(s_gif, i));
          RECT t;
          t.left = x;
          t.right = x + 1;
          t.top = l.track.top + UI(1);
          t.bottom = l.track.bottom - UI(1);
          edit_fill(dc, &t, COL_SEP);
        }
      }
      edit_frame(dc, &l.track, COL_BORDER);
    }
  }

  {
    int k;
    for (k = 0; k < s_nr; k++)
    {
      const bool selected = (k == s_sel);
      edit_draw_handle(dc, &l, edit_handle_x(&l, k, 1), 1, selected,
                       selected && (s_drag == 1 || s_hot == 4));
      edit_draw_handle(dc, &l, edit_handle_x(&l, k, 2), 2, selected,
                       selected && (s_drag == 2 || s_hot == 4));
    }
  }

  // playhead
  {
    const int x = edit_x_at_ms(&l.track, GIFEdit_StartMs(s_gif, s_cur));
    RECT ph;
    ph.left = x;
    ph.right = x + UI(2);
    ph.top = l.track.top - UI(2);
    ph.bottom = l.track.bottom + UI(2);
    edit_fill(dc, &ph, g_dark_ui ? RGB(250, 210, 60) : RGB(190, 120, 0));
  }

  edit_draw_button(dc, &l.apply, "Apply", true, s_hot == 2);
  edit_draw_button(dc, &l.close, "Close", false, s_hot == 3);

  // labels: two halves of the row, ellipsized so a long range list never
  // draws over the playhead/time readout
  {
    char a[32], b[32], cur[32], tot[32], kept[32], txt[256];
    edit_time_str(edit_rng_start_ms(s_sel), a, sizeof(a));
    edit_time_str(edit_rng_end_ms(s_sel), b, sizeof(b));
    edit_time_str(GIFEdit_StartMs(s_gif, s_cur), cur, sizeof(cur));
    edit_time_str(edit_total_ms(), tot, sizeof(tot));
    edit_time_str(edit_kept_ms(), kept, sizeof(kept));

    RECT tr;
    tr.top = l.strip.top + UI(43);
    tr.bottom = l.strip.bottom - UI(2);
    const int mid = l.track.left + (l.track.right - l.track.left) / 2;

    tr.left = l.track.left;
    tr.right = mid;
    snprintf(txt, sizeof(txt), "A %s   B %s   range %d/%d",
             a, b, s_sel + 1, s_nr > 0 ? s_nr : 1);
    edit_text(dc, &tr, txt, COL_TEXT, g_ui_font_small,
              DT_LEFT | DT_VCENTER | DT_END_ELLIPSIS);

    tr.left = mid;
    tr.right = l.track.right;
    snprintf(txt, sizeof(txt), "keep %s    %s / %s    frame %d/%d",
             kept, cur, tot, s_cur + 1, edit_frames());
    edit_text(dc, &tr, txt, COL_TEXT_DIS, g_ui_font_small,
              DT_RIGHT | DT_VCENTER | DT_END_ELLIPSIS);
  }
}
