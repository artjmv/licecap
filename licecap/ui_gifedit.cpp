/*
    ui_gifedit.cpp - GIF reading, frame compositing and trimmed-range encoding
    for the timeline editor (fork-only code, see licecap_ui.h).

    The editor has to show any frame of a finished capture immediately and then
    write back only the [A,B] range. Both need random access, so instead of
    slurping every frame (a 540x675 frame is 1.4 MB of indices) this module
    keeps the file bytes plus a small index of the image blocks and decodes one
    frame on demand.

    Frames are composed the way a viewer does it: transparent pixels leave the
    canvas alone and the disposal method of the previous frame is honoured.

    Writing a range re-encodes composited frames: the first frame is written
    whole, the rest as a diff against the previous one (transparent pixels +
    local palette), which is what the recorder itself does, so the trimmed file
    keeps roughly the size of the original.
*/

#include "licecap_ui.h"

#include "../WDL/fileread.h"
#include "../WDL/filewrite.h"
#include "../WDL/heapbuf.h"
#include "../WDL/lice/lice.h"

extern "C" {
#include "../WDL/giflib/gif_lib.h"
}

#define GE_MAX_DIM 16384
#define GE_MAX_FRAMES 100000
#define GE_MAX_PAL 256

// ---------------------------------------------------------------------------
// model
// ---------------------------------------------------------------------------

struct GE_Frame
{
  int x, y, w, h;
  int start_ms;         // time at which the frame is shown
  int delay_ms;         // how long it stays on screen
  int disposal;         // 0/1 = keep, 2 = restore background, 3 = restore previous
  int trans;            // transparent palette index, -1 for none
  int mcs;              // LZW minimum code size
  int dataoff;          // first sub-block size byte in the file image
  int dataend;          // offset just past the sub-block chain (must be 0 byte)
  int interlaced;
  int palcount;
  unsigned int pal[GE_MAX_PAL];   // 0xAARRGGBB, opaque
};

struct GIFEdit
{
  unsigned char *data;
  int datalen;
  int w, h;
  unsigned int bgcolor;
  int loopcount;                // -1 = no extension, 0 = infinite, n = repeats
  GE_Frame *frames;
  int nframes, framecap;
  int total_ms;

  unsigned int *canvas;         // composed BGRA, top-down (DIB ready)
  unsigned int *restore;        // saved rect for disposal 3
  int canvas_at;                // frame currently composed, -1 = empty
  int saved_x, saved_y, saved_w, saved_h;
  bool saved_valid;

  WDL_HeapBuf scratch;
};

static unsigned int ge_rgb(int r, int g, int b)
{
  return ((unsigned int)(r & 0xff) << 16) | ((unsigned int)(g & 0xff) << 8) |
         (unsigned int)(b & 0xff) | 0xff000000u;
}

static int ge_u16(const unsigned char *p)
{
  return (int)p[0] | ((int)p[1] << 8);
}

// ---------------------------------------------------------------------------
// parsing: header, global palette, and one entry per image block
// ---------------------------------------------------------------------------

static bool ge_add_frame(GIFEdit *g, GE_Frame *fr)
{
  if (g->nframes >= g->framecap)
  {
    const int ncap = g->framecap ? g->framecap * 2 : 64;
    if (ncap > GE_MAX_FRAMES) return false;
    GE_Frame *nf = (GE_Frame *)realloc(g->frames, (size_t)ncap * sizeof(GE_Frame));
    if (!nf) return false;
    g->frames = nf;
    g->framecap = ncap;
  }
  g->frames[g->nframes++] = *fr;
  return true;
}

static bool ge_read_palette(const unsigned char *p, int count, int *outcount,
                            unsigned int *pal)
{
  int i;
  if (count <= 0 || count > GE_MAX_PAL) return false;
  for (i = 0; i < count; i++)
    pal[i] = ge_rgb(p[i*3], p[i*3+1], p[i*3+2]);
  for (; i < GE_MAX_PAL; i++) pal[i] = 0xff000000u;
  *outcount = count;
  return true;
}

