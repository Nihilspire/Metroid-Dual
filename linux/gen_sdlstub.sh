#!/bin/sh
# Regenerates sdlstub.c from the SDL_ functions metroiddual.c and metroidlink.c call (host build needed for the object scan).
set -e
cd "$(dirname "$0")"
clang -c -O0 -I../native -I"${QJS:-/home/claude/src/quickjs}" $(sdl2-config --cflags) metroiddual.c -o /tmp/md_stubscan.o
clang -c -O0 -I"${QJS:-/home/claude/src/quickjs}" $(sdl2-config --cflags) metroidlink.c -o /tmp/ml_stubscan.o
{ echo "/* Link-time stub for libSDL2-2.0.so.0: symbol names only. The device's real SDL2 is used at run time. */"
  nm -u /tmp/md_stubscan.o /tmp/ml_stubscan.o | awk '{print $2}' | grep '^SDL_' | sort -u | sed 's/.*/void &(void) {}/'; } > sdlstub.c
