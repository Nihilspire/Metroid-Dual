# Metroid Dual for Linux (ROCKNIX / Anbernic RG DS).
#   make host                 -> out/linux-host/metroiddual (x86_64, system SDL2, for testing)
#   make arm64                -> out/linux-arm64/metroiddual (aarch64 glibc 2.36, links the device's libSDL2)
FCEUMM ?= /home/claude/src/fceumm
QJS ?= /home/claude/src/quickjs
SDL2SRC ?= /home/claude/src/SDL2
ROOT := $(abspath ..)
CORE_DIR := $(FCEUMM)/src
include $(FCEUMM)/Makefile.common

ARCH ?= host
OUT := $(ROOT)/out/linux-$(ARCH)
ifeq ($(ARCH),arm64)
CC := python3 -m ziglang cc -target aarch64-linux-gnu.2.36
OPT := -O2 -mcpu=cortex_a55
LDSTRIP := -s
SDL_CFLAGS := -I$(SDL2SRC)/include -D_REENTRANT
SDL_LIBS := -L$(OUT) -lSDL2-2.0
else
CC := clang
OPT := -O2 -g
SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS := $(shell sdl2-config --libs)
endif

CORE_CFLAGS := $(OPT) $(INCFLAGS) $(COREDEFINES) -DHAVE_STDINT_H -DRIGHTSHIFT_IS_SAR -DLSB_FIRST -Wno-everything -std=gnu99 -fPIC
QJS_SRC := $(QJS)/quickjs.c $(QJS)/libregexp.c $(QJS)/libunicode.c $(QJS)/dtoa.c
QJS_CFLAGS := $(OPT) -D_GNU_SOURCE -Wno-everything -std=gnu11
CORE_OBJS := $(patsubst $(FCEUMM)/%.c,$(OUT)/core/%.o,$(SOURCES_C))
QJS_OBJS := $(patsubst $(QJS)/%.c,$(OUT)/qjs/%.o,$(QJS_SRC))

$(OUT)/core/%.o: $(FCEUMM)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CORE_CFLAGS) -c $< -o $@
$(OUT)/qjs/%.o: $(QJS)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(QJS_CFLAGS) -c $< -o $@

$(OUT)/metroiddual: metroiddual.c gl3d.c ../native/nescore.c ../native/nescore.h $(CORE_OBJS) $(QJS_OBJS) $(if $(filter arm64,$(ARCH)),$(OUT)/libSDL2-2.0.so)
	@$(CC) $(OPT) -std=gnu11 -Wall -Wno-unused-parameter -Wno-misleading-indentation -I../native -I$(QJS) -I$(FCEUMM)/src/drivers/libretro -I$(FCEUMM)/src/drivers/libretro/libretro-common/include $(SDL_CFLAGS) \
	  metroiddual.c ../native/nescore.c $(CORE_OBJS) $(QJS_OBJS) $(SDL_LIBS) $(LDSTRIP) -lpthread -lm -o $@

# Link stub for the device's libSDL2-2.0.so.0 (only symbol names matter; the real library is on the device).
$(OUT)/libSDL2-2.0.so: sdlstub.c
	@mkdir -p $(OUT)
	$(CC) -shared -Wl,-soname,libSDL2-2.0.so.0 sdlstub.c -o $@

# Metroid Link: the Link screen alone, for the game running in RetroArch (no emulator core).
$(OUT)/metroidlink: metroidlink.c $(QJS_OBJS) $(if $(filter arm64,$(ARCH)),$(OUT)/libSDL2-2.0.so)
	@$(CC) $(OPT) -std=gnu11 -Wall -Wno-unused-parameter -Wno-misleading-indentation -I$(QJS) $(SDL_CFLAGS) \
	  metroidlink.c $(QJS_OBJS) $(SDL_LIBS) $(LDSTRIP) -lpthread -lm -o $@

host:
	$(MAKE) ARCH=host $(ROOT)/out/linux-host/metroiddual
arm64:
	$(MAKE) ARCH=arm64 $(ROOT)/out/linux-arm64/metroiddual
link-host:
	$(MAKE) ARCH=host $(ROOT)/out/linux-host/metroidlink
link-arm64:
	$(MAKE) ARCH=arm64 $(ROOT)/out/linux-arm64/metroidlink
.PHONY: host arm64 link-host link-arm64