// Progress reporting for the two long calls (see licecap_ui.h). Kept as plain
// globals because the caller is single threaded: one operation at a time.
static GIFEditProgressFn s_prog_fn;
static void *s_prog_ctx;

static void ge_progress(int done, int total)
{
  if (s_prog_fn) s_prog_fn(s_prog_ctx, done, total);
}

static bool ge_parse(GIFEdit *g)
{
  const unsigned char *d = g->data;
  const int len = g->datalen;
  int pos;

  if (len < 13) return false;
  if (memcmp(d, "GIF87a", 6) && memcmp(d, "GIF89a", 6)) return false;

  g->w = ge_u16(d + 6);
  g->h = ge_u16(d + 8);
  if (g->w <= 0 || g->h <= 0 || g->w > GE_MAX_DIM || g->h > GE_MAX_DIM) return false;

  const int packed = d[10];
  const int bgidx = d[11];
  pos = 13;

  unsigned int gpal[GE_MAX_PAL];
  int gpalcount = 0;
  if (packed & 0x80)
  {
    const int count = 2 << (packed & 7);
    if (pos + count * 3 > len) return false;
    if (!ge_read_palette(d + pos, count, &gpalcount, gpal)) return false;
    pos += count * 3;
  }
  g->bgcolor = 0xff000000u;
  if (gpalcount > 0 && bgidx >= 0 && bgidx < gpalcount) g->bgcolor = gpal[bgidx];

  g->loopcount = -1;

  // graphics control state carried to the next image block
  int delay = 0, disposal = 0, trans = -1;

  while (pos < len)
  {
    const int block = d[pos++];

    if (block == 0x3b) break;               // trailer

    if (block == 0x21)                      // extension
    {
      if (pos >= len) break;
      const int label = d[pos++];

      // extension sub-blocks: size byte, data, ... , 0
      if (label == 0xf9)
      {
        if (pos >= len) break;
        const int sz = d[pos++];
        if (sz >= 4 && pos + sz <= len)
        {
          const unsigned char *p = d + pos;
          trans = (p[0] & 1) ? (int)p[3] : -1;
          disposal = (p[0] >> 2) & 7;
          delay = (int)p[1] | ((int)p[2] << 8);
        }
        pos += sz;
        // skip the terminator of the block chain
        while (pos < len && d[pos]) pos += (int)d[pos] + 1;
        if (pos < len) pos++;
      }
      else
      {
        // Generic extension: walk the sub-blocks. The NETSCAPE loop count is
        // two blocks: "NETSCAPE2.0" and then { 1, lo, hi }.
        bool netscape = false;
        while (pos < len)
        {
          const int sz = d[pos];
          if (sz == 0) { pos++; break; }
          if (pos + 1 + sz > len) { pos = len; break; }
          const unsigned char *p = d + pos + 1;
          if (label == 0xff)
          {
            if (!netscape && sz >= 11 && !memcmp(p, "NETSCAPE2.0", 11)) netscape = true;
            else if (netscape && sz >= 3 && p[0] == 1)
              g->loopcount = (int)p[1] | ((int)p[2] << 8);
          }
          pos += 1 + sz;
        }
      }
      continue;
    }

    if (block == 0x2c)                      // image descriptor
    {
      if (pos + 9 > len) break;
      GE_Frame fr;
      memset(&fr, 0, sizeof(fr));
      fr.x = ge_u16(d + pos);
      fr.y = ge_u16(d + pos + 2);
      fr.w = ge_u16(d + pos + 4);
      fr.h = ge_u16(d + pos + 6);
      const int ipacked = d[pos + 8];
      pos += 9;

      fr.interlaced = (ipacked & 0x40) ? 1 : 0;

      if (ipacked & 0x80)
      {
        const int count = 2 << (ipacked & 7);
        if (pos + count * 3 > len) break;
        if (!ge_read_palette(d + pos, count, &fr.palcount, fr.pal)) break;
        pos += count * 3;
      }
      else
      {
        if (!gpalcount) break;
        memcpy(fr.pal, gpal, sizeof(fr.pal));
        fr.palcount = gpalcount;
      }

      if (pos >= len) break;
      fr.mcs = d[pos++];

      fr.dataoff = pos;
      while (pos < len)
      {
        const int sz = d[pos];
        if (sz == 0) { pos++; break; }
        if (pos + 1 + sz > len) { pos = len; break; }
        pos += 1 + sz;
      }
      fr.dataend = pos;

      if (fr.w <= 0 || fr.h <= 0) break;
      if (fr.x + fr.w > g->w || fr.y + fr.h > g->h) break;   // not our file
      if (fr.trans >= fr.palcount) fr.trans = -1;

      fr.delay_ms = delay * 10;
      if (fr.delay_ms <= 0) fr.delay_ms = 100;   // viewers treat 0 as "slow"
      fr.disposal = disposal;
      fr.trans = trans;
      fr.start_ms = g->nframes ? g->frames[g->nframes-1].start_ms +
                                 g->frames[g->nframes-1].delay_ms : 0;

      if (!ge_add_frame(g, &fr)) break;
      ge_progress(g->nframes, 0);   // frame count so far; the total is unknown

      delay = 0;
      disposal = 0;
      trans = -1;
      continue;
    }

    // unknown top level byte: the file is not something we understand
    break;
  }

  if (!g->nframes) return false;
  g->total_ms = g->frames[g->nframes-1].start_ms + g->frames[g->nframes-1].delay_ms;
  return true;
}

