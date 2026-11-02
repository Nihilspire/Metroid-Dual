/* metroidlink.c - Metroid Link for Linux handhelds (ROCKNIX): the Metroid Dual companion screen on its own, for a
 * game running in RetroArch (FCEUmm core) with network commands on.
 *
 * One window, "Metroid Link Bottom" (ROCKNIX's sway config puts "Bottom" windows on the second panel), showing the
 * Link page (assets/link/link.js, shared with Metroid Dual) run in QuickJS with the same canvas shim. window.Native
 * is backed by RetroArch's UDP command interface (READ_CORE_MEMORY / READ_CORE_RAM, GET_STATUS, SAVE_STATE_SLOT,
 * LOAD_STATE_SLOT, MENU_TOGGLE) instead of an emulator. The player's ROM (port folder) gives the map, items and font.
 *
 * Threads: main = SDL events + rendering, js = QuickJS, net = RetroArch polling (keeps a cache the page reads).
 */
#define _GNU_SOURCE
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <libgen.h>
#include <signal.h>
#include "quickjs.h"

#define VERSION "0.3"
#define SLOTS 4

/* ------------------------------------------------------------------ paths, log, files */
static char base_dir[1024], rom_path[1100], ls_path[1100], slots_path[1100];
static FILE *logf_;
static void logm(const char *fmt, ...) {
  char b[2048]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  fprintf(stderr, "%s\n", b); if (logf_) { fprintf(logf_, "%s\n", b); fflush(logf_); }
}
static double now_ms(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e3 + t.tv_nsec / 1e6; }
static char *read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb"); if (!f) return NULL;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  char *b = malloc(n + 1); if (!b) { fclose(f); return NULL; }
  size_t r = fread(b, 1, n, f); fclose(f); b[r] = 0; if (len) *len = r; return b;
}
static int write_file(const char *path, const void *d, size_t n) {
  char tmp[1200]; snprintf(tmp, sizeof tmp, "%s.tmp", path);
  FILE *f = fopen(tmp, "wb"); if (!f) return -1;
  int ok = fwrite(d, 1, n, f) == n; fclose(f);
  if (!ok) { remove(tmp); return -1; }
  return rename(tmp, path);
}
static volatile int quit_req;

/* ------------------------------------------------------------------ ROM (port folder) */
static uint8_t *rom; static size_t rom_len;
static int find_rom(void) {
  static const char *names[] = { "Metroid (USA).nes", "metroid.nes", "Metroid.nes", NULL };
  const char *env = getenv("ML_ROM");
  if (env && access(env, R_OK) == 0) { snprintf(rom_path, sizeof rom_path, "%s", env); goto found; }
  for (int i = 0; names[i]; i++) { snprintf(rom_path, sizeof rom_path, "%s/%s", base_dir, names[i]); if (access(rom_path, R_OK) == 0) goto found; }
  { char cmd[1300]; snprintf(cmd, sizeof cmd, "ls -1 \"%s\" 2>/dev/null", base_dir);
    FILE *p = popen(cmd, "r"); rom_path[0] = 0;
    if (p) { char ln[512]; while (fgets(ln, sizeof ln, p)) { ln[strcspn(ln, "\n")] = 0; size_t l = strlen(ln);
        if (l > 4 && !strcasecmp(ln + l - 4, ".nes")) { snprintf(rom_path, sizeof rom_path, "%s/%s", base_dir, ln); break; } } pclose(p); } }
  if (!rom_path[0]) return 0;
found:
  rom = (uint8_t *)read_file(rom_path, &rom_len);
  if (rom && rom_len < 16 + 0x20000) { free(rom); rom = NULL; rom_len = 0; }
  logm("ROM: %s (%zu bytes)", rom ? rom_path : "none", rom_len);
  return rom != NULL;
}
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *b64enc(const uint8_t *d, size_t n) {
  char *o = malloc(4 * ((n + 2) / 3) + 1), *p = o; size_t i = 0;
  for (; i + 2 < n; i += 3) { uint32_t v = d[i] << 16 | d[i + 1] << 8 | d[i + 2];
    *p++ = B64[v >> 18 & 63]; *p++ = B64[v >> 12 & 63]; *p++ = B64[v >> 6 & 63]; *p++ = B64[v & 63]; }
  if (i < n) { uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0);
    *p++ = B64[v >> 18 & 63]; *p++ = B64[v >> 12 & 63]; *p++ = i + 1 < n ? B64[v >> 6 & 63] : '='; *p++ = '='; }
  *p = 0; return o;
}

