/* drive.c - desktop test driver. Usage: drive rom script
 * script lines:  run N [buttons]   (buttons: U D L R A B S T(start) E(select))
 *                shot out.ppm | dump out.bin (CPU $0000-$7FFF + PPU $0000-$3FFF) | save f | load f */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nescore.h"
#include "libretro.h"
void nes_log(const char *m) { fputs(m, stderr); }
static unsigned parse(const char *s) {
  unsigned b = 0;
  for (; s && *s; s++) switch (*s) {
    case 'U': b |= 1 << RETRO_DEVICE_ID_JOYPAD_UP; break;   case 'D': b |= 1 << RETRO_DEVICE_ID_JOYPAD_DOWN; break;
    case 'L': b |= 1 << RETRO_DEVICE_ID_JOYPAD_LEFT; break; case 'R': b |= 1 << RETRO_DEVICE_ID_JOYPAD_RIGHT; break;
    case 'A': b |= 1 << RETRO_DEVICE_ID_JOYPAD_A; break;    case 'B': b |= 1 << RETRO_DEVICE_ID_JOYPAD_B; break;
    case 'T': b |= 1 << RETRO_DEVICE_ID_JOYPAD_START; break; case 'E': b |= 1 << RETRO_DEVICE_ID_JOYPAD_SELECT; break; }
  return b;
}
int main(int argc, char **argv) {
  nes_init(".", ".");
  if (nes_load(argv[1])) { fprintf(stderr, "load failed\n"); return 1; }
  fprintf(stderr, "fps %.4f rate %.0f state %zu\n", nes_fps(), nes_rate(), nes_state_size());
  FILE *rec = NULL; unsigned char lastchr[0x2000]; int havechr = 0;
  FILE *sc = fopen(argv[2], "r"); char line[256]; long frames = 0; int16_t tmp[8192];
  while (fgets(line, sizeof line, sc)) {
    char cmd[32] = "", a[200] = "", b[64] = ""; sscanf(line, "%31s %199s %63s", cmd, a, b);
    if (!strcmp(cmd, "recopen")) { rec = fopen(a, "wb"); havechr = 0; }
    else if (!strcmp(cmd, "recclose")) { fclose(rec); rec = NULL; }
    else if (!strcmp(cmd, "rec")) {   /* rec N buttons: run N frames, append each to the recording */
      int n = atoi(a); nes_set_pad(parse(b));
      for (int i = 0; i < n; i++) {
        nes_run(); nes_audio(tmp, 8192); frames++;
        unsigned char chr[0x2000]; for (int k = 0; k < 0x2000; k++) chr[k] = nes_ppu_peek(k);
        int chg = !havechr || memcmp(chr, lastchr, 0x2000);
        fputc(chg ? 1 : 0, rec);
        for (unsigned x = 0; x < 0x800; x++) fputc(nes_peek(x), rec);
        for (unsigned x = 0x2000; x < 0x3000; x++) fputc(nes_ppu_peek(x), rec);
        for (unsigned x = 0x3F00; x < 0x3F20; x++) fputc(nes_ppu_peek(x), rec);
        if (chg) { fwrite(chr, 1, 0x2000, rec); memcpy(lastchr, chr, 0x2000); havechr = 1; }
      }
    }
    else if (!strcmp(cmd, "run")) { int n = atoi(a); nes_set_pad(parse(b)); for (int i = 0; i < n; i++) { nes_run(); nes_audio(tmp, 8192); frames++; } }
    else if (!strcmp(cmd, "tap")) { for (int k = 0; k < 2; k++) { nes_set_pad(k ? 0 : parse(a)); for (int i = 0; i < 6; i++) { nes_run(); nes_audio(tmp, 8192); frames++; } } }
    else if (!strcmp(cmd, "shot")) { int w, h, f; const uint16_t *p = nes_frame(&w, &h, &f); FILE *o = fopen(a, "wb");
      fprintf(o, "P6 %d %d 255\n", w, h);
      for (int i = 0; i < w * h; i++) { uint16_t c = p[i]; fputc((c >> 11) << 3, o); fputc(((c >> 5) & 63) << 2, o); fputc((c & 31) << 3, o); }
      fclose(o); }
    else if (!strcmp(cmd, "dump")) { FILE *o = fopen(a, "wb"); for (unsigned x = 0; x < 0x8000; x++) fputc(nes_peek(x), o);
      for (unsigned x = 0; x < 0x4000; x++) fputc(nes_ppu_peek(x), o); fclose(o); }
    else if (!strcmp(cmd, "save")) { size_t n = nes_state_size(); void *buf = malloc(n); nes_state_save(buf, n); FILE *o = fopen(a, "wb"); fwrite(buf, 1, n, o); fclose(o); free(buf); }
    else if (!strcmp(cmd, "load")) { FILE *i = fopen(a, "rb"); fseek(i, 0, SEEK_END); size_t n = ftell(i); rewind(i); void *buf = malloc(n); fread(buf, 1, n, i); fclose(i); printf("load %d\n", nes_state_load(buf, n)); free(buf); }
    else if (!strcmp(cmd, "poke")) { nes_poke(strtoul(a, 0, 16), strtoul(b, 0, 16)); }
    else if (!strcmp(cmd, "peek")) { unsigned ad = strtoul(a, 0, 16); int n = b[0] ? atoi(b) : 1; printf("%04X:", ad); for (int i = 0; i < n; i++) printf(" %02X", nes_peek(ad + i)); printf("\n"); }
  }
  fprintf(stderr, "frames %ld\n", frames);
  return 0;
}
