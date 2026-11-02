/* metroiddual.c - Metroid Dual for Linux handhelds with two screens (Anbernic RG DS on ROCKNIX).
 *
 * One process, two windows: "Metroid Dual" (game + launcher/pause menu) and "Metroid Dual Bottom" (Link screen).
 * The emulator is the same FCEUmm core as the Android app (nescore.c). The Link screen and the menu are the
 * Android app's own link.js / menu.js, run unchanged in QuickJS on a second thread with a small canvas shim
 * (shim.js + the canvas functions here). The game ROM is read from the port folder at runtime; nothing of the
 * game ships with the program.
 *
 * Threads: main = SDL events, emulation, audio, rendering. js = QuickJS (both pages). The emulator is guarded by
 * emu_mx; the js thread takes it for save/load/poke/start/quit, and reads RAM without it (as on Android).
 */
#define _GNU_SOURCE
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include <sys/stat.h>
#include <libgen.h>
#include <signal.h>
#include "quickjs.h"
#include "nescore.h"

#define VERSION "0.45"
#define SLOTS 4
#define METROID_PRG_CRC 0x70080810u
#define ROM_FONT 0x1B4D0

/* ------------------------------------------------------------------ paths, log */
static char base_dir[1024], state_dir[1100], rom_path[1100], cfg_path[1100];
static FILE *logf_;
void nes_log(const char *msg) { fprintf(stderr, "[core] %s", msg); if (logf_) { fprintf(logf_, "[core] %s", msg); fflush(logf_); } }
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
static long long file_mtime_ms(const char *p) { struct stat st; if (stat(p, &st)) return 0; return (long long)st.st_mtim.tv_sec * 1000 + st.st_mtim.tv_nsec / 1000000; }

/* ------------------------------------------------------------------ settings (k=v file) */
typedef struct { const char *k; char type; const char *def; } SetDef;   /* type b=bool i=int s=string */
static const SetDef SETDEFS[] = {
  {"aspect", 's', "4:3"}, {"bezels", 'b', "true"}, {"integer", 'b', "false"}, {"palette", 's', "default"},
  {"touch", 's', "off"}, {"padOpacity", 'i', "70"}, {"split", 's', "auto"}, {"swap", 'b', "false"}, {"gap", 'i', "0"},
  {"side", 'b', "false"}, {"useExternal", 'b', "true"}, {"skipLauncher", 'b', "false"}, {"audio", 'b', "true"},
  {"stats", 'b', "false"}, {"dev3d", 'b', "false"}, {"cam3d", 's', "tilt"}, {"depth3d", 'i', "16"}, {"back3d", 's', "near"},
  {"cave3d", 'b', "true"}, {"light3d", 'b', "true"}, {"border3d", 'b', "true"}, {"codeEnergy", 'b', "false"},
  {"codeMissiles", 'b', "false"}, {"overclock", 'b', "true"}, {"devUnlock", 'b', "false"}, {"keyBinds", 's', ""},
};
#define NSET (int)(sizeof SETDEFS / sizeof SETDEFS[0])
static char setval[NSET][2048];
static pthread_mutex_t set_mx = PTHREAD_MUTEX_INITIALIZER;
static int set_idx(const char *k) { for (int i = 0; i < NSET; i++) if (!strcmp(SETDEFS[i].k, k)) return i; return -1; }
static void settings_save(void) {
  char buf[16384]; int o = 0;
  for (int i = 0; i < NSET; i++) o += snprintf(buf + o, sizeof buf - o, "%s=%s\n", SETDEFS[i].k, setval[i]);
  write_file(cfg_path, buf, o);
}
static void settings_load(void) {
  for (int i = 0; i < NSET; i++) snprintf(setval[i], sizeof setval[i], "%s", SETDEFS[i].def);
  char *t = read_file(cfg_path, NULL); if (!t) return;
  for (char *line = strtok(t, "\n"); line; line = strtok(NULL, "\n")) {
    char *eq = strchr(line, '='); if (!eq) continue; *eq = 0;
    int i = set_idx(line); if (i >= 0) snprintf(setval[i], sizeof setval[i], "%s", eq + 1);
  }
  free(t);
}
static const char *sget(const char *k) { int i = set_idx(k); return i < 0 ? "" : setval[i]; }
static int sbool(const char *k) { return !strcmp(sget(k), "true"); }
static void sset(const char *k, const char *v) {
  pthread_mutex_lock(&set_mx);
  int i = set_idx(k); if (i >= 0) { snprintf(setval[i], sizeof setval[i], "%s", v); settings_save(); }
  pthread_mutex_unlock(&set_mx);
}

/* ------------------------------------------------------------------ shared state */
static pthread_mutex_t emu_mx = PTHREAD_MUTEX_INITIALIZER;
static volatile int started, menu_open = 1, quit_req, dev_used, code_energy, code_missiles;
static volatile int rebind_target = -1;
static int gl3d_ok;                    /* the 3D view can be drawn (GLES2 works) */
static uint8_t *rom; static size_t rom_len;       /* the player's ROM, for the pages (font, map, items) */
static char last_frame_ok;
static uint16_t last_frame[256 * 240]; static int last_w = 256, last_h = 224;
static char osd_text[64]; static double osd_until;
static void osd(const char *s) { snprintf(osd_text, sizeof osd_text, "%s", s); osd_until = now_ms() + 1500; }

static int peek(unsigned a) { return nes_peek(a); }
static void poke(unsigned a, unsigned v) { nes_poke(a, v); }
static int in_game(void) { return peek(0x1D) == 0 && peek(0x1E) == 3; }

/* ------------------------------------------------------------------ ROM */
static uint32_t crc32(const uint8_t *d, size_t n) {
  uint32_t c = 0xFFFFFFFFu;
  for (size_t i = 0; i < n; i++) { c ^= d[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1)); }
  return ~c;
}
/* Finds the ROM in the port folder: "Metroid (USA).nes" if there, otherwise any .nes file. */
static int find_rom(void) {
  static const char *names[] = { "Metroid (USA).nes", "metroid.nes", "Metroid.nes", NULL };
  for (int i = 0; names[i]; i++) {
    snprintf(rom_path, sizeof rom_path, "%s/%s", base_dir, names[i]);
    if (access(rom_path, R_OK) == 0) goto found;
  }
  char cmd[1300]; snprintf(cmd, sizeof cmd, "ls -1 \"%s\" 2>/dev/null", base_dir);
  FILE *p = popen(cmd, "r"); rom_path[0] = 0;
  if (p) { char ln[512]; while (fgets(ln, sizeof ln, p)) { ln[strcspn(ln, "\n")] = 0; size_t l = strlen(ln);
      if (l > 4 && !strcasecmp(ln + l - 4, ".nes")) { snprintf(rom_path, sizeof rom_path, "%s/%s", base_dir, ln); break; } } pclose(p); }
  if (!rom_path[0]) { free(rom); rom = NULL; rom_len = 0; return 0; }
found:
  free(rom); rom = (uint8_t *)read_file(rom_path, &rom_len);
  if (rom && (rom_len < 16 + 0x20000 || rom_len > 0x100000)) { free(rom); rom = NULL; rom_len = 0; }
  logm("ROM: %s (%zu bytes)", rom ? rom_path : "none", rom_len);
  return rom != NULL;
}

/* ------------------------------------------------------------------ base64 */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char *b64enc(const uint8_t *d, size_t n) {
  char *o = malloc(4 * ((n + 2) / 3) + 1), *p = o;
  size_t i = 0;
  for (; i + 2 < n; i += 3) { uint32_t v = d[i] << 16 | d[i + 1] << 8 | d[i + 2];
    *p++ = B64[v >> 18 & 63]; *p++ = B64[v >> 12 & 63]; *p++ = B64[v >> 6 & 63]; *p++ = B64[v & 63]; }
  if (i < n) { uint32_t v = d[i] << 16 | (i + 1 < n ? d[i + 1] << 8 : 0);
    *p++ = B64[v >> 18 & 63]; *p++ = B64[v >> 12 & 63]; *p++ = i + 1 < n ? B64[v >> 6 & 63] : '='; *p++ = '='; }
  *p = 0; return o;
}

/* ------------------------------------------------------------------ canvases */
typedef struct { int w, h; uint32_t *px; int clip[16][4]; int nclip; unsigned gen; } Canvas;
static Canvas **cvs; static int ncvs, capcvs;
static pthread_mutex_t cv_mx = PTHREAD_MUTEX_INITIALIZER;   /* guards pixel reads by the main thread */
static int cv_new(int w, int h) {
  if (w < 1) w = 1; if (h < 1) h = 1;
  if (ncvs == capcvs) { capcvs = capcvs ? capcvs * 2 : 256; cvs = realloc(cvs, capcvs * sizeof *cvs); }
  Canvas *c = calloc(1, sizeof *c); c->w = w; c->h = h; c->px = calloc((size_t)w * h, 4);
  cvs[ncvs] = c; return ncvs++;
}
static Canvas *cv_get(int id) { return id >= 0 && id < ncvs ? cvs[id] : NULL; }
static void cv_size(int id, int w, int h) {
  Canvas *c = cv_get(id); if (!c) return; if (w < 1) w = 1; if (h < 1) h = 1;
  pthread_mutex_lock(&cv_mx);
  free(c->px); c->w = w; c->h = h; c->px = calloc((size_t)w * h, 4); c->nclip = 0; c->gen++;
  pthread_mutex_unlock(&cv_mx);
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
  int r[4] = {0, 0, c->w, c->h}; if (c->nclip) memcpy(r, c->clip[c->nclip - 1], sizeof r);
  memcpy(c->clip[c->nclip++], r, sizeof r); }
static void cv_restore(int id) { Canvas *c = cv_get(id); if (c && c->nclip) c->nclip--; }
static void cv_clip(int id, double x, double y, double w, double h) {
  Canvas *c = cv_get(id); if (!c) return;
  int x0 = (int)lround(x), y0 = (int)lround(y), x1 = (int)lround(x + w), y1 = (int)lround(y + h);
  cv_clip_rect(c, &x0, &y0, &x1, &y1);
  if (!c->nclip) cv_save(id);
  int *r = c->clip[c->nclip - 1]; r[0] = x0; r[1] = y0; r[2] = x1 > x0 ? x1 : x0; r[3] = y1 > y0 ? y1 : y0;
}
static inline uint32_t rgb565_to_rgba(uint16_t p) {
  uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
  r = r << 3 | r >> 2; g = g << 2 | g >> 4; b = b << 3 | b >> 2;
  return 0xFF000000u | b << 16 | g << 8 | r;
}