// ---------------------------------------------------------------------------
// LZW
// ---------------------------------------------------------------------------

static bool ge_lzw(const unsigned char *src, int srclen, int mcs,
                   unsigned char *dst, int npix)
{
  if (mcs < 2 || mcs > 8 || srclen <= 0 || npix <= 0) return false;

  const int clear = 1 << mcs;
  const int eoi = clear + 1;
  unsigned int prefix[4096];
  unsigned char suffix[4096];
  unsigned char stack[4096];

  int i;
  for (i = 0; i < clear; i++)
  {
    prefix[i] = 0xffffffffu;
    suffix[i] = (unsigned char)i;
  }

  int code_size = mcs + 1;
  int next = eoi + 1;
  int old = -1;
  int bitpos = 0;
  int outpos = 0;
  const int maxbit = srclen * 8;

  while (outpos < npix)
  {
    if (bitpos + code_size > maxbit) break;

    const int bytei = bitpos >> 3;
    const int sh = bitpos & 7;
    unsigned int v = src[bytei];
    if (bytei + 1 < srclen) v |= (unsigned int)src[bytei+1] << 8;
    if (bytei + 2 < srclen) v |= (unsigned int)src[bytei+2] << 16;
    const int code = (int)((v >> sh) & ((1u << code_size) - 1));
    bitpos += code_size;

    if (code == clear)
    {
      code_size = mcs + 1;
      next = eoi + 1;
      old = -1;
      continue;
    }
    if (code == eoi) break;

    if (old < 0)
    {
      if (code >= clear) break;
      dst[outpos++] = (unsigned char)code;
      old = code;
      continue;
    }

    const bool kwk = (code == next);
    if (code > next) break;

    int c = kwk ? old : code;
    int sp = 0;
    bool bad = false;
    while (c >= clear)
    {
      if (sp >= 4096 || c >= 4096) { bad = true; break; }
      stack[sp++] = suffix[c];
      c = (int)prefix[c];
      if (c < 0) { bad = true; break; }
    }
    if (bad || c < 0 || c >= clear) break;
    stack[sp++] = (unsigned char)c;
    const int firstchar = c;

    while (sp > 0 && outpos < npix) dst[outpos++] = stack[--sp];
    if (kwk && outpos < npix) dst[outpos++] = (unsigned char)firstchar;

    if (next < 4096)
    {
      prefix[next] = (unsigned int)old;
      suffix[next] = (unsigned char)firstchar;
      next++;
      if (next == (1 << code_size) && code_size < 12) code_size++;
    }
    old = code;
  }

  return outpos > 0;
}

