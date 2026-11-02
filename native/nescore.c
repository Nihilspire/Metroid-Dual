/* nescore.c - minimal libretro frontend around the statically linked FCEUmm core.
 * Shared by the Android JNI layer (nesjni.c) and the desktop test driver (drive.c). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include "libretro.h"
#include "nescore.h"

extern uint8_t RAM[0x800];
extern uint8_t *Page[32], *VPage[8];
extern uint8_t PALRAM[0x20];
extern uint8_t *vnapage[4];
static int var_update;

static char sysdir[512] = ".", savedir[512] = ".";
static uint16_t fb[256 * 240];
static int fb_w = 256, fb_h = 240, fb_new;
static int16_t abuf[8192 * 2];
static volatile int alen;
static uint16_t pad;               /* RETRO_DEVICE_ID_JOYPAD_* bits */
static int loaded;
static void *rombuf;

static void core_log(enum retro_log_level l, const char *fmt, ...) {
  char b[512]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap);
  nes_log(b);
}
static const char *oc_name = "2x-Postrender";
static bool env_cb(unsigned cmd, void *data) {
  switch (cmd) {
  case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: ((struct retro_log_callback *)data)->log = core_log; return true;
  case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: *(const char **)data = sysdir; return true;
  case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: *(const char **)data = savedir; return true;
  case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: return *(enum retro_pixel_format *)data == RETRO_PIXEL_FORMAT_RGB565;
  case RETRO_ENVIRONMENT_GET_VARIABLE: {
    struct retro_variable *v = data; v->value = NULL;
    if (!strcmp(v->key, "fceumm_sndquality")) v->value = "High";
        else if (!strcmp(v->key, "fceumm_overscan_v_top")) v->value = "8";
    else if (!strcmp(v->key, "fceumm_overscan_v_bottom")) v->value = "8";
    else if (!strcmp(v->key, "fceumm_overscan_h_left")) v->value = "0";
    else if (!strcmp(v->key, "fceumm_overscan_h_right")) v->value = "0";
    else if (!strcmp(v->key, "fceumm_palette")) v->value = nes_palette_name();
    else if (!strcmp(v->key, "fceumm_overclocking")) v->value = oc_name;
    return v->value != NULL;
  }
  case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE: *(bool *)data = var_update; var_update = 0; return true;
  case RETRO_ENVIRONMENT_GET_CAN_DUPE: *(bool *)data = true; return true;
  case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION: *(unsigned *)data = 0; return true;
  case RETRO_ENVIRONMENT_GET_AUDIO_VIDEO_ENABLE: *(int *)data = 3; return true;
  default: return false;
  }
}
static void video_cb(const void *d, unsigned w, unsigned h, size_t pitch) {
  if (!d) return;
  fb_w = w; fb_h = h;
  for (unsigned y = 0; y < h; y++) memcpy(fb + y * w, (const uint8_t *)d + y * pitch, w * 2);
  fb_new = 1;
}
static size_t audio_batch_cb(const int16_t *d, size_t frames) {
  int n = (int)frames * 2;
  if (alen + n > (int)(sizeof abuf / 2)) n = (int)(sizeof abuf / 2) - alen;
  if (n > 0) { memcpy(abuf + alen, d, n * 2); alen += n; }
  return frames;
}
static void audio_cb(int16_t l, int16_t r) { int16_t s[2] = {l, r}; audio_batch_cb(s, 1); }
static void poll_cb(void) {}
static int16_t input_cb(unsigned port, unsigned dev, unsigned idx, unsigned id) {
  if (port || dev != RETRO_DEVICE_JOYPAD) return 0;
  if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return pad;
  return (pad >> id) & 1;
}

static const char *pal_name = "default";
const char *nes_palette_name(void) { return pal_name; }
static char pal_buf[64];
/* Overclock: extra CPU-only scanlines after each frame is drawn ("2x-Postrender"), so the game finishes its work
 * within the frame and the original slowdown with many enemies on screen goes away. Video and sound timing unchanged. */
void nes_set_overclock(int on) { oc_name = on ? "2x-Postrender" : "disabled"; var_update = 1; }
void nes_set_palette(const char *p) { snprintf(pal_buf, sizeof pal_buf, "%s", p); pal_name = pal_buf; var_update = 1; }

int nes_init(const char *sys, const char *save) {
  snprintf(sysdir, sizeof sysdir, "%s", sys); snprintf(savedir, sizeof savedir, "%s", save);
  retro_set_environment(env_cb);
  retro_set_video_refresh(video_cb);
  retro_set_audio_sample(audio_cb);
  retro_set_audio_sample_batch(audio_batch_cb);
  retro_set_input_poll(poll_cb);
  retro_set_input_state(input_cb);
  retro_init();
  return 0;
}
int nes_load(const char *path) {
  FILE *f = fopen(path, "rb"); if (!f) return -1;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  free(rombuf); rombuf = malloc(n);
  if (fread(rombuf, 1, n, f) != (size_t)n) { fclose(f); return -2; }
  fclose(f);
  struct retro_game_info gi = { path, rombuf, (size_t)n, NULL };
  if (loaded) retro_unload_game();
  loaded = retro_load_game(&gi);
  if (loaded) retro_set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
  return loaded ? 0 : -3;
}
void nes_unload(void) { if (loaded) retro_unload_game(); loaded = 0; }
void nes_reset(void) { if (loaded) retro_reset(); }
void nes_set_pad(unsigned bits) { pad = bits; }
void nes_run(void) { if (loaded) retro_run(); }
const uint16_t *nes_frame(int *w, int *h, int *fresh) { *w = fb_w; *h = fb_h; *fresh = fb_new; fb_new = 0; return fb; }
int nes_audio(int16_t *out, int max) { int n = alen < max ? alen : max; memcpy(out, abuf, n * 2);
  memmove(abuf, abuf + n, (alen - n) * 2); alen -= n; return n; }
double nes_fps(void) { struct retro_system_av_info av; retro_get_system_av_info(&av); return av.timing.fps; }
double nes_rate(void) { struct retro_system_av_info av; retro_get_system_av_info(&av); return av.timing.sample_rate; }

/* CPU-visible memory without side effects: 2KB RAM mirrors and mapped pages ($6000-$FFFF). */
int nes_peek(unsigned a) {
  a &= 0xFFFF;
  if (a < 0x2000) return RAM[a & 0x7FF];
  if (a >= 0x6000 && Page[a >> 11]) return Page[a >> 11][a];
  return 0;
}
void nes_poke(unsigned a, unsigned v) {
  a &= 0xFFFF;
  if (a < 0x2000) RAM[a & 0x7FF] = v;
  else if (a >= 0x6000 && a < 0x8000 && Page[a >> 11]) Page[a >> 11][a] = v;
}
int nes_ppu_peek(unsigned a) {
  a &= 0x3FFF;
  if (a < 0x2000) return VPage[a >> 10] ? VPage[a >> 10][a] : 0;
  if (a >= 0x3F00) return PALRAM[a & 0x1F];
  if (vnapage[(a >> 10) & 3]) return vnapage[(a >> 10) & 3][a & 0x3FF];
  return 0;
}
size_t nes_state_size(void) { return loaded ? retro_serialize_size() : 0; }
int nes_state_save(void *buf, size_t n) { return loaded && retro_serialize(buf, n); }
int nes_state_load(const void *buf, size_t n) { return loaded && retro_unserialize(buf, n); }