/* ------------------------------------------------------------------ save states */
static void state_path(char *out, size_t n, const char *name, const char *ext) { snprintf(out, n, "%s/%s.%s", state_dir, name, ext); }
/* Call with emu_mx held. */
static int save_state(const char *name) {
  size_t n = nes_state_size(); if (!n) return 0;
  void *buf = malloc(n); int ok = nes_state_save(buf, n);
  char p[1200]; state_path(p, sizeof p, name, "st");
  if (ok) ok = write_file(p, buf, n) == 0;
  free(buf);
  if (ok && last_frame_ok) {      /* thumbnail: raw RGB565 frame with a small header */
    state_path(p, sizeof p, name, "thumb");
    size_t sz = 8 + (size_t)last_w * last_h * 2; uint8_t *t = malloc(sz);
    int32_t hd[2] = { last_w, last_h }; memcpy(t, hd, 8); memcpy(t + 8, last_frame, sz - 8);
    write_file(p, t, sz); free(t);
  }
  return ok;
}
static int load_state(const char *name) {
  char p[1200]; state_path(p, sizeof p, name, "st");
  size_t n; char *b = read_file(p, &n); if (!b) return 0;
  int ok = nes_state_load(b, n); free(b); return ok;
}
static int slot_exists(int n) { char p[1200], nm[16]; snprintf(nm, sizeof nm, "slot%d", n); state_path(p, sizeof p, nm, "st"); return access(p, R_OK) == 0; }
static long long slot_time(int n) { char p[1200], nm[16]; snprintf(nm, sizeof nm, "slot%d", n); state_path(p, sizeof p, nm, "st"); return file_mtime_ms(p); }

/* ------------------------------------------------------------------ JS page messages (main -> js thread) */
typedef struct Msg { int page; int type; double x, y; int id; char s[256]; struct Msg *next; } Msg;  /* page 0 menu, 1 link */
enum { M_DOWN, M_MOVE, M_UP, M_EVAL, M_RESIZE };
static Msg *mq_head, *mq_tail; static pthread_mutex_t mq_mx = PTHREAD_MUTEX_INITIALIZER;
static void post(int page, int type, double x, double y, int id, const char *s) {
  Msg *m = calloc(1, sizeof *m); m->page = page; m->type = type; m->x = x; m->y = y; m->id = id;
  if (s) snprintf(m->s, sizeof m->s, "%s", s);
  pthread_mutex_lock(&mq_mx); if (mq_tail) mq_tail->next = m; else mq_head = m; mq_tail = m; pthread_mutex_unlock(&mq_mx);
}
static void eval_menu(const char *js) { post(0, M_EVAL, 0, 0, 0, js); }
static void eval_link(const char *js) { post(1, M_EVAL, 0, 0, 0, js); }

/* Pages publish their main canvas here; the main thread uploads when gen changes. */
typedef struct { uint32_t *px; int w, h; unsigned gen; } Pub;
static Pub pub[2]; static pthread_mutex_t pub_mx = PTHREAD_MUTEX_INITIALIZER;

/* ------------------------------------------------------------------ game control (callable from either thread) */
static void open_menu(void) {
  if (menu_open) return;
  menu_open = 1;
  eval_menu("window.show&&show('pause')");
}
static void close_menu(void) { if (started) menu_open = 0; }
static void start_game(int cont) {
  pthread_mutex_lock(&emu_mx);
  if (!rom) { pthread_mutex_unlock(&emu_mx); eval_menu("window.toast&&toast('NO ROM IN THE PORT FOLDER')"); return; }
  nes_set_palette(sget("palette"));
  nes_set_overclock(!strcmp(sget("overclock"), "true"));
  int r = nes_load(rom_path);
  if (r == 0) {
    started = 1;
    if (cont) load_state("auto");
  }
  pthread_mutex_unlock(&emu_mx);
  if (r) { eval_menu("window.toast&&toast('COULD NOT LOAD ROM')"); return; }
  menu_open = 0;
  eval_link("window.onSession&&onSession()");
}
static void quit_to_launcher(void) {
  pthread_mutex_lock(&emu_mx);
  if (started) { save_state("auto"); nes_unload(); }
  started = 0; last_frame_ok = 0;
  pthread_mutex_unlock(&emu_mx);
  menu_open = 1;
  eval_menu("window.show&&show('launcher')");
}

static void load_codes(void) {
  int unlocked = sbool("devUnlock");           /* hidden and off unless unlocked (Konami code) */
  code_energy = unlocked && sbool("codeEnergy"); code_missiles = unlocked && sbool("codeMissiles");
  if (code_energy || code_missiles) dev_used = 1;
}
/* Every frame while an always-on playtest code is enabled (emu_mx held). */
static void apply_codes(void) {
  if (peek(0x1D) != 0) return;
  if (code_energy) { int t = peek(0x6877); if (t > 6) t = 6; poke(0x107, (t << 4) | 9); poke(0x106, 0x99); }
  if (code_missiles) { int m = peek(0x687A); if (m > 0) poke(0x6879, m); }
}
static const char *dev_action(const char *what) {
  static char msg[64];
  if (!started) return "START A GAME FIRST";
  dev_used = 1; snprintf(msg, sizeof msg, "DONE");
  pthread_mutex_lock(&emu_mx);
  if (!strcmp(what, "items")) { poke(0x6878, 0x7F); poke(0x6877, 6); poke(0x687A, 255); poke(0x6879, 255); poke(0x107, 0x69); poke(0x106, 0x99); snprintf(msg, sizeof msg, "ALL ITEMS"); }
  else if (!strcmp(what, "ice")) { int g = peek(0x6878); poke(0x6878, (g & 0x3F) | ((g & 0x80) ? 0x40 : 0x80)); snprintf(msg, sizeof msg, (g & 0x80) ? "WAVE BEAM" : "ICE BEAM"); }
  else if (!strcmp(what, "refill")) { int t = peek(0x6877); if (t > 6) t = 6; poke(0x107, (t << 4) | 9); poke(0x106, 0x99); poke(0x6879, peek(0x687A)); snprintf(msg, sizeof msg, "REFILLED"); }
  else if (!strcmp(what, "kraid")) { poke(0x687B, peek(0x687B) | 1); snprintf(msg, sizeof msg, "KRAID DOWN"); }
  else if (!strcmp(what, "ridley")) { poke(0x687C, peek(0x687C) | 2); snprintf(msg, sizeof msg, "RIDLEY DOWN"); }
  else if (!strcmp(what, "escape")) { poke(0x10B, 0x09); poke(0x10A, 0x90); snprintf(msg, sizeof msg, "ESCAPE 099"); }
  else if (!strcmp(what, "report")) { int on = peek(0x6883) == 1 ? 0 : 1; poke(0x6883, on); snprintf(msg, sizeof msg, on ? "REPORT ON" : "REPORT OFF"); }
  else if (!strncmp(what, "time:", 5)) {
    int min = atoi(what + 5); long long ticks = (long long)(min * 60 * 60.0988 / 256);
    poke(0x687D, ticks % 208); poke(0x687E, (ticks / 208) % 256); poke(0x687F, ticks / 208 / 256);
    snprintf(msg, sizeof msg, "TIME %d:%02d", min / 60, min % 60);
  }
  pthread_mutex_unlock(&emu_mx);
  eval_link("window.onDev&&onDev()");
  return msg;
}

/* ------------------------------------------------------------------ key bindings */
/* Input ids: "b:<controller button name>", "t:lefttrigger", "k:<scancode>", "j:<joystick button>" */
static const char *BUTTON_NAMES[] = { "UP", "DOWN", "LEFT", "RIGHT", "B", "A", "SELECT", "START", "TURBO B", "TURBO A",
                                      "MENU", "FAST", "SAVE", "LOAD", "3D VIEW", "3D DEPTH +", "3D DEPTH -" };
