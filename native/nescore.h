#pragma once
#include <stdint.h>
#include <stddef.h>
void nes_log(const char *msg);                 /* provided by the frontend */
int nes_init(const char *sysdir, const char *savedir);
int nes_load(const char *path);
void nes_unload(void);
void nes_reset(void);
void nes_set_pad(unsigned bits);
void nes_run(void);
const uint16_t *nes_frame(int *w, int *h, int *fresh);
int nes_audio(int16_t *out, int max);
double nes_fps(void);
double nes_rate(void);
int nes_peek(unsigned a);
void nes_poke(unsigned a, unsigned v);
int nes_ppu_peek(unsigned a);
size_t nes_state_size(void);
int nes_state_save(void *buf, size_t n);
int nes_state_load(const void *buf, size_t n);
const char *nes_palette_name(void);
void nes_set_palette(const char *p);
void nes_set_overclock(int on);