// Concatenate one image block's sub-blocks into g->scratch and decode it into
// the index buffer (row order fixed up when the frame is interlaced).
static bool ge_frame_indices(GIFEdit *g, const GE_Frame *f, unsigned char **out)
{
  const int need = (int)((size_t)f->w * (size_t)f->h);
  if (need <= 0) return false;

  int total = 0;
  int pos = f->dataoff;
  while (pos < f->dataend)
  {
    const int sz = g->data[pos];
    if (sz == 0) break;
    if (pos + 1 + sz > f->dataend) break;
    total += sz;
    pos += 1 + sz;
  }
  if (total <= 0) return false;

  unsigned char *raw = (unsigned char *)g->scratch.ResizeOK(total + need + 16);
  if (!raw) return false;

  int wr = 0;
  pos = f->dataoff;
  while (pos < f->dataend)
  {
    const int sz = g->data[pos];
    if (sz == 0) break;
    if (pos + 1 + sz > f->dataend) break;
    memcpy(raw + wr, g->data + pos + 1, sz);
    wr += sz;
    pos += 1 + sz;
  }

  unsigned char *idx = raw + total;
  memset(idx, 0, need);
  if (!ge_lzw(raw, wr, f->mcs, idx, need)) return false;

  if (f->interlaced)
  {
    unsigned char *tmp = (unsigned char *)g->scratch.ResizeOK(total + need * 2 + 16);
    if (!tmp) return false;
    // ResizeOK may move the buffer: re-derive both pointers
    const unsigned char *src = tmp + total;
    unsigned char *dst = tmp + total + need;
    static const int start[4] = { 0, 4, 2, 1 };
    static const int step[4]  = { 8, 8, 4, 2 };
    int row = 0, pass;
    for (pass = 0; pass < 4 && row < f->h; pass++)
    {
      int y;
      for (y = start[pass]; y < f->h && row < f->h; y += step[pass])
      {
        memcpy(dst + (size_t)y * f->w, src + (size_t)row * f->w, f->w);
        row++;
      }
    }
    *out = dst;
    return true;
  }

  *out = idx;
  return true;
}

// ---------------------------------------------------------------------------
// compositing
// ---------------------------------------------------------------------------

static void ge_fill(GIFEdit *g, unsigned int col)
{
  const size_t npix = (size_t)g->w * (size_t)g->h;
  size_t i;
  unsigned int *p = g->canvas;
  for (i = 0; i < npix; i++) p[i] = col;
}

static void ge_fill_rect(GIFEdit *g, int x, int y, int w, int h, unsigned int col)
{
  int yy, xx;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > g->w) w = g->w - x;
  if (y + h > g->h) h = g->h - y;
  for (yy = 0; yy < h; yy++)
  {
    unsigned int *p = g->canvas + (size_t)(y+yy) * g->w + x;
    for (xx = 0; xx < w; xx++) p[xx] = col;
  }
}

static void ge_save_rect(GIFEdit *g, int x, int y, int w, int h)
{
  g->saved_valid = false;
  if (x < 0) { w += x; x = 0; }
  if (y < 0) { h += y; y = 0; }
  if (x + w > g->w) w = g->w - x;
  if (y + h > g->h) h = g->h - y;
  if (w <= 0 || h <= 0) return;
  g->saved_x = x;
  g->saved_y = y;
  g->saved_w = w;
  g->saved_h = h;
  int yy;
  for (yy = 0; yy < h; yy++)
    memcpy(g->restore + (size_t)(y+yy) * g->w + x,
           g->canvas + (size_t)(y+yy) * g->w + x,
           (size_t)w * sizeof(unsigned int));
  g->saved_valid = true;
}

static void ge_restore_rect(GIFEdit *g)
{
  if (!g->saved_valid) return;
  int yy;
  for (yy = 0; yy < g->saved_h; yy++)
    memcpy(g->canvas + (size_t)(g->saved_y+yy) * g->w + g->saved_x,
           g->restore + (size_t)(g->saved_y+yy) * g->w + g->saved_x,
           (size_t)g->saved_w * sizeof(unsigned int));
  g->saved_valid = false;
}