#define NBTN 17
static const unsigned BUTTON_BITS[] = { 1u << 4, 1u << 5, 1u << 6, 1u << 7, 1u, 1u << 8, 1u << 2, 1u << 3, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
typedef struct { char id[32]; int btn; } Bind;
static Bind binds[96]; static int nbinds;
static void binds_defaults(void) {
  static const struct { const char *id; int b; } D[] = {
    {"b:dpup", 0}, {"b:dpdown", 1}, {"b:dpleft", 2}, {"b:dpright", 3}, {"b:a", 4}, {"b:b", 5}, {"b:x", 8}, {"b:y", 9},
    {"b:back", 6}, {"b:start", 7}, {"b:guide", 10}, {"b:leftshoulder", 10}, {"b:rightshoulder", 11}, {"t:lefttrigger", 12},
    {"t:righttrigger", 13}, {"b:rightstick", 14},
    {"k:82", 0}, {"k:81", 1}, {"k:80", 2}, {"k:79", 3}, {"k:29", 4}, {"k:27", 5}, {"k:229", 6}, {"k:40", 7}, {"k:41", 10},
    {"k:43", 11}, {"k:62", 12}, {"k:64", 13}, {"k:59", 14}, {"k:75", 15}, {"k:78", 16} };
  nbinds = 0;
  for (size_t i = 0; i < sizeof D / sizeof D[0]; i++) { snprintf(binds[nbinds].id, 32, "%s", D[i].id); binds[nbinds++].btn = D[i].b; }
}
static void binds_save(void) {
  char b[4096]; int o = 0;
  for (int i = 0; i < nbinds; i++) o += snprintf(b + o, sizeof b - o, "%s%s/%d", i ? ";" : "", binds[i].id, binds[i].btn);
  sset("keyBinds", b);
}
static void binds_load(void) {
  const char *s = sget("keyBinds");
  if (!*s) { binds_defaults(); return; }
  nbinds = 0; char t[2048]; snprintf(t, sizeof t, "%s", s);
  for (char *e = strtok(t, ";"); e && nbinds < 96; e = strtok(NULL, ";")) {
    char *sl = strrchr(e, '/'); if (!sl) continue; *sl = 0;
    snprintf(binds[nbinds].id, 32, "%s", e); binds[nbinds++].btn = atoi(sl + 1);
  }
  if (!nbinds) binds_defaults();
}
static int bind_of(const char *id) { for (int i = 0; i < nbinds; i++) if (!strcmp(binds[i].id, id)) return binds[i].btn; return -1; }
static void bind_assign(const char *id, int btn) {
  int prev = bind_of(id); if (prev == btn) return;
  int other = -1;
  for (int i = 0; i < nbinds; i++) if (binds[i].btn == btn && binds[i].id[0] == id[0] && strcmp(binds[i].id, id)) { other = i; break; }
  if (prev >= 0 && other >= 0) binds[other].btn = prev;                          /* swap within the same device class */
  else if (other >= 0) { binds[other] = binds[--nbinds]; }
  for (int i = 0; i < nbinds; i++) if (!strcmp(binds[i].id, id)) { binds[i].btn = btn; binds_save(); return; }
  if (nbinds < 96) { snprintf(binds[nbinds].id, 32, "%s", id); binds[nbinds++].btn = btn; }
  binds_save();
}
static void bind_label(const char *id, char *out, size_t n) {
  if (id[0] == 'k') { const char *nm = SDL_GetScancodeName((SDL_Scancode)atoi(id + 2)); snprintf(out, n, "%s", nm && *nm ? nm : "KEY"); }
  else if (id[0] == 'j') snprintf(out, n, "JOY %s", id + 2);
  else snprintf(out, n, "%s", id + 2);
  for (char *p = out; *p; p++) *p = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
  if (!strcmp(out, "DPUP")) snprintf(out, n, "DPAD UP"); else if (!strcmp(out, "DPDOWN")) snprintf(out, n, "DPAD DOWN");
  else if (!strcmp(out, "DPLEFT")) snprintf(out, n, "DPAD LEFT"); else if (!strcmp(out, "DPRIGHT")) snprintf(out, n, "DPAD RIGHT");
  else if (!strcmp(out, "LEFTSHOULDER")) snprintf(out, n, "L1"); else if (!strcmp(out, "RIGHTSHOULDER")) snprintf(out, n, "R1");
  else if (!strcmp(out, "LEFTTRIGGER")) snprintf(out, n, "L2"); else if (!strcmp(out, "RIGHTTRIGGER")) snprintf(out, n, "R2");
  else if (!strcmp(out, "LEFTSTICK")) snprintf(out, n, "L3"); else if (!strcmp(out, "RIGHTSTICK")) snprintf(out, n, "R3");
  else if (!strcmp(out, "BACK")) snprintf(out, n, "SELECT"); else if (!strcmp(out, "GUIDE")) snprintf(out, n, "HOME");
  for (char *p = out; *p; p++) if (*p == '"' || *p == '\\' || (unsigned char)*p < 32) *p = '?';   /* keep the JSON valid */
}

/* ------------------------------------------------------------------ QuickJS pages */
static JSRuntime *rt;
static JSContext *pctx[2];
static int page_canvas[2] = {-1, -1};
static int win_w[2] = {640, 640}, win_h[2] = {480, 480};   /* logical size of the window each page shows in */
static char ls_path[2][1100];

#define ARGD(i) ({ double _d = 0; JS_ToFloat64(ctx, &_d, argv[i]); _d; })
#define ARGI(i) ({ int32_t _v = 0; JS_ToInt32(ctx, &_v, argv[i]); _v; })
static JSValue js_cnew(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { return JS_NewInt32(ctx, cv_new(ARGI(0), ARGI(1))); }
static JSValue js_csize(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { cv_size(ARGI(0), ARGI(1), ARGI(2)); return JS_UNDEFINED; }
static JSValue js_cfill(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
  uint32_t col = 0; JS_ToUint32(ctx, &col, argv[5]);
  cv_fill(ARGI(0), ARGD(1), ARGD(2), ARGD(3), ARGD(4), col, argc > 6 && JS_ToBool(ctx, argv[6])); return JS_UNDEFINED; }
static JSValue js_cdraw(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { cv_draw(ARGI(0), ARGI(1), ARGD(2), ARGD(3), ARGD(4), ARGD(5)); return JS_UNDEFINED; }
static JSValue js_csave(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { cv_save(ARGI(0)); return JS_UNDEFINED; }
static JSValue js_crestore(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { cv_restore(ARGI(0)); return JS_UNDEFINED; }
static JSValue js_cclip(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { cv_clip(ARGI(0), ARGD(1), ARGD(2), ARGD(3), ARGD(4)); return JS_UNDEFINED; }
/* "slot:N" -> a canvas holding that slot's thumbnail */
static JSValue js_cimg(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
  const char *s = JS_ToCString(ctx, argv[0]); int id = -1;
  if (s && !strncmp(s, "slot:", 5)) {
    char p[1200], nm[16]; snprintf(nm, sizeof nm, "slot%d", atoi(s + 5)); state_path(p, sizeof p, nm, "thumb");
    size_t n; char *b = read_file(p, &n);
    if (b && n > 8) { int32_t hd[2]; memcpy(hd, b, 8);
      if (hd[0] > 0 && hd[1] > 0 && hd[0] <= 256 && hd[1] <= 240 && n >= 8 + (size_t)hd[0] * hd[1] * 2) {
        id = cv_new(hd[0], hd[1]); Canvas *c = cv_get(id); const uint16_t *src = (const uint16_t *)(b + 8);
        for (int i = 0; i < hd[0] * hd[1]; i++) c->px[i] = rgb565_to_rgba(src[i]);
      } }
    free(b);
  }
  JS_FreeCString(ctx, s); return JS_NewInt32(ctx, id);
}
static JSValue js_read(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
  const char *p = JS_ToCString(ctx, argv[0]); char *t = p ? read_file(p, NULL) : NULL; JS_FreeCString(ctx, p);
  JSValue v = t ? JS_NewString(ctx, t) : JS_NULL; free(t); return v; }
static JSValue js_write(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
  const char *p = JS_ToCString(ctx, argv[0]); size_t n; const char *t = JS_ToCStringLen(ctx, &n, argv[1]);
  if (p && t) write_file(p, t, n); JS_FreeCString(ctx, p); JS_FreeCString(ctx, t); return JS_UNDEFINED; }
static JSValue js_now(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) { return JS_NewFloat64(ctx, now_ms()); }
static JSValue js_log(JSContext *ctx, JSValueConst this, int argc, JSValueConst *argv) {
  const char *s = JS_ToCString(ctx, argv[0]); logm("[js] %s", s ? s : "?"); JS_FreeCString(ctx, s); return JS_UNDEFINED; }

static JSValue rom_b64(JSContext *ctx, int off, int len) {
  if (!rom || off < 0 || len < 0 || (size_t)off + len > rom_len) return JS_NewString(ctx, "");
  char *s = b64enc(rom + off, len); JSValue v = JS_NewString(ctx, s); free(s); return v;
}
static JSValue str_ret(JSContext *ctx, char *s) { JSValue v = JS_NewString(ctx, s); free(s); return v; }

/* --- window.Native (Link page) */
static JSValue n_version(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_NewString(ctx, VERSION); }
static JSValue n_dual(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_TRUE; }
static JSValue n_running(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_NewBool(ctx, started); }
static JSValue n_mem(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  if (!started) return JS_NewString(ctx, "");
  const char *s = JS_ToCString(ctx, argv[0]); static char out[16384 * 2 + 8]; int o = 0; const char *p = s;
  static const char hx[] = "0123456789abcdef";
  while (p && *p) {
    char *end; unsigned ad = strtoul(p, &end, 16), n = 1;
    if (*end == ':') n = strtoul(end + 1, &end, 10);
    for (unsigned i = 0; i < n && o < 16384 * 2; i++) { int v = peek(ad + i) & 255; out[o++] = hx[v >> 4]; out[o++] = hx[v & 15]; }
    p = *end ? end + 1 : end;
  }
  out[o] = 0; JS_FreeCString(ctx, s); return JS_NewString(ctx, out);
}
static JSValue n_pal(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  if (!started) return JS_NewString(ctx, "");
  char s[65]; for (int i = 0; i < 32; i++) sprintf(s + i * 2, "%02x", nes_ppu_peek(0x3F00 + i) & 255);
  return JS_NewString(ctx, s);
}
static JSValue n_chr(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  static uint8_t chr[0x1000]; static int have;
  if (!started) return JS_NewString(ctx, "");
  if (in_game()) { for (int i = 0; i < 0x1000; i++) chr[i] = nes_ppu_peek(i); have = 1; }
  if (!have) return JS_NewString(ctx, "");
  return str_ret(ctx, b64enc(chr, 0x1000));
}
static JSValue n_rom(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { return rom_b64(ctx, ARGI(0), ARGI(1)); }
static JSValue n_overlay(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_FALSE; }
static JSValue n_close(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_UNDEFINED; }
static JSValue n_devused(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return JS_NewBool(ctx, dev_used); }
static JSValue n_slots(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  char s[512]; int o = snprintf(s, sizeof s, "[");
  for (int i = 1; i <= SLOTS; i++) o += snprintf(s + o, sizeof s - o, "%s{\"n\":%d,\"time\":%lld}", i > 1 ? "," : "", i, slot_time(i));
  snprintf(s + o, sizeof s - o, "]"); return JS_NewString(ctx, s);
}
static JSValue n_saveslot(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  int n = ARGI(0); if (!started || n < 1 || n > SLOTS) return JS_FALSE;
  char nm[16]; snprintf(nm, sizeof nm, "slot%d", n);
  pthread_mutex_lock(&emu_mx); int ok = save_state(nm); pthread_mutex_unlock(&emu_mx);
  return JS_NewBool(ctx, ok);
}
static JSValue n_loadslot(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  int n = ARGI(0); if (!started || !slot_exists(n)) return JS_FALSE;
  char nm[16]; snprintf(nm, sizeof nm, "slot%d", n);
  pthread_mutex_lock(&emu_mx); int ok = load_state(nm); pthread_mutex_unlock(&emu_mx);
  return JS_NewBool(ctx, ok);
}
static JSValue n_settings3d(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  char b[256]; snprintf(b, sizeof b, "{\"dev3d\":%s,\"cam3d\":\"%s\",\"depth3d\":%d,\"border3d\":%s,\"back3d\":\"%s\",\"cave3d\":%s,\"light3d\":%s}",
    sget("dev3d"), sget("cam3d"), atoi(sget("depth3d")), sget("border3d"), sget("back3d"), sget("cave3d"), sget("light3d"));
  return JS_NewString(ctx, b);
}
static JSValue n_set3d(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *k = JS_ToCString(ctx, argv[0]), *v = JS_ToCString(ctx, argv[1]);
  if (k && v) {
    if (!strcmp(k, "depth3d")) { int d = atoi(v); if (d < 4) d = 4; if (d > 48) d = 48; char b[16]; snprintf(b, sizeof b, "%d", d); sset(k, b); }
    else sset(k, v);
  }
  JS_FreeCString(ctx, k); JS_FreeCString(ctx, v); return JS_UNDEFINED;
}
static JSValue n_menu(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { if (started) open_menu(); return JS_UNDEFINED; }

/* --- window.Host (menu page) */
static JSValue h_info(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  char *s = malloc(16384); int o = 0;
  o += snprintf(s + o, 16384 - o, "{\"version\":\"%s\",\"mode\":\"%s\",\"linux\":true,\"no3d\":%s,\"linkShown\":true,\"devUsed\":%s,",
                VERSION, started ? "pause" : "launcher", gl3d_ok ? "false" : "true", dev_used ? "true" : "false");
  if (rom) {
    uint32_t crc = crc32(rom + 16, rom_len - 16); const char *nm = strrchr(rom_path, '/');
    o += snprintf(s + o, 16384 - o, "\"rom\":{\"present\":true,\"ok\":%s,\"crc\":\"%x\",\"name\":\"%s\"},", crc == METROID_PRG_CRC ? "true" : "false", crc, nm ? nm + 1 : "");
  } else o += snprintf(s + o, 16384 - o, "\"rom\":{\"present\":false},");
  char ap[1200]; state_path(ap, sizeof ap, "auto", "st");
  o += snprintf(s + o, 16384 - o, "\"auto\":%s,\"settings\":{", access(ap, R_OK) == 0 ? "true" : "false");
  pthread_mutex_lock(&set_mx);
  for (int i = 0; i < NSET; i++) {
    if (!strcmp(SETDEFS[i].k, "keyBinds")) continue;
    if (SETDEFS[i].type == 's') o += snprintf(s + o, 16384 - o, "\"%s\":\"%s\",", SETDEFS[i].k, setval[i]);
    else o += snprintf(s + o, 16384 - o, "\"%s\":%s,", SETDEFS[i].k, setval[i][0] ? setval[i] : "0");
  }
  pthread_mutex_unlock(&set_mx);
  if (s[o - 1] == ',') o--;
  int sw = win_w[0], sh = win_h[0];
  o += snprintf(s + o, 16384 - o, "},\"screens\":{\"how\":\"hinge\",\"source\":\"linux\",\"model\":\"%s\",\"size\":\"%dX%d\"},\"slots\":[",
                getenv("MD_MODEL") ? getenv("MD_MODEL") : "", sw, sh);
  for (int i = 1; i <= SLOTS; i++) o += snprintf(s + o, 16384 - o, "%s{\"n\":%d,\"time\":%lld}", i > 1 ? "," : "", i, slot_time(i));
  snprintf(s + o, 16384 - o, "]}");
  return str_ret(ctx, s);
}
static JSValue h_font(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { return rom_b64(ctx, ROM_FONT, 0x400); }
static JSValue h_pickrom(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  int had = rom != NULL;
  if (find_rom()) { eval_menu("window.toast&&toast('ROM FOUND')"); eval_link("window.onRom&&onRom()"); }
  else eval_menu("window.toast&&toast('PUT METROID (USA).NES IN THE PORT FOLDER')");
  (void)had; return JS_UNDEFINED;
}
static JSValue h_start(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { start_game(JS_ToBool(ctx, argv[0])); return JS_UNDEFINED; }
static JSValue h_resume(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { close_menu(); return JS_UNDEFINED; }
static JSValue h_reset(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  pthread_mutex_lock(&emu_mx); nes_reset(); pthread_mutex_unlock(&emu_mx); close_menu(); return JS_UNDEFINED; }
static JSValue h_save(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  if (!started) return JS_FALSE; char nm[16]; snprintf(nm, sizeof nm, "slot%d", ARGI(0));
  pthread_mutex_lock(&emu_mx); int ok = save_state(nm); pthread_mutex_unlock(&emu_mx); return JS_NewBool(ctx, ok);
}
static JSValue h_load(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  int n = ARGI(0); if (!slot_exists(n)) return JS_FALSE;
  char nm[16]; snprintf(nm, sizeof nm, "slot%d", n);
  if (!started) start_game(0);
  if (!started) return JS_FALSE;
  pthread_mutex_lock(&emu_mx); int ok = load_state(nm); pthread_mutex_unlock(&emu_mx);
  if (ok) close_menu();
  return JS_NewBool(ctx, ok);
}
static JSValue h_thumb(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  char p[1200], nm[16]; int n = ARGI(0); snprintf(nm, sizeof nm, "slot%d", n); state_path(p, sizeof p, nm, "thumb");
  if (access(p, R_OK)) return JS_NewString(ctx, "");
  char u[16]; snprintf(u, sizeof u, "slot:%d", n); return JS_NewString(ctx, u);
}
static JSValue h_quit(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { quit_to_launcher(); return JS_UNDEFINED; }
static JSValue h_exit(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { quit_req = 1; return JS_UNDEFINED; }
static JSValue h_set(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  const char *k = JS_ToCString(ctx, argv[0]), *v = JS_ToCString(ctx, argv[1]);
  if (k && v) {
    sset(k, v);
    if (!strcmp(k, "palette")) { pthread_mutex_lock(&emu_mx); nes_set_palette(v); pthread_mutex_unlock(&emu_mx); }
    if (!strcmp(k, "overclock")) { pthread_mutex_lock(&emu_mx); nes_set_overclock(!strcmp(v, "true")); pthread_mutex_unlock(&emu_mx); }
    load_codes();
  }
  JS_FreeCString(ctx, k); JS_FreeCString(ctx, v); return JS_UNDEFINED;
}
static JSValue h_binds(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) {
  char *s = malloc(8192); int o = snprintf(s, 8192, "[");
  for (int b = 0; b < NBTN; b++) {
    o += snprintf(s + o, 8192 - o, "%s{\"name\":\"%s\",\"keys\":[", b ? "," : "", BUTTON_NAMES[b]);
    int k = 0;
    for (int i = 0; i < nbinds; i++) if (binds[i].btn == b) {
      char l[64]; bind_label(binds[i].id, l, sizeof l);
      o += snprintf(s + o, 8192 - o, "%s\"%s\"", k++ ? "," : "", l);
    }
    o += snprintf(s + o, 8192 - o, "]}");
  }
  snprintf(s + o, 8192 - o, "]"); return str_ret(ctx, s);
}
static JSValue h_dev(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) {
  if (!sbool("devUnlock")) return JS_NewString(ctx, "LOCKED");
  const char *w = JS_ToCString(ctx, argv[0]); JSValue v = JS_NewString(ctx, dev_action(w ? w : "")); JS_FreeCString(ctx, w); return v; }
static JSValue h_rebind(JSContext *ctx, JSValueConst t, int argc, JSValueConst *argv) { rebind_target = ARGI(0); return JS_UNDEFINED; }
static JSValue h_cancelrebind(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { rebind_target = -1; return JS_UNDEFINED; }
static JSValue h_resetbinds(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { binds_defaults(); sset("keyBinds", ""); return JS_UNDEFINED; }
static JSValue h_showlink(JSContext *ctx, JSValueConst t, int c, JSValueConst *a) { close_menu(); return JS_UNDEFINED; }

typedef struct { const char *name; JSCFunction *fn; int n; } Fn;
static void add_fns(JSContext *ctx, JSValue obj, const Fn *f) {
  for (; f->name; f++) JS_SetPropertyStr(ctx, obj, f->name, JS_NewCFunction(ctx, f->fn, f->name, f->n));
}
static const Fn GLOBAL_FNS[] = { {"__cnew", js_cnew, 2}, {"__csize", js_csize, 3}, {"__cfill", js_cfill, 7}, {"__cdraw", js_cdraw, 6},
  {"__csave", js_csave, 1}, {"__crestore", js_crestore, 1}, {"__cclip", js_cclip, 5}, {"__cimg", js_cimg, 1}, {"__read", js_read, 1},
  {"__write", js_write, 2}, {"__now", js_now, 0}, {"__log", js_log, 1}, {0} };
static const Fn NATIVE_FNS[] = { {"version", n_version, 0}, {"dual", n_dual, 0}, {"running", n_running, 0}, {"mem", n_mem, 1},
  {"pal", n_pal, 0}, {"chr", n_chr, 0}, {"rom", n_rom, 2}, {"overlay", n_overlay, 0}, {"close", n_close, 0}, {"devUsed", n_devused, 0},
  {"slots", n_slots, 0}, {"saveSlot", n_saveslot, 1}, {"loadSlot", n_loadslot, 1}, {"thumb", h_thumb, 1}, {"menu", n_menu, 0}, {0} };
static const Fn HOST_FNS[] = { {"info", h_info, 0}, {"font", h_font, 0}, {"rom", n_rom, 2}, {"pickRom", h_pickrom, 0}, {"start", h_start, 1},
  {"resume", h_resume, 0}, {"reset", h_reset, 0}, {"save", h_save, 1}, {"load", h_load, 1}, {"thumb", h_thumb, 1}, {"quit", h_quit, 0},
  {"exit", h_exit, 0}, {"set", h_set, 2}, {"binds", h_binds, 0}, {"dev", h_dev, 1}, {"rebind", h_rebind, 1},
  {"cancelRebind", h_cancelrebind, 0}, {"resetBinds", h_resetbinds, 0}, {"showLink", h_showlink, 1}, {0} };

static void js_exc(JSContext *ctx, const char *where) {
  JSValue e = JS_GetException(ctx);
  const char *s = JS_ToCString(ctx, e); JSValue st = JS_GetPropertyStr(ctx, e, "stack"); const char *ss = JS_ToCString(ctx, st);
  logm("[js] %s: %s\n%s", where, s ? s : "?", ss ? ss : "");
  JS_FreeCString(ctx, s); JS_FreeCString(ctx, ss); JS_FreeValue(ctx, st); JS_FreeValue(ctx, e);
}
static int js_run(JSContext *ctx, const char *code, size_t len, const char *name) {
  JSValue v = JS_Eval(ctx, code, len, name, JS_EVAL_TYPE_GLOBAL);
  int ok = !JS_IsException(v); if (!ok) js_exc(ctx, name); JS_FreeValue(ctx, v); return ok;
}
static int js_file(JSContext *ctx, const char *rel) {
  char p[1200]; snprintf(p, sizeof p, "%s/assets/%s", base_dir, rel);
  size_t n; char *t = read_file(p, &n); if (!t) { logm("missing %s", p); return 0; }
  int ok = js_run(ctx, t, n, rel); free(t); return ok;
}
static void js_call(JSContext *ctx, const char *fn, int argc, JSValue *argv) {
  JSValue g = JS_GetGlobalObject(ctx), f = JS_GetPropertyStr(ctx, g, fn);
  if (JS_IsFunction(ctx, f)) { JSValue r = JS_Call(ctx, f, g, argc, argv); if (JS_IsException(r)) js_exc(ctx, fn); JS_FreeValue(ctx, r); }
  JS_FreeValue(ctx, f); JS_FreeValue(ctx, g);
}
static void page_resize(int pg, int w, int h) {
  JSValue a[2] = { JS_NewInt32(pctx[pg], w), JS_NewInt32(pctx[pg], h) }; js_call(pctx[pg], "__resize", 2, a);
}
static void page_init(int pg) {
  JSContext *ctx = pctx[pg] = JS_NewContext(rt);
  JSValue g = JS_GetGlobalObject(ctx);
  add_fns(ctx, g, GLOBAL_FNS);
  JS_SetPropertyStr(ctx, g, "__lsPath", JS_NewString(ctx, ls_path[pg]));
  JS_SetPropertyStr(ctx, g, "innerWidth", JS_NewInt32(ctx, win_w[pg]));
  JS_SetPropertyStr(ctx, g, "innerHeight", JS_NewInt32(ctx, win_h[pg]));
  JSValue o = JS_NewObject(ctx); add_fns(ctx, o, pg ? NATIVE_FNS : HOST_FNS);
  if (pg && gl3d_ok) {
    JS_SetPropertyStr(ctx, o, "settings3d", JS_NewCFunction(ctx, n_settings3d, "settings3d", 0));
    JS_SetPropertyStr(ctx, o, "set3d", JS_NewCFunction(ctx, n_set3d, "set3d", 2));
  }
  JS_SetPropertyStr(ctx, g, pg ? "Native" : "Host", o);
  js_file(ctx, "shim.js");
  JSValue mc = JS_GetPropertyStr(ctx, g, "__mainCanvas"), id = JS_GetPropertyStr(ctx, mc, "_id");
  JS_ToInt32(ctx, &page_canvas[pg], id); JS_FreeValue(ctx, id); JS_FreeValue(ctx, mc);
  JS_FreeValue(ctx, g);
  if (pg) { js_file(ctx, "link/nesgfx.js"); js_file(ctx, "link/metroid.js"); js_file(ctx, "link/mroom.js"); js_file(ctx, "link/link.js"); }
  else { js_file(ctx, "menu/nesgfx.js"); js_file(ctx, "menu/menu.js"); }
}
static void page_publish(int pg) {
  Canvas *c = cv_get(page_canvas[pg]); if (!c || c->gen == pub[pg].gen) return;
  pthread_mutex_lock(&pub_mx);
  if (pub[pg].w != c->w || pub[pg].h != c->h) { free(pub[pg].px); pub[pg].px = malloc((size_t)c->w * c->h * 4); pub[pg].w = c->w; pub[pg].h = c->h; }
  memcpy(pub[pg].px, c->px, (size_t)c->w * c->h * 4); pub[pg].gen = c->gen;
  pthread_mutex_unlock(&pub_mx);
}
static void *js_thread(void *arg) {
  rt = JS_NewRuntime();
  JS_SetMaxStackSize(rt, 1 << 20);
  page_init(0); page_init(1);
  int menu_was = 0;
  while (!quit_req) {
    pthread_mutex_lock(&mq_mx); Msg *m = mq_head; mq_head = mq_tail = NULL; pthread_mutex_unlock(&mq_mx);
    while (m) {
      Msg *nx = m->next; JSContext *ctx = pctx[m->page];
      if (m->type == M_EVAL) js_run(ctx, m->s, strlen(m->s), "eval");
      else if (m->type == M_RESIZE) { win_w[m->page] = (int)m->x; win_h[m->page] = (int)m->y; page_resize(m->page, (int)m->x, (int)m->y); }
      else {
        static const char *T[] = { "down", "move", "up" };
        JSValue a[4] = { JS_NewString(ctx, T[m->type]), JS_NewFloat64(ctx, m->x), JS_NewFloat64(ctx, m->y), JS_NewInt32(ctx, m->id) };
        js_call(ctx, "__pointer", 4, a); JS_FreeValue(ctx, a[0]);
      }
      free(m); m = nx;
    }
    js_call(pctx[1], "__runTimers", 0, NULL);
    if (menu_open) { if (!menu_was) js_run(pctx[0], "window.refresh&&refresh()", 25, "refresh"); js_call(pctx[0], "__runTimers", 0, NULL); }
    menu_was = menu_open;
    page_publish(0); page_publish(1);
    JS_RunGC(rt);
    usleep(8000);
  }
  return NULL;
}

/* ------------------------------------------------------------------ audio (queue + gentle rate control) */
static SDL_AudioDeviceID adev; static int arate = 48000;
static double rs_pos; static int16_t rs_prev[2];
static void audio_push(const int16_t *in, int frames, double ratio) {
  if (!adev || frames <= 0) return;
  Uint32 q = SDL_GetQueuedAudioSize(adev) / 4;
  double target = arate * 0.06, fill = q / target;               /* aim for ~60 ms queued */
  if (q > arate / 4) { SDL_ClearQueuedAudio(adev); q = 0; fill = 0; }
  double adj = 1.0 + 0.005 * (1.0 - fill); if (adj < 0.995) adj = 0.995; if (adj > 1.005) adj = 1.005;
  double step = ratio / adj;                                     /* input frames per output frame */
  static int16_t out[8192 * 2]; int o = 0;
  while (o < 8192) {
    int i0 = (int)floor(rs_pos); if (i0 >= frames) break;
    double f = rs_pos - i0;
    const int16_t *a = i0 < 0 ? rs_prev : in + i0 * 2, *b = in + (i0 + 1 < frames ? i0 + 1 : frames - 1) * 2;
    if (i0 + 1 >= frames) break;
    out[o * 2] = (int16_t)(a[0] + (b[0] - a[0]) * f); out[o * 2 + 1] = (int16_t)(a[1] + (b[1] - a[1]) * f);
    o++; rs_pos += step;
  }
  rs_pos -= frames - 1; rs_prev[0] = in[(frames - 1) * 2]; rs_prev[1] = in[(frames - 1) * 2 + 1];
  if (rs_pos < -1) rs_pos = -1;
  if (q < target * 0.25 && o) SDL_QueueAudio(adev, out, o * 4);     /* refill a little extra after an underrun */
  SDL_QueueAudio(adev, out, o * 4);
}

/* ------------------------------------------------------------------ windows, rendering */
#include "gl3d.c"
static int use_gl;                     /* both screens drawn with GLES2 (one shared context); 3D view available */
static SDL_GLContext glctx;
static GTex tex_page[2], tex_game, tex_text; static unsigned tex_page_gen[2] = {(unsigned)-1, (unsigned)-1}, game_gen, tex_game_gen = (unsigned)-1;
typedef struct { SDL_Window *win; SDL_Renderer *ren; SDL_Texture *tex[2]; int tw[2], th[2]; unsigned gen[2]; int w, h; } Screen;
static Screen scr[2];    /* 0 = top, 1 = bottom */
static SDL_Texture *game_tex; static SDL_Renderer *game_ren; static int game_tw, game_th;
static uint8_t font[0x400]; static int have_font;

static void draw_text(SDL_Renderer *r, const char *s, int x, int y, int sc, SDL_Color c) {
  static const char *map = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
  for (int i = 0; s[i]; i++) {
    char ch = s[i]; if (ch >= 'a' && ch <= 'z') ch -= 32;
    const char *p = strchr(map, ch); if (!p || !ch) continue;
    int t = (int)(p - map);
    for (int yy = 0; yy < 8; yy++) { uint8_t lo = font[t * 16 + yy], hi = font[t * 16 + yy + 8];
      for (int xx = 0; xx < 8; xx++) if (((lo | hi) >> (7 - xx)) & 1) { SDL_Rect rc = { x + (i * 8 + xx) * sc, y + yy * sc, sc, sc }; SDL_RenderFillRect(r, &rc); } }
  }
}
static void upload_page(Screen *s, int pg) {
  pthread_mutex_lock(&pub_mx);
  Pub *p = &pub[pg];
  if (p->px && p->gen != s->gen[pg]) {
    if (!s->tex[pg] || s->tw[pg] != p->w || s->th[pg] != p->h) {
      if (s->tex[pg]) SDL_DestroyTexture(s->tex[pg]);
      s->tex[pg] = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, p->w, p->h);
      s->tw[pg] = p->w; s->th[pg] = p->h;
    }
    SDL_UpdateTexture(s->tex[pg], NULL, p->px, p->w * 4); s->gen[pg] = p->gen;
  }
  pthread_mutex_unlock(&pub_mx);
}
static void game_rect(int W, int H, SDL_Rect *r) {
  const char *asp = sget("aspect"); int integer = sbool("integer");
  double w, h, ar = !strcmp(asp, "pixel") ? (double)game_tw / game_th : 4.0 / 3.0;
  if (!strcmp(asp, "stretch")) { w = W; h = H; }
  else { h = H; w = h * ar; if (w > W) { w = W; h = w / ar; } }
  if (integer) { int k = (int)(h / game_th); if (k < 1) k = 1; h = k * game_th; w = !strcmp(asp, "pixel") ? k * game_tw : h * ar; if (w > W) w = W; }
  r->w = (int)lround(w); r->h = (int)lround(h); r->x = (W - r->w) / 2; r->y = (H - r->h) / 2;
}
static double stat_fps, stat_hz; static int stat_aud;
static char shot_req[2][256]; static void shot(int i, const char *path);
/* Text in the game's font as an RGBA strip (8 px high + pad), uploaded to tex_text. */
static void text_strip(const char *str, uint32_t fg, uint32_t bg, int pad, int *tw, int *th) {
  static const char *map = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  int n = (int)strlen(str), W = n * 8 + pad * 2, H = 8 + pad * 2; static uint32_t buf[512 * 24];
  if (W > 512) { W = 512; n = (512 - pad * 2) / 8; }
  for (int i = 0; i < W * H; i++) buf[i] = bg;
  for (int i = 0; i < n; i++) {
    char ch = str[i]; if (ch >= 'a' && ch <= 'z') ch -= 32;
    const char *p = ch ? strchr(map, ch) : NULL; if (!p) continue;
    int t = (int)(p - map);
    for (int yy = 0; yy < 8; yy++) { uint8_t b = font[t * 16 + yy] | font[t * 16 + yy + 8];
      for (int xx = 0; xx < 8; xx++) if ((b >> (7 - xx)) & 1) buf[(pad + yy) * W + pad + i * 8 + xx] = fg; }
  }
  gtex_rgba(&tex_text, W, H, buf); *tw = W; *th = H;
}
static void gl_page(int pg) {
  pthread_mutex_lock(&pub_mx);
  if (pub[pg].px && pub[pg].gen != tex_page_gen[pg]) { gtex_rgba(&tex_page[pg], pub[pg].w, pub[pg].h, pub[pg].px); tex_page_gen[pg] = pub[pg].gen; }
  pthread_mutex_unlock(&pub_mx);
}
static void gl_shot(int W, int H, const char *path) {
  uint8_t *px = malloc((size_t)W * H * 4); gl.ReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, px);
  SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
  for (int y = 0; y < H; y++) memcpy((uint8_t *)sf->pixels + y * sf->pitch, px + (size_t)(H - 1 - y) * W * 4, W * 4);
  SDL_SaveBMP(sf, path); SDL_FreeSurface(sf); free(px);
}
static int want3d(void) { return gl3d_ok && sbool("dev3d") && started && !menu_open; }
static void render_screen_gl(int i, int game_here) {
  Screen *s = &scr[i]; if (!s->win) return;
  SDL_GL_MakeCurrent(s->win, glctx);
  int W, H; SDL_GL_GetDrawableSize(s->win, &W, &H); if (W < 1 || H < 1) return;
  gl.Viewport(0, 0, W, H); gl.ClearColor(0, 0, 0, 1); gl.Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  if (game_here) {
    if (menu_open) { gl_page(0); if (tex_page[0].id) blit(tex_page[0].id, 0, 0, W, H, W, H, 0); }
    else {
      if (want3d()) {
        R3DOpts op = { sget("cam3d"), sget("back3d"), atoi(sget("depth3d")), sbool("cave3d"), sbool("light3d") };
        if (op.depth < 4) op.depth = 4; if (op.depth > 48) op.depth = 48;
        r3d_draw(&op, W, H);
      } else if (last_frame_ok) {
        if (tex_game_gen != game_gen) { gtex_565(&tex_game, last_w, last_h, last_frame); tex_game_gen = game_gen; }
        game_tw = last_w; game_th = last_h;
        SDL_Rect r; game_rect(W, H, &r); blit(tex_game.id, r.x, r.y, r.w, r.h, W, H, 0);
      }
      int sc = H >= 400 ? 2 : 1, tw, th;
      if (osd_until > now_ms() && have_font) {
        text_strip(osd_text, 0xFF3898FCu, 0xFF000000u, 3, &tw, &th);
        blit(tex_text.id, (W - tw * sc) / 2, H - 30 * sc, tw * sc, th * sc, W, H, 1);
      }
      if (sbool("stats") && have_font) {
        char b[80]; snprintf(b, sizeof b, "%.1f FPS %.0f HZ AUD %d%s", stat_fps, stat_hz, stat_aud, want3d() ? " 3D" : "");
        text_strip(b, 0xFFFCFCFCu, 0xA0000000u, 1, &tw, &th); blit(tex_text.id, 2, 2, tw, th, W, H, 1);
      }
    }
  } else { gl_page(1); if (tex_page[1].id) blit(tex_page[1].id, 0, 0, W, H, W, H, 0); }
  if (shot_req[i][0]) { gl_shot(W, H, shot_req[i]); shot_req[i][0] = 0; }
  SDL_GL_SetSwapInterval(game_here ? 1 : 0);
  SDL_GL_SwapWindow(s->win);
}
static void render_screen(int i, int game_here) {
  if (use_gl) { render_screen_gl(i, game_here); return; }
  Screen *s = &scr[i]; if (!s->ren) return;
  SDL_SetRenderDrawColor(s->ren, 0, 0, 0, 255); SDL_RenderClear(s->ren);
  int link_here = !game_here;
  if (game_here) {
    if (menu_open) { upload_page(s, 0); if (s->tex[0]) SDL_RenderCopy(s->ren, s->tex[0], NULL, NULL); }
    else if (game_tex) {
      SDL_Rect r; game_rect(s->w, s->h, &r); SDL_RenderCopy(s->ren, game_tex, NULL, &r);
      int sc = s->h >= 400 ? 2 : 1;
      if (osd_until > now_ms() && have_font) {
        int tw = (int)strlen(osd_text) * 8 * sc; SDL_Rect bg = { (s->w - tw) / 2 - 6, s->h - 30 * sc, tw + 12, 14 * sc };
        SDL_SetRenderDrawColor(s->ren, 0, 0, 0, 255); SDL_RenderFillRect(s->ren, &bg);
        draw_text(s->ren, osd_text, (s->w - tw) / 2, s->h - 27 * sc, sc, (SDL_Color){252, 152, 56, 255});
      }
      if (sbool("stats") && have_font) {
        char b[64]; snprintf(b, sizeof b, "%.1f FPS %.0f HZ AUD %d", stat_fps, stat_hz, stat_aud);
        draw_text(s->ren, b, 4, 4, 1, (SDL_Color){252, 252, 252, 255});
      }
    }
  }
  if (link_here) { upload_page(s, 1); if (s->tex[1]) SDL_RenderCopy(s->ren, s->tex[1], NULL, NULL); }
  if (shot_req[i][0]) { shot(i, shot_req[i]); shot_req[i][0] = 0; }
  SDL_RenderPresent(s->ren);
}

/* SDL display index whose name contains the sway output name (xdg-output descriptions end in "(DSI-1)"). */
static int display_named(const char *out) {
  for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) { const char *n = SDL_GetDisplayName(d); if (n && strstr(n, out)) return d; }
  return -1;
}
static int top_display, bottom_display = 1;
static int open_screen(int i, const char *title, int vsync) {
  Screen *s = &scr[i];
  int disp = i ? bottom_display : top_display;
  if (disp >= SDL_GetNumVideoDisplays()) disp = 0;
  int fs = getenv("MD_WINDOWED") ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP;
  int w = 640, h = 480;
  if (getenv("MD_SIZE")) sscanf(getenv("MD_SIZE"), "%dx%d", &w, &h);
  s->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED_DISPLAY(disp), SDL_WINDOWPOS_CENTERED_DISPLAY(disp), w, h,
                            SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | fs | (use_gl ? SDL_WINDOW_OPENGL : 0));
  if (!s->win) { logm("window %s: %s", title, SDL_GetError()); return 0; }
  if (use_gl) {
    if (!glctx) {
      glctx = SDL_GL_CreateContext(s->win);
      if (!glctx || !gl_load() || !blit_init()) {
        logm("GL unavailable (%s): falling back to SDL's renderer, no 3D view", SDL_GetError());
        if (glctx) SDL_GL_DeleteContext(glctx); glctx = NULL; use_gl = 0;
        SDL_DestroyWindow(s->win); s->win = NULL; return open_screen(i, title, vsync);
      }
      logm("GL: %s | %s | %s", gl.GetString(GL_VERSION), gl.GetString(GL_RENDERER), gl.GetString(GL_SHADING_LANGUAGE_VERSION));
      gl3d_ok = getenv("MD_NO3D") ? 0 : r3d_init();
      logm("3D view: %s", gl3d_ok ? "available" : "not available");
    } else if (SDL_GL_MakeCurrent(s->win, glctx)) { logm("GL on %s: %s", title, SDL_GetError()); return 0; }
    SDL_GetWindowSize(s->win, &s->w, &s->h);
    return 1;
  }
  s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_ACCELERATED | (vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
  if (!s->ren) s->ren = SDL_CreateRenderer(s->win, -1, vsync ? SDL_RENDERER_PRESENTVSYNC : 0);
  if (!s->ren) s->ren = SDL_CreateRenderer(s->win, -1, SDL_RENDERER_SOFTWARE);
  if (!s->ren) { logm("renderer %s: %s", title, SDL_GetError()); return 0; }
  SDL_RendererInfo ri; SDL_GetRendererInfo(s->ren, &ri); logm("%s: renderer %s%s", title, ri.name, (ri.flags & SDL_RENDERER_PRESENTVSYNC) ? " vsync" : "");
  SDL_GetWindowSize(s->win, &s->w, &s->h);
  return 1;
}

/* Puts each window on its panel under sway (ROCKNIX). Output names can be overridden. */
static void place_windows(void) {
  if (!getenv("SWAYSOCK") && !getenv("MD_FORCE_SWAYMSG")) return;
  const char *top = getenv("MD_TOP_OUTPUT") ? getenv("MD_TOP_OUTPUT") : "DSI-2";
  const char *bot = getenv("MD_BOTTOM_OUTPUT") ? getenv("MD_BOTTOM_OUTPUT") : "DSI-1";
  char cmd[1024];
  snprintf(cmd, sizeof cmd, "swaymsg 'output %s power on' >/dev/null 2>&1; "
                            "swaymsg '[title=\"^Metroid Dual$\"] move container to output %s, focus' >/dev/null 2>&1; "
                            "swaymsg '[title=\"^Metroid Dual Bottom$\"] move container to output %s' >/dev/null 2>&1; "
                            "swaymsg '[title=\"^Metroid Dual\"] fullscreen enable' >/dev/null 2>&1; "
                            "swaymsg '[title=\"^Metroid Dual$\"] focus' >/dev/null 2>&1", bot, top, bot);
  logm("sway: %s", cmd);
  if (system(cmd)) {}
}

/* ------------------------------------------------------------------ input */
static unsigned key_bits, axis_bits, special_bits;   /* special: 1 turbo B, 2 turbo A, 8 fast */
static int select_held, swallow_start;
static SDL_GameController *pads[8];
static void menu_nav(int btn, int is_ok_key, int is_back_key) {
  const char *nav = NULL;
  if (btn == 0) nav = "up"; else if (btn == 1) nav = "down"; else if (btn == 2) nav = "left"; else if (btn == 3) nav = "right";
  else if (btn == 5 || btn == 7 || is_ok_key) nav = "ok";
  else if (btn == 4 || btn == 10 || is_back_key) nav = "back";
  if (nav) { char js[64]; snprintf(js, sizeof js, "window.pad&&pad('%s')", nav); eval_menu(js); }
}
static void toggle3d(void) {
  if (!gl3d_ok) { osd("3D VIEW NOT AVAILABLE"); return; }
  int on = !sbool("dev3d"); sset("dev3d", on ? "true" : "false"); osd(on ? "3D VIEW" : "2D VIEW");
  if (on) { pthread_mutex_lock(&emu_mx); if (started) r3d_capture(); pthread_mutex_unlock(&emu_mx); }
}
static void change_depth(int d) {
  if (!gl3d_ok) { osd("3D VIEW NOT AVAILABLE"); return; }
  int v = atoi(sget("depth3d")) + d; if (v < 4) v = 4; if (v > 48) v = 48;
  char b[16]; snprintf(b, sizeof b, "%d", v); sset("depth3d", b);
  snprintf(b, sizeof b, "DEPTH %d", v); osd(b);
}
static void on_button(const char *id, int down, int ok_key, int back_key) {
  if (rebind_target >= 0 && down) {
    int idx = rebind_target; rebind_target = -1; bind_assign(id, idx);
    char js[64]; snprintf(js, sizeof js, "window.onRebind&&onRebind(%d)", idx); eval_menu(js); return;
  }
  int btn = bind_of(id);
  if (btn == 6) select_held = down;
  if (menu_open) { if (down) menu_nav(btn, ok_key, back_key); return; }
  if (btn == 7 && down && select_held) { swallow_start = 1; key_bits &= ~(1u << 2); open_menu(); return; }   /* SELECT+START */
  if (btn == 7 && !down && swallow_start) { swallow_start = 0; return; }
  if (btn < 0) { if (down && back_key) open_menu(); return; }
  if (btn == 10) { if (down) open_menu(); return; }
  if (btn == 14) { if (down) toggle3d(); return; }
  if (btn == 15 || btn == 16) { if (down) change_depth(btn == 15 ? 4 : -4); return; }
  unsigned bit = btn < 8 ? BUTTON_BITS[btn] : 0, sp = btn >= 8 ? 1u << (btn - 8) : 0;
  if (down) { key_bits |= bit; special_bits |= sp; } else { key_bits &= ~bit; special_bits &= ~sp; }
  if (down && (btn == 12 || btn == 13) && started) {
    pthread_mutex_lock(&emu_mx); int ok = btn == 12 ? save_state("quick") : load_state("quick"); pthread_mutex_unlock(&emu_mx);
    osd(btn == 12 ? (ok ? "QUICK SAVED" : "SAVE FAILED") : (ok ? "QUICK LOADED" : "NO QUICK SAVE"));
  }
}
static int last_nav;
static void on_axes(SDL_GameController *gc) {
  int x = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX), y = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
  unsigned b = 0;
  if (x < -16000) b |= 1u << 6; else if (x > 16000) b |= 1u << 7;
  if (y < -16000) b |= 1u << 4; else if (y > 16000) b |= 1u << 5;
  if (menu_open) { if (b != (unsigned)last_nav && b) menu_nav((b & 16) ? 0 : (b & 32) ? 1 : (b & 64) ? 2 : 3, 0, 0); last_nav = b; axis_bits = 0; }
  else axis_bits = b;
}
static int trig_state[2];
static int screen_of_window(Uint32 wid) { for (int i = 0; i < 2; i++) if (scr[i].win && SDL_GetWindowID(scr[i].win) == wid) return i; return -1; }
static int page_on_screen(int s) { int swap = sbool("swap"); int game_screen = swap ? 1 : 0; return s == game_screen ? (menu_open ? 0 : -1) : 1; }
static void pointer(int s, int type, double x, double y, int id) {
  if (s < 0) return; int pg = page_on_screen(s); if (pg < 0) return;
  post(pg, type, x, y, id, NULL);
}

/* ------------------------------------------------------------------ test script (MD_SCRIPT) */
/* Lines: "wait N" (loop iterations), "poke <hex addr> <hex value>", "key <scancode> <0|1>", "pad <button name> <0|1>", "tap <screen> x y",
 *        "drag <screen> x0 y0 x1 y1", "shot <screen> <file.bmp>", "quit" */
static FILE *script; static int script_wait;
static void shot(int i, const char *path) {
  Screen *s = &scr[i]; if (!s->ren) return;
  int w, h; SDL_GetRendererOutputSize(s->ren, &w, &h);
  SDL_Surface *sf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
  if (SDL_RenderReadPixels(s->ren, NULL, SDL_PIXELFORMAT_ARGB8888, sf->pixels, sf->pitch) == 0) SDL_SaveBMP(sf, path);
  else logm("shot: %s", SDL_GetError());
  SDL_FreeSurface(sf);
}
static void script_step(void) {
  if (!script) return;
  if (script_wait > 0) { script_wait--; return; }
  char ln[256];
  while (fgets(ln, sizeof ln, script)) {
    char a[64] = "", b[256] = ""; int x = 0, y = 0, z = 0, w = 0, sc = 0;
    if (sscanf(ln, "wait %d", &x) == 1) { script_wait = x; return; }
    if (sscanf(ln, "key %d %d", &x, &y) == 2) { char id[32]; snprintf(id, sizeof id, "k:%d", x); on_button(id, y, x == SDL_SCANCODE_RETURN, x == SDL_SCANCODE_BACKSPACE); continue; }
    if (sscanf(ln, "pad %63s %d", a, &y) == 2) { char id[80]; snprintf(id, sizeof id, "b:%s", a); on_button(id, y, 0, 0); continue; }
    if (sscanf(ln, "tap %d %d %d", &sc, &x, &y) == 3) { pointer(sc, M_DOWN, x, y, 1); pointer(sc, M_UP, x, y, 1); continue; }
    if (sscanf(ln, "drag %d %d %d %d %d", &sc, &x, &y, &z, &w) == 5) { pointer(sc, M_DOWN, x, y, 1);
      for (int k = 1; k <= 8; k++) pointer(sc, M_MOVE, x + (z - x) * k / 8, y + (w - y) * k / 8, 1); pointer(sc, M_UP, z, w, 1); continue; }
    if (sscanf(ln, "shot %d %255s", &sc, b) == 2) { snprintf(shot_req[sc & 1], 256, "%s", b); return; }
    if (sscanf(ln, "poke %x %x", &x, &y) == 2) { pthread_mutex_lock(&emu_mx); nes_poke(x, y); pthread_mutex_unlock(&emu_mx); continue; }
    if (!strncmp(ln, "quit", 4)) { quit_req = 1; return; }
  }
  fclose(script); script = NULL;
}

/* ------------------------------------------------------------------ main */
static void on_signal(int sig) { quit_req = 1; }     /* the system's quit hotkey sends SIGTERM: still save "auto" */
int main(int argc, char **argv) {
  signal(SIGTERM, on_signal); signal(SIGINT, on_signal); signal(SIGHUP, on_signal);
  if (getenv("MD_HOME")) snprintf(base_dir, sizeof base_dir, "%s", getenv("MD_HOME"));
  else { char exe[1024]; ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1); exe[n > 0 ? n : 0] = 0; snprintf(base_dir, sizeof base_dir, "%s", n > 0 ? dirname(exe) : "."); }
  snprintf(state_dir, sizeof state_dir, "%s/states", base_dir); mkdir(state_dir, 0755);
  snprintf(cfg_path, sizeof cfg_path, "%s/settings.txt", base_dir);
  snprintf(ls_path[0], sizeof ls_path[0], "%s/menu-storage.json", base_dir);
  snprintf(ls_path[1], sizeof ls_path[1], "%s/link-storage.json", base_dir);
  char lp[1100]; snprintf(lp, sizeof lp, "%s/log.txt", base_dir); logf_ = fopen(lp, "w");
  logm("Metroid Dual %s (Linux) in %s", VERSION, base_dir);
  settings_load(); binds_load(); load_codes();
  find_rom();
  if (rom && rom_len >= ROM_FONT + 0x400) { memcpy(font, rom + ROM_FONT, 0x400); have_font = 1; }

  SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
  SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
  SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
  SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");   /* controller focus lock: touching the bottom screen never takes the pad away */
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) < 0) { logm("SDL_Init: %s", SDL_GetError()); return 1; }
  logm("SDL video driver: %s, displays: %d", SDL_GetCurrentVideoDriver(), SDL_GetNumVideoDisplays());
  int single = getenv("MD_SINGLE") != NULL;
  for (int d = 0; d < SDL_GetNumVideoDisplays(); d++) { SDL_Rect b; SDL_GetDisplayBounds(d, &b); logm("display %d: %s %dx%d at %d,%d", d, SDL_GetDisplayName(d), b.w, b.h, b.x, b.y); }
  {
    int t = display_named(getenv("MD_TOP_OUTPUT") ? getenv("MD_TOP_OUTPUT") : "DSI-2");
    int b = display_named(getenv("MD_BOTTOM_OUTPUT") ? getenv("MD_BOTTOM_OUTPUT") : "DSI-1");
    if (t < 0 && b < 0) { t = 0; b = SDL_GetNumVideoDisplays() > 1 ? 1 : 0; }
    else if (t < 0) t = b == 0 && SDL_GetNumVideoDisplays() > 1 ? 1 : 0;
    else if (b < 0) b = t == 0 && SDL_GetNumVideoDisplays() > 1 ? 1 : 0;
    top_display = t; bottom_display = b;
    logm("top display %d, bottom display %d", t, b);
  }
  use_gl = !getenv("MD_NOGL");
  if (use_gl) {
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2); SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 16); SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  }
  if (!open_screen(0, "Metroid Dual", 1)) return 1;
  if (!single && !open_screen(1, "Metroid Dual Bottom", 0)) logm("no bottom window; Link screen disabled");
  for (int i = 0; i < 2; i++) if (scr[i].win) SDL_GetWindowSize(scr[i].win, &scr[i].w, &scr[i].h);
  win_w[0] = scr[0].w; win_h[0] = scr[0].h; win_w[1] = scr[1].win ? scr[1].w : 640; win_h[1] = scr[1].win ? scr[1].h : 480;

  nes_init(base_dir, state_dir);
  SDL_AudioSpec want = {0}, have; want.freq = 48000; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 512;
  adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
  if (adev) { arate = have.freq; SDL_PauseAudioDevice(adev, 0); logm("audio %d Hz", arate); } else logm("no audio: %s", SDL_GetError());

  pthread_t jt; pthread_create(&jt, NULL, js_thread, NULL);
  if (getenv("MD_SCRIPT")) script = fopen(getenv("MD_SCRIPT"), "r");
  if (sbool("skipLauncher") && rom) { char ap[1200]; state_path(ap, sizeof ap, "auto", "st"); start_game(access(ap, R_OK) == 0); }

  static int16_t samples[8192];
  double hz = 60, last_present = now_ms(), acc = 0, stat_t = now_ms(), next_timer = now_ms(), present_ms_sum = 0;
  int stat_frames = 0, hz_n = 0, vsync_ok = -1, prev_menu = menu_open; double hz_t0 = 0;
  unsigned long long frame = 0, loops = 0; int ls_force = 1, last_gs = 0;
  while (!quit_req) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
      case SDL_QUIT: quit_req = 1; break;
      case SDL_CONTROLLERDEVICEADDED: {
        SDL_GameController *gc = SDL_GameControllerOpen(e.cdevice.which);
        if (gc) { for (int i = 0; i < 8; i++) if (!pads[i]) { pads[i] = gc; break; } logm("controller: %s", SDL_GameControllerName(gc)); }
        break; }
      case SDL_CONTROLLERBUTTONDOWN: case SDL_CONTROLLERBUTTONUP: {
        char id[32]; snprintf(id, sizeof id, "b:%s", SDL_GameControllerGetStringForButton((SDL_GameControllerButton)e.cbutton.button));
        on_button(id, e.type == SDL_CONTROLLERBUTTONDOWN, 0, 0); break; }
      case SDL_CONTROLLERAXISMOTION: {
        SDL_GameController *gc = SDL_GameControllerFromInstanceID(e.caxis.which);
        if (e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT || e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) {
          int t = e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT, on = e.caxis.value > 16000;
          if (on != trig_state[t]) { trig_state[t] = on; on_button(t ? "t:righttrigger" : "t:lefttrigger", on, 0, 0); }
        } else if (gc) on_axes(gc);
        break; }
      case SDL_KEYDOWN: case SDL_KEYUP: {
        if (e.key.repeat) break;
        char id[32]; snprintf(id, sizeof id, "k:%d", e.key.keysym.scancode);
        SDL_Scancode sc = e.key.keysym.scancode;
        on_button(id, e.type == SDL_KEYDOWN, sc == SDL_SCANCODE_RETURN || sc == SDL_SCANCODE_SPACE,
                  sc == SDL_SCANCODE_ESCAPE || sc == SDL_SCANCODE_BACKSPACE || sc == SDL_SCANCODE_AC_BACK);
        break; }
      case SDL_FINGERDOWN: case SDL_FINGERMOTION: case SDL_FINGERUP: {
        int s = screen_of_window(e.tfinger.windowID);
        if (s < 0) { /* no window id (older drivers): pick by touch device order */ s = SDL_GetNumTouchDevices() > 1 && SDL_GetTouchDevice(1) == e.tfinger.touchId ? 1 : 0; if (!scr[1].win) s = 0; }
        int type = e.type == SDL_FINGERDOWN ? M_DOWN : e.type == SDL_FINGERMOTION ? M_MOVE : M_UP;
        pointer(s, type, e.tfinger.x * scr[s].w, e.tfinger.y * scr[s].h, (int)(e.tfinger.fingerId & 0x7FFFFFFF));
        break; }
      case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP:
        if (e.button.which == SDL_TOUCH_MOUSEID || e.button.button != SDL_BUTTON_LEFT) break;
        pointer(screen_of_window(e.button.windowID), e.type == SDL_MOUSEBUTTONDOWN ? M_DOWN : M_UP, e.button.x, e.button.y, 1); break;
      case SDL_MOUSEMOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID || !(e.motion.state & SDL_BUTTON_LMASK)) break;
        pointer(screen_of_window(e.motion.windowID), M_MOVE, e.motion.x, e.motion.y, 1); break;
      case SDL_WINDOWEVENT:
        if (e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || e.window.event == SDL_WINDOWEVENT_RESIZED) {
          int s = screen_of_window(e.window.windowID); if (s < 0) break;
          SDL_GetWindowSize(scr[s].win, &scr[s].w, &scr[s].h);
          int swap = sbool("swap"), pg = (s == (swap ? 1 : 0)) ? 0 : 1;
          post(pg, M_RESIZE, scr[s].w, scr[s].h, 0, NULL); ls_force = 1;
        }
        if (e.window.event == SDL_WINDOWEVENT_EXPOSED) ls_force = 1;
        break;
      }
    }
    if (prev_menu != menu_open) {        /* menu closed or opened: audio + pacing restart */
      prev_menu = menu_open; acc = 0; key_bits = 0; special_bits = 0;
      if (adev) SDL_ClearQueuedAudio(adev);
    }

    /* pacing: one emulated frame per refresh when the display runs at the NES rate (or a multiple), otherwise by
     * elapsed time. vsync is detected from how long the game window's present blocks during the first 60 frames. */
    double t = now_ms(), dt = t - last_present; last_present = t;
    double fps = started ? nes_fps() : 60.0988; if (fps <= 0) fps = 60.0988;
    double eff = fps;
    if (vsync_ok < 0 && dt > 0 && dt < 200) {
      if (hz_n == 0) hz_t0 = t;
      if (++hz_n == 61) {
        hz = 60000.0 / (t - hz_t0); vsync_ok = present_ms_sum / 60 > 3.0 && hz > 20 && hz < 400;
        logm("display %.2f Hz, present %.1f ms -> %s pacing", hz, present_ms_sum / 60, vsync_ok ? "vsync" : "timer");
        if (!vsync_ok) next_timer = t;
      }
    }
    int m = vsync_ok == 1 ? (int)lround(hz / fps) : 0;
    if (m >= 1 && fabs(hz / m - fps) / fps < 0.02) { acc += dt * hz / 1000.0 / m; eff = hz / m; }
    else acc += dt * fps / 1000.0;
    if (vsync_ok == 0) { double w = next_timer - now_ms(); if (w > 0) SDL_Delay((Uint32)w); next_timer = fmax(next_timer + 1000.0 / fps, now_ms() - 50); }
    int runs = (int)(acc + 0.5); if (runs > 0) acc -= runs; if (runs > 2) { runs = 2; acc = 0; }
    if (acc < -1) acc = 0;

    if (started && !menu_open && runs > 0) {
      int fast = (special_bits & 8) != 0; if (fast) runs *= 3;
      pthread_mutex_lock(&emu_mx);
      for (int i = 0; i < runs && started; i++) {
        unsigned turbo = ((frame >> 1) & 1) ? (((special_bits & 1) ? 1u : 0) | ((special_bits & 2) ? 1u << 8 : 0)) : 0;
        unsigned bits = key_bits | axis_bits | turbo;
        if ((bits & 0x30) == 0x30) bits &= ~0x20u; if ((bits & 0xC0) == 0xC0) bits &= ~0x80u;
        nes_set_pad(bits); nes_run();
        if (code_energy || code_missiles) apply_codes();
        frame++;
        int n = nes_audio(samples, 8192);
        if (!fast && sbool("audio")) audio_push(samples, n / 2, nes_rate() / arate * eff / fps);
      }
      int w, h, fresh; const uint16_t *fb = nes_frame(&w, &h, &fresh);
      if (fresh) { memcpy(last_frame, fb, (size_t)w * h * 2); last_w = w; last_h = h; last_frame_ok = 1; }
      if (want3d()) r3d_capture();
      pthread_mutex_unlock(&emu_mx);
      if (fresh) game_gen++;
      if (fresh && !use_gl) {
        int gs = sbool("swap") ? 1 : 0; Screen *s = &scr[gs]; if (!s->ren) s = &scr[0];
        if (!game_tex || game_tw != w || game_th != h || game_ren != s->ren) { if (game_tex) SDL_DestroyTexture(game_tex);
          game_tex = SDL_CreateTexture(s->ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, w, h); game_tw = w; game_th = h; game_ren = s->ren; }
        SDL_UpdateTexture(game_tex, NULL, last_frame, w * 2);
      }
      stat_frames += runs;
    }
    if (t - stat_t >= 1000) { stat_fps = stat_frames * 1000.0 / (t - stat_t); stat_hz = hz; stat_frames = 0; stat_t = t;
      stat_aud = adev ? (int)(SDL_GetQueuedAudioSize(adev) / 4 * 100 / (arate * 0.06)) : 0; }

    int gs = sbool("swap") && scr[1].win ? 1 : 0;
    if (scr[1].win) {                    /* Link screen: only when the page drew something new */
      Screen *ls = &scr[1 - gs];
      if (pub[1].gen != ls->gen[1] || ls_force || shot_req[1 - gs][0]) { render_screen(1 - gs, 0); ls_force = 0; }
    }
    script_step();
    if (++loops == 10 || loops == 180) place_windows();     /* once the windows are mapped, and again a few seconds later */
    double p0 = now_ms(); render_screen(gs, 1);
    if (vsync_ok < 0 && hz_n > 0) present_ms_sum += now_ms() - p0;
    if (gs != last_gs) { last_gs = gs; ls_force = 1; for (int i = 0; i < 2; i++) { scr[i].gen[0] = scr[i].gen[1] = (unsigned)-1;
        post(i == gs ? 0 : 1, M_RESIZE, scr[i].w, scr[i].h, 0, NULL); } }
  }
  logm("exiting");
  if (started) { pthread_mutex_lock(&emu_mx); save_state("auto"); pthread_mutex_unlock(&emu_mx); }
  pthread_join(jt, NULL);
  SDL_Quit();
  return 0;
}
