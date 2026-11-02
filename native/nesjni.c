/* nesjni.c - JNI bridge for Metroid Dual (com.jack.metroiddual.Native). */
#include <jni.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>
#include "nescore.h"

#define J(name) Java_com_jack_metroiddual_Native_##name
#define EXPORT __attribute__((visibility("default")))

void nes_log(const char *m) { __android_log_write(ANDROID_LOG_INFO, "MetroidDual", m); }

static int16_t abuf[8192];

EXPORT jint J(init)(JNIEnv *e, jclass c, jstring sys, jstring save) {
  const char *a = (*e)->GetStringUTFChars(e, sys, 0), *b = (*e)->GetStringUTFChars(e, save, 0);
  int r = nes_init(a, b);
  (*e)->ReleaseStringUTFChars(e, sys, a); (*e)->ReleaseStringUTFChars(e, save, b);
  return r;
}
EXPORT jint J(load)(JNIEnv *e, jclass c, jstring path) {
  const char *p = (*e)->GetStringUTFChars(e, path, 0); int r = nes_load(p);
  (*e)->ReleaseStringUTFChars(e, path, p); return r;
}
EXPORT void J(unload)(JNIEnv *e, jclass c) { nes_unload(); }
EXPORT void J(reset)(JNIEnv *e, jclass c) { nes_reset(); }
EXPORT jdouble J(fps)(JNIEnv *e, jclass c) { return nes_fps(); }
EXPORT jdouble J(rate)(JNIEnv *e, jclass c) { return nes_rate(); }
EXPORT void J(setOverclock)(JNIEnv *e, jclass c, jboolean on) { nes_set_overclock(on); }
EXPORT void J(setPalette)(JNIEnv *e, jclass c, jstring s) {
  const char *p = (*e)->GetStringUTFChars(e, s, 0); nes_set_palette(p); (*e)->ReleaseStringUTFChars(e, s, p);
}
/* Runs one frame with the given pad bits. Copies the picture into dst (direct buffer, RGB565).
 * Returns (w << 16 | h) when a new picture was produced, else 0. */
EXPORT jint J(runFrame)(JNIEnv *e, jclass c, jint pad, jobject dst) {
  nes_set_pad((unsigned)pad);
  nes_run();
  int w, h, fresh; const uint16_t *fb = nes_frame(&w, &h, &fresh);
  if (!fresh || !dst) return 0;
  void *d = (*e)->GetDirectBufferAddress(e, dst); jlong cap = (*e)->GetDirectBufferCapacity(e, dst);
  if (!d || cap < (jlong)w * h * 2) return 0;
  memcpy(d, fb, (size_t)w * h * 2);
  return (w << 16) | h;
}
EXPORT jint J(audio)(JNIEnv *e, jclass c, jshortArray out) {
  int max = (*e)->GetArrayLength(e, out); if (max > 8192) max = 8192;
  int n = nes_audio(abuf, max);
  if (n > 0) (*e)->SetShortArrayRegion(e, out, 0, n, abuf);
  return n;
}
EXPORT jint J(peek)(JNIEnv *e, jclass c, jint a) { return nes_peek((unsigned)a); }
EXPORT void J(poke)(JNIEnv *e, jclass c, jint a, jint v) { nes_poke((unsigned)a, (unsigned)v); }
/* Reads CPU memory ranges "addr:len,addr:len" (hex addr, decimal len) and returns them as one hex string. */
EXPORT jstring J(mem)(JNIEnv *e, jclass c, jstring spec) {
  const char *s = (*e)->GetStringUTFChars(e, spec, 0);
  static char out[16384 * 2 + 8]; int o = 0; const char *p = s;
  static const char hx[] = "0123456789abcdef";
  while (*p) {
    char *end; unsigned a = strtoul(p, &end, 16); unsigned n = 1;
    if (*end == ':') n = strtoul(end + 1, &end, 10);
    for (unsigned i = 0; i < n && o < 16384 * 2; i++) { int v = nes_peek(a + i); out[o++] = hx[v >> 4]; out[o++] = hx[v & 15]; }
    p = *end ? end + 1 : end;
  }
  out[o] = 0;
  (*e)->ReleaseStringUTFChars(e, spec, s);
  return (*e)->NewStringUTF(e, out);
}
/* PPU space $0000-$3FFF (pattern tables, nametables, palette) into a 16 KB array. */
EXPORT void J(ppu)(JNIEnv *e, jclass c, jbyteArray out) {
  static jbyte b[0x4000];
  for (int i = 0; i < 0x4000; i++) b[i] = (jbyte)nes_ppu_peek(i);
  (*e)->SetByteArrayRegion(e, out, 0, 0x4000, b);
}
EXPORT jint J(stateSave)(JNIEnv *e, jclass c, jstring path) {
  size_t n = nes_state_size(); if (!n) return -1;
  void *buf = malloc(n); int ok = nes_state_save(buf, n);
  const char *p = (*e)->GetStringUTFChars(e, path, 0);
  FILE *f = ok ? fopen(p, "wb") : NULL; int r = -2;
  if (f) { r = fwrite(buf, 1, n, f) == n ? 0 : -3; fclose(f); }
  (*e)->ReleaseStringUTFChars(e, path, p); free(buf); return r;
}
EXPORT jint J(stateLoad)(JNIEnv *e, jclass c, jstring path) {
  const char *p = (*e)->GetStringUTFChars(e, path, 0);
  FILE *f = fopen(p, "rb"); (*e)->ReleaseStringUTFChars(e, path, p);
  if (!f) return -1;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  void *buf = malloc(n); int r = -2;
  if (fread(buf, 1, n, f) == (size_t)n) r = nes_state_load(buf, n) ? 0 : -3;
  fclose(f); free(buf); return r;
}

/* One frame's worth of what the 3D view needs: RAM 0x800 | nametables $2000-$2FFF 0x1000 | palette 0x20 | CHR 0x2000. */
EXPORT void J(snapshot)(JNIEnv *e, jclass c, jbyteArray out) {
  static jbyte b[0x3820];
  for (int i = 0; i < 0x800; i++) b[i] = (jbyte)nes_peek(i);
  for (int i = 0; i < 0x1000; i++) b[0x800 + i] = (jbyte)nes_ppu_peek(0x2000 + i);
  for (int i = 0; i < 0x20; i++) b[0x1800 + i] = (jbyte)nes_ppu_peek(0x3F00 + i);
  for (int i = 0; i < 0x2000; i++) b[0x1820 + i] = (jbyte)nes_ppu_peek(i);
  (*e)->SetByteArrayRegion(e, out, 0, 0x3820, b);
}