static void ge_draw_frame(GIFEdit *g, const GE_Frame *f)
{
  unsigned char *idx = NULL;
  if (!ge_frame_indices(g, f, &idx)) return;

  int yy, xx;
  for (yy = 0; yy < f->h; yy++)
  {
    const int cy = f->y + yy;
    if (cy < 0 || cy >= g->h) continue;
    const unsigned char *row = idx + (size_t)yy * f->w;
    unsigned int *dst = g->canvas + (size_t)cy * g->w;
    for (xx = 0; xx < f->w; xx++)
    {
      const int cx = f->x + xx;
      if (cx < 0 || cx >= g->w) continue;
      const int c = row[xx];
      if (c == f->trans) continue;
      if (c >= f->palcount) continue;
      dst[cx] = f->pal[c];
    }
  }
}

// ---------------------------------------------------------------------------
// public: load / query / compose
// ---------------------------------------------------------------------------

void *GIFEdit_Load(const char *fn)
{
  if (!fn || !fn[0]) return NULL;

  WDL_FileRead fr(fn, 0, 65536, 4);
  if (!fr.IsOpen()) return NULL;

  GIFEdit *g = (GIFEdit *)calloc(1, sizeof(GIFEdit));
  if (!g) return NULL;
  g->canvas_at = -1;

  WDL_HeapBuf buf(65536);
  {
    const WDL_FILEREAD_POSTYPE fsz = fr.GetSize();
    if (fsz <= 0 || fsz > (WDL_FILEREAD_POSTYPE)512 << 20)
    {
      free(g);
      return NULL;
    }
    unsigned char *p = (unsigned char *)buf.ResizeOK((int)fsz + 1);
    if (!p)
    {
      free(g);
      return NULL;
    }
    int got = 0;
    while (got < (int)fsz)
    {
      const int n = fr.Read(p + got, (int)fsz - got);
      if (n <= 0) break;
      got += n;
    }
    g->data = (unsigned char *)malloc((size_t)got + 1);
    if (!g->data)
    {
      free(g);
      return NULL;
    }
    memcpy(g->data, p, (size_t)got);
    g->datalen = got;
  }

  if (!ge_parse(g))
  {
    GIFEdit_Free(g);
    return NULL;
  }

  const size_t npix = (size_t)g->w * (size_t)g->h;
  g->canvas = (unsigned int *)malloc(npix * sizeof(unsigned int));
  g->restore = (unsigned int *)malloc(npix * sizeof(unsigned int));
  if (!g->canvas || !g->restore)
  {
    GIFEdit_Free(g);
    return NULL;
  }
  return g;
}

void GIFEdit_Free(void *h)
{
  GIFEdit *g = (GIFEdit *)h;
  if (!g) return;
  free(g->data);
  free(g->frames);
  free(g->canvas);
  free(g->restore);
  free(g);
}

void GIFEdit_SetProgress(GIFEditProgressFn fn, void *ctx)
{
  s_prog_fn = fn;
  s_prog_ctx = fn ? ctx : NULL;
}

bool GIFEdit_Valid(void *h) { return h && ((GIFEdit *)h)->nframes > 0; }
int GIFEdit_Frames(void *h) { return h ? ((GIFEdit *)h)->nframes : 0; }
int GIFEdit_Width(void *h) { return h ? ((GIFEdit *)h)->w : 0; }
int GIFEdit_Height(void *h) { return h ? ((GIFEdit *)h)->h : 0; }
int GIFEdit_TotalMs(void *h) { return h ? ((GIFEdit *)h)->total_ms : 0; }

int GIFEdit_DelayMs(void *h, int i)
{
  GIFEdit *g = (GIFEdit *)h;
  if (!g || i < 0 || i >= g->nframes) return 0;
  return g->frames[i].delay_ms;
}