/* ------------------------------------------------------------------ RetroArch (UDP network commands) */
/* Same logic as linkapp/RetroArch.java: a thread keeps a cache of the RAM ranges the page asked for. */
static char ra_host[256] = "127.0.0.1"; static int ra_port = 55355;
static int ra_sock = -1; static struct sockaddr_in ra_addr; static int ra_addr_ok;
static pthread_mutex_t ra_mx = PTHREAD_MUTEX_INITIALIZER;          /* guards the cache strings below */
static char ra_spec[1024], ra_spec_cached[1024], ra_mem[4096], ra_pal[65], ra_status[256], ra_error[64];
static double ra_last_ok = -1e9, ra_last_reply = -1e9;
static int ra_use_ram;
static int ra_connected(void) { return now_ms() - ra_last_ok < 1500; }
static int ra_resolve(void) {
  memset(&ra_addr, 0, sizeof ra_addr); ra_addr.sin_family = AF_INET; ra_addr.sin_port = htons(ra_port);
  if (inet_pton(AF_INET, ra_host, &ra_addr.sin_addr) == 1) return ra_addr_ok = 1;
  struct addrinfo hints = {0}, *res = NULL; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM;
  if (getaddrinfo(ra_host, NULL, &hints, &res) == 0 && res) { ra_addr.sin_addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr; freeaddrinfo(res); return ra_addr_ok = 1; }
  return ra_addr_ok = 0;
}
static void ra_send(const char *cmd) {
  if (ra_sock < 0 || !ra_addr_ok) return;
  sendto(ra_sock, cmd, strlen(cmd), 0, (struct sockaddr *)&ra_addr, sizeof ra_addr);
}
/* Sends cmd, waits up to 150 ms for a reply starting with prefix. Returns 1 with the reply in out. */
static int ra_ask(const char *cmd, const char *prefix, char *out, size_t n) {
  if (ra_sock < 0 || !ra_addr_ok) return 0;
  ra_send(cmd);
  double until = now_ms() + 150;
  while (now_ms() < until) {
    ssize_t r = recv(ra_sock, out, n - 1, 0);
    if (r < 0) return 0;                       /* timeout (SO_RCVTIMEO) */
    out[r] = 0; while (r > 0 && (out[r - 1] == '\n' || out[r - 1] == '\r' || out[r - 1] == ' ')) out[--r] = 0;
    ra_last_reply = now_ms();
    if (!strncmp(out, prefix, strlen(prefix))) return 1;
  }
  return 0;
}
static int ra_read(unsigned long a, int n, uint8_t *dst) {
  const char *cmd = ra_use_ram ? "READ_CORE_RAM" : "READ_CORE_MEMORY";
  char q[96], pre[96]; static char rep[8192];
  snprintf(q, sizeof q, "%s %lx %d", cmd, a, n); snprintf(pre, sizeof pre, "%s %lx ", cmd, a);
  if (!ra_ask(q, pre, rep, sizeof rep)) {
    if (now_ms() - ra_last_reply < 500) ra_use_ram = !ra_use_ram;   /* answering other commands, not this one */
    return 0;
  }
  char *p = rep + strlen(pre);
  if (!strncmp(p, "-1", 2)) {
    if (!ra_use_ram && strstr(p, "no memory map")) ra_use_ram = 1;
    pthread_mutex_lock(&ra_mx); snprintf(ra_error, sizeof ra_error, "CAN'T READ GAME MEMORY"); pthread_mutex_unlock(&ra_mx);
    return 0;
  }
  for (int i = 0; i < n; i++) { char *e; long v = strtol(p, &e, 16); if (e == p) return 0; dst[i] = (uint8_t)v; p = e; }
  return 1;
}
typedef struct { unsigned long a; int n; } Rg;
static int rg_cmp(const void *x, const void *y) { const Rg *a = x, *b = y; return a->a < b->a ? -1 : a->a > b->a; }
/* Reads the spec's ranges (merged when close and in the same 1 KB page) and writes hex in spec order. */
static int ra_read_spec(const char *spec, char *hex, size_t hn) {
  Rg want[64], merged[64]; int nw = 0, nm = 0; const char *p = spec;
  while (*p && nw < 64) { char *e; want[nw].a = strtoul(p, &e, 16); want[nw].n = *e == ':' ? (int)strtol(e + 1, &e, 10) : 1; nw++; p = *e ? e + 1 : e; }
  Rg s[64]; memcpy(s, want, sizeof(Rg) * nw); qsort(s, nw, sizeof(Rg), rg_cmp);
  for (int i = 0; i < nw; i++) {
    unsigned long end = s[i].a + s[i].n;
    if (nm && s[i].a <= merged[nm - 1].a + merged[nm - 1].n + 128 && (merged[nm - 1].a >> 10) == ((end - 1) >> 10) && end - merged[nm - 1].a <= 512) {
      if ((long)(end - merged[nm - 1].a) > merged[nm - 1].n) merged[nm - 1].n = (int)(end - merged[nm - 1].a);
    } else merged[nm++] = s[i];
  }
  static uint8_t data[64][512];
  for (int i = 0; i < nm; i++) if (!ra_read(merged[i].a, merged[i].n, data[i])) return 0;
  size_t o = 0; static const char hx[] = "0123456789abcdef";
  for (int i = 0; i < nw; i++) for (int k = 0; k < nm; k++)
    if (want[i].a >= merged[k].a && want[i].a + want[i].n <= merged[k].a + merged[k].n) {
      for (int j = 0; j < want[i].n && o + 2 < hn; j++) { uint8_t v = data[k][want[i].a - merged[k].a + j]; hex[o++] = hx[v >> 4]; hex[o++] = hx[v & 15]; }
      break;
    }
  hex[o] = 0; return 1;
}
static void *net_thread(void *arg) {
  ra_sock = socket(AF_INET, SOCK_DGRAM, 0);
  struct timeval tv = { 0, 120000 }; setsockopt(ra_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  ra_resolve();
  double next_status = 0, next_pal = 0; static char buf[4096], spec[1024], rep[512];
  while (!quit_req) {
    double t = now_ms();
    if (!ra_addr_ok) ra_resolve();
    if (t >= next_status) {
      if (ra_ask("GET_STATUS", "GET_STATUS", rep, sizeof rep)) { pthread_mutex_lock(&ra_mx); snprintf(ra_status, sizeof ra_status, "%s", rep); pthread_mutex_unlock(&ra_mx); }
      next_status = t + 1000;
    }
    pthread_mutex_lock(&ra_mx); snprintf(spec, sizeof spec, "%s", ra_spec); int contentless = !strncmp(ra_status, "GET_STATUS CONTENTLESS", 22); pthread_mutex_unlock(&ra_mx);
    if (spec[0] && !contentless && ra_read_spec(spec, buf, sizeof buf)) {
      pthread_mutex_lock(&ra_mx); snprintf(ra_mem, sizeof ra_mem, "%s", buf); snprintf(ra_spec_cached, sizeof ra_spec_cached, "%s", spec); ra_error[0] = 0; pthread_mutex_unlock(&ra_mx);
      ra_last_ok = now_ms();
      if (t >= next_pal) {
        uint8_t pl[32];
        if (ra_read(0x80003000ul, 32, pl)) { char h[65]; for (int i = 0; i < 32; i++) sprintf(h + i * 2, "%02x", pl[i]); pthread_mutex_lock(&ra_mx); memcpy(ra_pal, h, 65); pthread_mutex_unlock(&ra_mx); }
        next_pal = t + 1000;
      }
    }
    usleep(ra_connected() ? 30000 : 250000);
  }
  return NULL;
}

/* ------------------------------------------------------------------ canvases (same as metroiddual.c) */
typedef struct { int w, h; uint32_t *px; int clip[16][4]; int nclip; unsigned gen; } Canvas;
static Canvas **cvs; static int ncvs, capcvs;
static pthread_mutex_t cv_mx = PTHREAD_MUTEX_INITIALIZER;
static int cv_new(int w, int h) {
  if (w < 1) w = 1; if (h < 1) h = 1;
  if (ncvs == capcvs) { capcvs = capcvs ? capcvs * 2 : 256; cvs = realloc(cvs, capcvs * sizeof *cvs); }
  Canvas *c = calloc(1, sizeof *c); c->w = w; c->h = h; c->px = calloc((size_t)w * h, 4);
  cvs[ncvs] = c; return ncvs++;
}
static Canvas *cv_get(int id) { return id >= 0 && id < ncvs ? cvs[id] : NULL; }
static void cv_size(int id, int w, int h) {
  Canvas *c = cv_get(id); if (!c) return; if (w < 1) w = 1; if (h < 1) h = 1;
  pthread_mutex_lock(&cv_mx); free(c->px); c->w = w; c->h = h; c->px = calloc((size_t)w * h, 4); c->nclip = 0; c->gen++; pthread_mutex_unlock(&cv_mx);
}
static void cv_clip_rect(Canvas *c, int *x0, int *y0, int *x1, int *y1) {
  int cx0 = 0, cy0 = 0, cx1 = c->w, cy1 = c->h;
  if (c->nclip) { int *r = c->clip[c->nclip - 1]; cx0 = r[0]; cy0 = r[1]; cx1 = r[2]; cy1 = r[3]; }
  if (*x0 < cx0) *x0 = cx0; if (*y0 < cy0) *y0 = cy0; if (*x1 > cx1) *x1 = cx1; if (*y1 > cy1) *y1 = cy1;
}
static inline uint32_t blend(uint32_t d, uint32_t s) {
  uint32_t a = s >> 24; if (a == 255) return s; if (a == 0) return d;
  uint32_t ia = 255 - a, r = ((s & 255) * a + (d & 255) * ia) / 255, g = ((s >> 8 & 255) * a + (d >> 8 & 255) * ia) / 255,
           b = ((s >> 16 & 255) * a + (d >> 16 & 255) * ia) / 255, da = d >> 24; da = a + da * ia / 255;
  return da << 24 | b << 16 | g << 8 | r;
}
static void cv_fill(int id, double x, double y, double w, double h, uint32_t col, int replace) {
  Canvas *c = cv_get(id); if (!c) return;
  if (w < 0) { x += w; w = -w; } if (h < 0) { y += h; h = -h; }
  int x0 = (int)lround(x), y0 = (int)lround(y), x1 = (int)lround(x + w), y1 = (int)lround(y + h);
  if (x1 == x0 && w > 0) x1 = x0 + 1; if (y1 == y0 && h > 0) y1 = y0 + 1;
  cv_clip_rect(c, &x0, &y0, &x1, &y1);
  if (x0 >= x1 || y0 >= y1) return;
  if (!replace && (col >> 24) == 0) return;
  for (int yy = y0; yy < y1; yy++) {
    uint32_t *p = c->px + (size_t)yy * c->w;
    if (replace || (col >> 24) == 255) for (int xx = x0; xx < x1; xx++) p[xx] = col;
    else for (int xx = x0; xx < x1; xx++) p[xx] = blend(p[xx], col);
  }
  c->gen++;
}
static void cv_draw(int dst, int src, double dx, double dy, double dw, double dh) {
  Canvas *d = cv_get(dst), *s = cv_get(src); if (!d || !s || dw <= 0 || dh <= 0) return;
  int x0 = (int)lround(dx), y0 = (int)lround(dy), x1 = (int)lround(dx + dw), y1 = (int)lround(dy + dh);
  int bx = x0, by = y0, bw = x1 - x0, bh = y1 - y0; if (bw <= 0 || bh <= 0) return;
  cv_clip_rect(d, &x0, &y0, &x1, &y1);
  for (int yy = y0; yy < y1; yy++) {
    int sy = (int)((long long)(yy - by) * s->h / bh); uint32_t *sp = s->px + (size_t)sy * s->w, *dp = d->px + (size_t)yy * d->w;
    for (int xx = x0; xx < x1; xx++) { uint32_t v = sp[(long long)(xx - bx) * s->w / bw]; if (v >> 24) dp[xx] = blend(dp[xx], v); }
  }
  d->gen++;
}
static void cv_save(int id) { Canvas *c = cv_get(id); if (!c || c->nclip >= 16) return;
  int r[4] = {0, 0, c->w, c->h}; if (c->nclip) memcpy(r, c->clip[c->nclip - 1], sizeof r); memcpy(c->clip[c->nclip++], r, sizeof r); }
static void cv_restore(int id) { Canvas *c = cv_get(id); if (c && c->nclip) c->nclip--; }
static void cv_clip(int id, double x, double y, double w, double h) {
  Canvas *c = cv_get(id); if (!c) return;
  int x0 = (int)lround(x), y0 = (int)lround(y), x1 = (int)lround(x + w), y1 = (int)lround(y + h);
  cv_clip_rect(c, &x0, &y0, &x1, &y1);
  if (!c->nclip) cv_save(id);
  int *r = c->clip[c->nclip - 1]; r[0] = x0; r[1] = y0; r[2] = x1 > x0 ? x1 : x0; r[3] = y1 > y0 ? y1 : y0;
}

/* ------------------------------------------------------------------ page messages + published pixels */
typedef struct Msg { int type; double x, y; int id; struct Msg *next; } Msg;
enum { M_DOWN, M_MOVE, M_UP, M_RESIZE };
static Msg *mq_head, *mq_tail; static pthread_mutex_t mq_mx = PTHREAD_MUTEX_INITIALIZER;
static void post(int type, double x, double y, int id) {
  Msg *m = calloc(1, sizeof *m); m->type = type; m->x = x; m->y = y; m->id = id;
  pthread_mutex_lock(&mq_mx); if (mq_tail) mq_tail->next = m; else mq_head = m; mq_tail = m; pthread_mutex_unlock(&mq_mx);
}
typedef struct { uint32_t *px; int w, h; unsigned gen; } Pub;
static Pub pub; static pthread_mutex_t pub_mx = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ QuickJS page */
static JSRuntime *rt; static JSContext *pctx; static int page_canvas = -1;
static int win_w = 640, win_h = 480;
#define ARGD(i) ({ double _d = 0; JS_ToFloat64(ctx, &_d, argv[i]); _d; })
#define ARGI(i) ({ int32_t _v = 0; JS_ToInt32(ctx, &_v, argv[i]); _v; })
static JSValue js_cnew(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, cv_new(ARGI(0), ARGI(1))); }
static JSValue js_csize(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { cv_size(ARGI(0), ARGI(1), ARGI(2)); return JS_UNDEFINED; }
static JSValue js_cfill(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  uint32_t col = 0; JS_ToUint32(ctx, &col, argv[5]); cv_fill(ARGI(0), ARGD(1), ARGD(2), ARGD(3), ARGD(4), col, argc > 6 && JS_ToBool(ctx, argv[6])); return JS_UNDEFINED; }
static JSValue js_cdraw(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { cv_draw(ARGI(0), ARGI(1), ARGD(2), ARGD(3), ARGD(4), ARGD(5)); return JS_UNDEFINED; }
static JSValue js_csave(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { cv_save(ARGI(0)); return JS_UNDEFINED; }
static JSValue js_crestore(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { cv_restore(ARGI(0)); return JS_UNDEFINED; }
static JSValue js_cclip(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { cv_clip(ARGI(0), ARGD(1), ARGD(2), ARGD(3), ARGD(4)); return JS_UNDEFINED; }
static JSValue js_cimg(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, -1); }   /* no thumbnails */
static JSValue js_read(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *p = JS_ToCString(ctx, argv[0]); char *s = p ? read_file(p, NULL) : NULL; JS_FreeCString(ctx, p);
  JSValue v = s ? JS_NewString(ctx, s) : JS_NULL; free(s); return v; }
static JSValue js_write(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *p = JS_ToCString(ctx, argv[0]); size_t n; const char *s = JS_ToCStringLen(ctx, &n, argv[1]);
  if (p && s) write_file(p, s, n); JS_FreeCString(ctx, p); JS_FreeCString(ctx, s); return JS_UNDEFINED; }
static JSValue js_now(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return JS_NewFloat64(ctx, now_ms()); }
static JSValue js_log(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *s = JS_ToCString(ctx, argv[0]); logm("[js] %s", s ? s : "?"); JS_FreeCString(ctx, s); return JS_UNDEFINED; }

/* --- window.Native */
static long long slot_time[SLOTS + 1];
static void slots_load(void) { char *s = read_file(slots_path, NULL); if (!s) return; char *p = s; for (int i = 1; i <= SLOTS; i++) slot_time[i] = strtoll(p, &p, 10); free(s); }
static void slots_save(void) { char b[256]; int o = 0; for (int i = 1; i <= SLOTS; i++) o += snprintf(b + o, sizeof b - o, "%lld\n", slot_time[i]); write_file(slots_path, b, o); }
static long long wall_ms(void) { struct timeval tv; gettimeofday(&tv, NULL); return (long long)tv.tv_sec * 1000 + tv.tv_usec / 1000; }
static JSValue n_version(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_NewString(ctx, VERSION); }
static JSValue n_true(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_TRUE; }
static JSValue n_false(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_FALSE; }
static JSValue n_undef(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_UNDEFINED; }
static JSValue n_empty(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_NewString(ctx, ""); }
/* RetroArch is answering and has a game loaded (the page then asks for memory; mem() is "" until the first read) */
static JSValue n_running(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  pthread_mutex_lock(&ra_mx); int on = now_ms() - ra_last_reply < 2000 && strncmp(ra_status, "GET_STATUS CONTENTLESS", 22) != 0; pthread_mutex_unlock(&ra_mx);
  return JS_NewBool(ctx, on); }
static JSValue n_mem(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *s = JS_ToCString(ctx, argv[0]); JSValue v;
  pthread_mutex_lock(&ra_mx);
  if (s && strcmp(s, ra_spec)) snprintf(ra_spec, sizeof ra_spec, "%s", s);
  v = JS_NewString(ctx, s && !strcmp(s, ra_spec_cached) && ra_connected() ? ra_mem : "");
  pthread_mutex_unlock(&ra_mx); JS_FreeCString(ctx, s); return v;
}
static JSValue n_pal(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  pthread_mutex_lock(&ra_mx); JSValue v = JS_NewString(ctx, ra_connected() ? ra_pal : ""); pthread_mutex_unlock(&ra_mx); return v; }
static JSValue n_rom(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  int off = ARGI(0), len = ARGI(1);
  if (!rom || off < 0 || len < 0 || (size_t)off + len > rom_len) return JS_NewString(ctx, "");
  char *s = b64enc(rom + off, len); JSValue v = JS_NewString(ctx, s); free(s); return v;
}
static JSValue n_slots(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  char s[512]; int o = snprintf(s, sizeof s, "[");
  for (int i = 1; i <= SLOTS; i++) o += snprintf(s + o, sizeof s - o, "%s{\"n\":%d,\"time\":%lld}", i > 1 ? "," : "", i, slot_time[i]);
  snprintf(s + o, sizeof s - o, "]"); return JS_NewString(ctx, s);
}
static JSValue n_saveslot(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  int n = ARGI(0); if (!ra_connected() || n < 1 || n > SLOTS) return JS_FALSE;
  char c[48]; snprintf(c, sizeof c, "SAVE_STATE_SLOT %d", n); ra_send(c);
  slot_time[n] = wall_ms(); slots_save(); return JS_TRUE;
}
static JSValue n_loadslot(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  int n = ARGI(0); if (!ra_connected() || n < 1 || n > SLOTS) return JS_FALSE;
  char c[48]; snprintf(c, sizeof c, "LOAD_STATE_SLOT %d", n); ra_send(c); return JS_TRUE;
}
static JSValue n_menu(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { ra_send("MENU_TOGGLE"); return JS_UNDEFINED; }
static JSValue n_rastatus(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  char s[400], where[300]; snprintf(where, sizeof where, "%s:%d", ra_host, ra_port);
  pthread_mutex_lock(&ra_mx);
  if (now_ms() - ra_last_reply > 2000) snprintf(s, sizeof s, "WAITING FOR RETROARCH|%s", where);
  else if (!strncmp(ra_status, "GET_STATUS CONTENTLESS", 22)) snprintf(s, sizeof s, "RETROARCH: NO GAME RUNNING|%s", where);
  else if (ra_error[0]) snprintf(s, sizeof s, "%s|%s", ra_error, where);
  else snprintf(s, sizeof s, "CONNECTED|%s", where);
  pthread_mutex_unlock(&ra_mx); return JS_NewString(ctx, s);
}
static JSValue n_rahost(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { char s[300]; snprintf(s, sizeof s, "%s:%d", ra_host, ra_port); return JS_NewString(ctx, s); }

typedef struct { const char *name; JSCFunction *fn; int n; } Fn;
static void add_fns(JSContext *ctx, JSValue obj, const Fn *f) { for (; f->name; f++) JS_SetPropertyStr(ctx, obj, f->name, JS_NewCFunction(ctx, f->fn, f->name, f->n)); }
static const Fn GLOBAL_FNS[] = { {"__cnew", js_cnew, 2}, {"__csize", js_csize, 3}, {"__cfill", js_cfill, 7}, {"__cdraw", js_cdraw, 6},
  {"__csave", js_csave, 1}, {"__crestore", js_crestore, 1}, {"__cclip", js_cclip, 5}, {"__cimg", js_cimg, 1}, {"__read", js_read, 1},
  {"__write", js_write, 2}, {"__now", js_now, 0}, {"__log", js_log, 1}, {0} };
static const Fn NATIVE_FNS[] = { {"version", n_version, 0}, {"dual", n_true, 0}, {"running", n_running, 0}, {"mem", n_mem, 1},
  {"pal", n_pal, 0}, {"chr", n_empty, 0}, {"rom", n_rom, 2}, {"overlay", n_false, 0}, {"close", n_undef, 0}, {"devUsed", n_false, 0},
  {"slots", n_slots, 0}, {"saveSlot", n_saveslot, 1}, {"loadSlot", n_loadslot, 1}, {"thumb", n_empty, 1}, {"menu", n_menu, 0},
  {"raStatus", n_rastatus, 0}, {"raHost", n_rahost, 0}, {0} };

static void js_exc(JSContext *ctx, const char *where) {
  JSValue e = JS_GetException(ctx); const char *s = JS_ToCString(ctx, e); JSValue st = JS_GetPropertyStr(ctx, e, "stack"); const char *ss = JS_ToCString(ctx, st);
  logm("[js] %s: %s\n%s", where, s ? s : "?", ss ? ss : ""); JS_FreeCString(ctx, s); JS_FreeCString(ctx, ss); JS_FreeValue(ctx, st); JS_FreeValue(ctx, e);
}
static int js_file(JSContext *ctx, const char *rel) {
  char p[1200]; snprintf(p, sizeof p, "%s/assets/%s", base_dir, rel);
  size_t n; char *s = read_file(p, &n); if (!s) { logm("missing %s", p); return 0; }
  JSValue v = JS_Eval(ctx, s, n, rel, JS_EVAL_TYPE_GLOBAL); int ok = !JS_IsException(v); if (!ok) js_exc(ctx, rel);
  JS_FreeValue(ctx, v); free(s); return ok;
}
static void js_call(JSContext *ctx, const char *fn, int argc, JSValue *argv) {
  JSValue g = JS_GetGlobalObject(ctx), f = JS_GetPropertyStr(ctx, g, fn);
  if (JS_IsFunction(ctx, f)) { JSValue r = JS_Call(ctx, f, g, argc, argv); if (JS_IsException(r)) js_exc(ctx, fn); JS_FreeValue(ctx, r); }
  JS_FreeValue(ctx, f); JS_FreeValue(ctx, g);
}
static void *js_thread(void *arg) {
  rt = JS_NewRuntime(); JS_SetMaxStackSize(rt, 1 << 20);
  JSContext *ctx = pctx = JS_NewContext(rt);
  JSValue g = JS_GetGlobalObject(ctx);
  add_fns(ctx, g, GLOBAL_FNS);
  JS_SetPropertyStr(ctx, g, "__lsPath", JS_NewString(ctx, ls_path));
  JS_SetPropertyStr(ctx, g, "innerWidth", JS_NewInt32(ctx, win_w));
  JS_SetPropertyStr(ctx, g, "innerHeight", JS_NewInt32(ctx, win_h));
  JSValue o = JS_NewObject(ctx); add_fns(ctx, o, NATIVE_FNS); JS_SetPropertyStr(ctx, g, "Native", o);
  js_file(ctx, "shim.js");
  JSValue mc = JS_GetPropertyStr(ctx, g, "__mainCanvas"), id = JS_GetPropertyStr(ctx, mc, "_id");
  JS_ToInt32(ctx, &page_canvas, id); JS_FreeValue(ctx, id); JS_FreeValue(ctx, mc); JS_FreeValue(ctx, g);
  js_file(ctx, "link/nesgfx.js"); js_file(ctx, "link/metroid.js"); js_file(ctx, "link/mroom.js"); js_file(ctx, "link/link.js");
  while (!quit_req) {
    pthread_mutex_lock(&mq_mx); Msg *m = mq_head; mq_head = mq_tail = NULL; pthread_mutex_unlock(&mq_mx);
    while (m) {
      Msg *nx = m->next;
      if (m->type == M_RESIZE) { win_w = (int)m->x; win_h = (int)m->y; JSValue a[2] = { JS_NewInt32(ctx, win_w), JS_NewInt32(ctx, win_h) }; js_call(ctx, "__resize", 2, a); }
      else { static const char *T[] = { "down", "move", "up" };
        JSValue a[4] = { JS_NewString(ctx, T[m->type]), JS_NewFloat64(ctx, m->x), JS_NewFloat64(ctx, m->y), JS_NewInt32(ctx, m->id) };
        js_call(ctx, "__pointer", 4, a); JS_FreeValue(ctx, a[0]); }
      free(m); m = nx;
    }
    js_call(ctx, "__runTimers", 0, NULL);
    Canvas *c = cv_get(page_canvas);
    if (c && c->gen != pub.gen) {
      pthread_mutex_lock(&pub_mx);
      if (pub.w != c->w || pub.h != c->h) { free(pub.px); pub.px = malloc((size_t)c->w * c->h * 4); pub.w = c->w; pub.h = c->h; }
      memcpy(pub.px, c->px, (size_t)c->w * c->h * 4); pub.gen = c->gen;
      pthread_mutex_unlock(&pub_mx);
    }
    JS_RunGC(rt);
    usleep(8000);
  }
  return NULL;
}

/* ------------------------------------------------------------------ window */
static SDL_Window *win; static SDL_Renderer *ren; static SDL_Texture *tex; static int tw, th; static unsigned tgen = (unsigned)-1;
static char shot_req[256];
static void render(void) {
  pthread_mutex_lock(&pub_mx);
  if (pub.px && pub.gen != tgen) {
    if (!tex || tw != pub.w || th != pub.h) { if (tex) SDL_DestroyTexture(tex); tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, pub.w, pub.h); tw = pub.w; th = pub.h; }
    SDL_UpdateTexture(tex, NULL, pub.px, pub.w * 4); tgen = pub.gen;
  }
  pthread_mutex_unlock(&pub_mx);
  SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
  if (tex) SDL_RenderCopy(ren, tex, NULL, NULL);
  if (shot_req[0]) {
    int w, h; SDL_GetRendererOutputSize(ren, &w, &h);
    SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0) SDL_SaveBMP(sf, shot_req);
    SDL_FreeSurface(sf); shot_req[0] = 0;
  }
  SDL_RenderPresent(ren);
}
static int display_named(const char *out) {
  for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) { const char *n = SDL_GetDisplayName(d); if (n && strstr(n, out)) return d; }
  return -1;
}
/* Under sway (ROCKNIX): this window to the bottom panel, focus back to the top one (RetroArch). */
static void place_window(void) {
  if (!getenv("SWAYSOCK") && !getenv("ML_FORCE_SWAYMSG")) return;
  const char *top = getenv("MD_TOP_OUTPUT") ? getenv("MD_TOP_OUTPUT") : "DSI-2";
  const char *bot = getenv("MD_BOTTOM_OUTPUT") ? getenv("MD_BOTTOM_OUTPUT") : "DSI-1";
  char cmd[1024];
  snprintf(cmd, sizeof cmd, "swaymsg 'output %s power on' >/dev/null 2>&1; "
                            "swaymsg '[title=\"^Metroid Link Bottom$\"] move container to output %s' >/dev/null 2>&1; "
                            "swaymsg '[title=\"^Metroid Link Bottom$\"] fullscreen enable' >/dev/null 2>&1; "
                            "swaymsg 'focus output %s' >/dev/null 2>&1", bot, bot, top);
  logm("sway: %s", cmd); if (system(cmd)) {}
}

/* test script (ML_SCRIPT): "wait N", "tap x y", "drag x0 y0 x1 y1", "shot file.bmp", "quit" */
static FILE *script; static int script_wait;
static void script_step(void) {
  if (!script) return;
  if (script_wait > 0) { script_wait--; return; }
  char ln[300];
  while (fgets(ln, sizeof ln, script)) {
    int x, y, z, w; char b[256];
    if (sscanf(ln, "wait %d", &x) == 1) { script_wait = x; return; }
    if (sscanf(ln, "tap %d %d", &x, &y) == 2) { post(M_DOWN, x, y, 1); post(M_UP, x, y, 1); continue; }
    if (sscanf(ln, "drag %d %d %d %d", &x, &y, &z, &w) == 4) { post(M_DOWN, x, y, 1); for (int k = 1; k <= 8; k++) post(M_MOVE, x + (z - x) * k / 8, y + (w - y) * k / 8, 1); post(M_UP, z, w, 1); continue; }
    if (sscanf(ln, "shot %255s", b) == 1) { snprintf(shot_req, sizeof shot_req, "%s", b); return; }
    if (!strncmp(ln, "quit", 4)) { quit_req = 1; return; }
  }
  fclose(script); script = NULL;
}

static void on_signal(int sig) { quit_req = 1; }
int main(int argc, char **argv) {
  signal(SIGTERM, on_signal); signal(SIGINT, on_signal);
  if (getenv("ML_HOME")) snprintf(base_dir, sizeof base_dir, "%s", getenv("ML_HOME"));
  else { char exe[1024]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); exe[n > 0 ? n : 0] = 0; snprintf(base_dir, sizeof base_dir, "%s", n > 0 ? dirname(exe) : "."); }
  snprintf(ls_path, sizeof ls_path, "%s/link-storage.json", base_dir);
  snprintf(slots_path, sizeof slots_path, "%s/slots.txt", base_dir);
  char lp[1100]; snprintf(lp, sizeof lp, "%s/log.txt", base_dir); logf_ = fopen(lp, "w");
  if (getenv("ML_HOST")) snprintf(ra_host, sizeof ra_host, "%s", getenv("ML_HOST"));
  if (getenv("ML_PORT")) ra_port = atoi(getenv("ML_PORT"));
  logm("Metroid Link %s (Linux) in %s, RetroArch at %s:%d", VERSION, base_dir, ra_host, ra_port);
  find_rom(); slots_load();

  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) { logm("SDL_Init: %s", SDL_GetError()); return 1; }   /* no controller: RetroArch has it */
  int disp = display_named(getenv("MD_BOTTOM_OUTPUT") ? getenv("MD_BOTTOM_OUTPUT") : "DSI-1");
  if (disp < 0) disp = SDL_GetNumVideoDisplays() > 1 ? 1 : 0;
  int w = 640, h = 480; if (getenv("ML_SIZE")) sscanf(getenv("ML_SIZE"), "%dx%d", &w, &h);
  int fs = getenv("ML_WINDOWED") ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
  win = SDL_CreateWindow("Metroid Link Bottom", SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp), w, h, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | fs);
  if (!win) { logm("window: %s", SDL_GetError()); return 1; }
  ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
  if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
  if (!ren) { logm("renderer: %s", SDL_GetError()); return 1; }
  SDL_GetWindowSize(win, &win_w, &win_h);

  pthread_t nt, jt; pthread_create(&nt, NULL, net_thread, NULL); pthread_create(&jt, NULL, js_thread, NULL);
  if (getenv("ML_SCRIPT")) script = fopen(getenv("ML_SCRIPT"), "r");
  unsigned long long loops = 0;
  while (!quit_req) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_QUIT: quit_req = 1; break;
      case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP:
        post(e.type == SDL_FINGERDOWN ? M_DOWN : e.type == SDL_FINGERMOTION ? M_MOVE : M_UP, e.tfinger.x * win_w, e.tfinger.y * win_h, (int)(e.tfinger.fingerId & 0x7FFFFFFF)); break;
      case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
        if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
        post(e.type == SDL_MOUSEBUTTONDOWN ? M_DOWN : M_UP, e.button.x, e.button.y, 1); break;
      case SDL_MOUSEMOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID || !(e.motion.state & SDL_BUTTON_LMASK)) break;
        post(M_MOVE, e.motion.x, e.motion.y, 1); break;
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) { SDL_GetWindowSize(win, &win_w, &win_h); post(M_RESIZE, win_w, win_h, 0); }
        break;
      }
    }
    script_step();
    if (++loops == 10 || loops == 180) place_window();
    render();
    SDL_Delay(16);
  }
  logm("exiting");
  pthread_join(jt, NULL);
  SDL_Quit();
  return 0;
}