int GIFEdit_StartMs(void *h, int i)
{
  GIFEdit *g = (GIFEdit *)h;
  if (!g || !g->nframes) return 0;
  if (i < 0) return 0;
  if (i >= g->nframes) return g->total_ms;
  return g->frames[i].start_ms;
}

int GIFEdit_IndexAtMs(void *h, int ms)
{
  GIFEdit *g = (GIFEdit *)h;
  if (!g || !g->nframes) return 0;
  if (ms <= 0) return 0;
  int lo = 0, hi = g->nframes - 1;
  while (lo < hi)
  {
    const int mid = (lo + hi + 1) / 2;
    if (g->frames[mid].start_ms <= ms) lo = mid;
    else hi = mid - 1;
  }
  return lo;
}

unsigned int *GIFEdit_Compose(void *h, int i)
{
  GIFEdit *g = (GIFEdit *)h;
  if (!g || !g->nframes) return NULL;
  if (i < 0) i = 0;
  if (i >= g->nframes) i = g->nframes - 1;

  if (g->canvas_at < 0 || i < g->canvas_at)
  {
    ge_fill(g, g->bgcolor);
    g->canvas_at = -1;
    g->saved_valid = false;
  }

  while (g->canvas_at < i)
  {
    const int n = g->canvas_at + 1;
    if (n > 0)
    {
      const GE_Frame *p = &g->frames[n-1];
      if (p->disposal == 2) ge_fill_rect(g, p->x, p->y, p->w, p->h, g->bgcolor);
      else if (p->disposal == 3) ge_restore_rect(g);
    }
    g->saved_valid = false;

    const GE_Frame *f = &g->frames[n];
    if (f->disposal == 3) ge_save_rect(g, f->x, f->y, f->w, f->h);
    ge_draw_frame(g, f);
    g->canvas_at = n;
  }
  return g->canvas;
}

// ---------------------------------------------------------------------------
// public: write a trimmed GIF
// ---------------------------------------------------------------------------

static int ge_write_func(GifFileType *fh, const GifByteType *buf, int sz)
{
  return ((WDL_FileWrite *)fh->UserData)->Write(buf, sz);
}

static int ge_delay_cs(int ms)
{
  int cs = ms / 10;
  if (cs < 1 && ms > 0) cs = 1;
  if (cs > 60000) cs = 60000;
  return cs;
}

static void ge_palette_to_cmap(const LICE_pixel *pal, int n, ColorMapObject *cmap)
{
  int i;
  for (i = 0; i < GE_MAX_PAL; i++)
  {
    if (i < n)
    {
      cmap->Colors[i].Red   = (GifByteType)LICE_GETR(pal[i]);
      cmap->Colors[i].Green = (GifByteType)LICE_GETG(pal[i]);
      cmap->Colors[i].Blue  = (GifByteType)LICE_GETB(pal[i]);
    }
    else
    {
      cmap->Colors[i].Red = cmap->Colors[i].Green = cmap->Colors[i].Blue = 0;
    }
  }
}

static void ge_build_lut(void *octree, unsigned char *lut)
{
  int r, g, b;
  for (r = 0; r < 32; r++)
    for (g = 0; g < 32; g++)
      for (b = 0; b < 32; b++)
        lut[(r << 10) | (g << 5) | b] = (unsigned char)LICE_FindInOctree(octree,
          LICE_RGBA(r << 3, g << 3, b << 3, 255));
}

// Write the frames listed in seq, in that order, as a single GIF. The list is
// normally the concatenation of several kept ranges (the editor's [A,B] +
// [C,D] + ...), so consecutive entries may jump: the delta/transparency step
// compares against the frame actually written before, which is still correct
// for a cut, it just produces a bigger diff rectangle.
static bool ge_write_seq(GIFEdit *g, const int *seq, int total, const char *outfn)
{
  if (!g || !g->nframes || !seq || total <= 0 || !outfn || !outfn[0]) return false;

  unsigned int *canvas = GIFEdit_Compose(g, seq[0]);
  if (!canvas) return false;

  const int w = g->w, hgt = g->h;
  const size_t npix = (size_t)w * (size_t)hgt;
  unsigned int *prev = (unsigned int *)malloc(npix * sizeof(unsigned int));
  GifPixelType *line = (GifPixelType *)malloc((size_t)w + 16);
  if (!prev || !line)
  {
    free(prev);
    free(line);
    return false;
  }
  memcpy(prev, canvas, npix * sizeof(unsigned int));

  bool ok = true;
  WDL_FileWrite *fw = new WDL_FileWrite(outfn, false, 65536, 16, 16);
  if (!fw->IsOpen())
  {
    delete fw;
    free(prev);
    free(line);
    return false;
  }

  EGifSetGifVersion("89a");
  GifFileType *gf = EGifOpen(fw, ge_write_func);
  if (!gf)
  {
    delete fw;
    free(prev);
    free(line);
    return false;
  }

  ColorMapObject *cmap = MakeMapObject(GE_MAX_PAL, NULL);
  void *octree = LICE_CreateOctree(GE_MAX_PAL - 1);
  if (!cmap || !octree) ok = false;

  if (ok && EGifPutScreenDesc(gf, w, hgt, 8, 0, NULL) == GIF_ERROR) ok = false;

  if (ok && g->loopcount >= 0)
  {
    const int nr = g->loopcount;
    unsigned char ext[] = { 0xB, 'N','E','T','S','C','A','P','E','2','.','0', 3, 1,
                            (unsigned char)(nr & 0xff), (unsigned char)((nr >> 8) & 0xff) };
    if (EGifPutExtension(gf, 0xff, sizeof(ext), ext) == GIF_ERROR) ok = false;
  }

  WDL_HeapBuf lutbuf;
  unsigned char *lut = (unsigned char *)lutbuf.ResizeOK(32*32*32);
  if (!lut) ok = false;

  LICE_WrapperBitmap full((LICE_pixel *)canvas, w, hgt, w, false);
  LICE_WrapperBitmap fullprev((LICE_pixel *)prev, w, hgt, w, false);
  LICE_pixel palette[GE_MAX_PAL];

  int pending_ms = 0;
  int t;

  for (t = 0; ok && t < total; t++)
  {
    ge_progress(t, total);
    const int i = seq[t];
    unsigned int *c = (t == 0) ? canvas : GIFEdit_Compose(g, i);
    if (!c) { ok = false; break; }

    const int delay = g->frames[i].delay_ms;

    int dx0 = w, dy0 = hgt, dx1 = -1, dy1 = -1;
    if (t > 0)
    {
      int y, x;
      for (y = 0; y < hgt; y++)
      {
        const unsigned int *a = c + (size_t)y * w;
        const unsigned int *b = prev + (size_t)y * w;
        int xmin = -1, xmax = -1;
        for (x = 0; x < w; x++)
        {
          if (a[x] != b[x])
          {
            if (xmin < 0) xmin = x;
            xmax = x;
          }
        }
        if (xmin >= 0)
        {
          if (y < dy0) dy0 = y;
          if (y > dy1) dy1 = y;
          if (xmin < dx0) dx0 = xmin;
          if (xmax > dx1) dx1 = xmax;
        }
      }
    }

    const bool firstframe = (t == 0);
    const bool nodiff = (!firstframe && dx1 < 0);

    if (nodiff)
    {
      // nothing changed: fold the delay into the next frame we actually write
      pending_ms += delay;
      continue;
    }

    int rx = 0, ry = 0, rw = w, rh = hgt;
    if (!firstframe) { rx = dx0; ry = dy0; rw = dx1 - dx0 + 1; rh = dy1 - dy0 + 1; }

    LICE_SubBitmap sub(&full, rx, ry, rw, rh);
    LICE_SubBitmap subprev(&fullprev, rx, ry, rw, rh);

    LICE_ResetOctree(octree, GE_MAX_PAL - 1);
    if (firstframe) LICE_BuildOctree(octree, &sub);
    else LICE_BuildOctreeForDiff(octree, &sub, &subprev, LICE_RGBA(255,255,255,0));
    int palcount = LICE_ExtractOctreePalette(octree, palette);
    if (palcount < 1) palcount = 1;
    if (palcount > GE_MAX_PAL - 1) palcount = GE_MAX_PAL - 1;
    ge_palette_to_cmap(palette, palcount, cmap);
    if (rw * rh > 16384) ge_build_lut(octree, lut);

    const int trans_index = GE_MAX_PAL - 1;
    unsigned char gce[4];
    // disposal 1 (leave in place) and transparency for the diff frames
    gce[0] = (unsigned char)((1 << 2) | (firstframe ? 0 : 1));
    gce[1] = (unsigned char)(ge_delay_cs(delay + pending_ms) & 0xff);
    gce[2] = (unsigned char)((ge_delay_cs(delay + pending_ms) >> 8) & 0xff);
    gce[3] = (unsigned char)trans_index;
    pending_ms = 0;

    if (EGifPutExtension(gf, 0xf9, 4, gce) == GIF_ERROR) { ok = false; break; }
    if (EGifPutImageDesc(gf, rx, ry, rw, rh, 0, cmap) == GIF_ERROR) { ok = false; break; }

    int y;
    for (y = 0; ok && y < rh; y++)
    {
      const unsigned int *src = c + (size_t)(ry + y) * w + rx;
      const unsigned int *ref = prev + (size_t)(ry + y) * w + rx;
      int x;
      for (x = 0; x < rw; x++)
      {
        if (!firstframe && src[x] == ref[x]) { line[x] = (GifPixelType)trans_index; continue; }
        const unsigned int px = src[x];
        const int r = (int)((px >> 16) & 0xff);
        const int gg = (int)((px >> 8) & 0xff);
        const int b = (int)(px & 0xff);
        int cidx;
        if (rw * rh > 16384) cidx = lut[((r >> 3) << 10) | ((gg >> 3) << 5) | (b >> 3)];
        else cidx = LICE_FindInOctree(octree, LICE_RGBA(r, gg, b, 255));
        if (cidx < 0 || cidx >= palcount) cidx = 0;
        line[x] = (GifPixelType)cidx;
      }
      if (EGifPutLine(gf, line, rw) == GIF_ERROR) { ok = false; break; }
    }

    memcpy(prev, c, npix * sizeof(unsigned int));
  }

  if (cmap)
  {
    free(cmap->Colors);
    free(cmap);
  }
  if (octree) LICE_DestroyOctree(octree);
  if (EGifCloseFile(gf) != GIF_OK) ok = false;
  delete fw;
  free(prev);
  free(line);
  return ok;
}

bool GIFEdit_WriteRanges(void *h, const int *firsts, const int *lasts, int nranges,
                         const char *outfn)
{
  GIFEdit *g = (GIFEdit *)h;
  if (!g || !g->nframes || !firsts || !lasts || nranges <= 0) return false;

  // flatten the ranges into one ascending list of frame indices
  int total = 0, k;
  for (k = 0; k < nranges; k++)
  {
    int a = firsts[k], b = lasts[k];
    if (a < 0) a = 0;
    if (b >= g->nframes) b = g->nframes - 1;
    if (b < a) continue;
    total += b - a + 1;
  }
  if (total <= 0) return false;

  int *seq = (int *)malloc((size_t)total * sizeof(int));
  if (!seq) return false;

  int n = 0;
  for (k = 0; k < nranges; k++)
  {
    int a = firsts[k], b = lasts[k];
    if (a < 0) a = 0;
    if (b >= g->nframes) b = g->nframes - 1;
    int i;
    for (i = a; i <= b; i++) seq[n++] = i;
  }

  const bool ok = ge_write_seq(g, seq, n, outfn);
  free(seq);
  return ok;
}

bool GIFEdit_WriteRange(void *h, int first, int last, const char *outfn)
{
  return GIFEdit_WriteRanges(h, &first, &last, 1, outfn);
}
