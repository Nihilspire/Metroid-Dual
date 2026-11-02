# Builds libretro-fceumm (static) + glue for a target.
#   make TARGET=android   -> out/android/libmetroiddual.so
#   make TARGET=host      -> out/host/drive (desktop test driver)
CORE_DIR := $(FCEUMM)/src
include $(FCEUMM)/Makefile.common
TARGET ?= android
ROOT := $(abspath ..)
OBJDIR := $(ROOT)/out/$(TARGET)/obj
ifeq ($(TARGET),android)
CC := clang --target=aarch64-linux-android23 --sysroot=$(ROOT)/out/sysroot
CFLAGS := -O2 -fPIC -ffunction-sections -fdata-sections -fvisibility=hidden -DANDROID
else
CC := clang
CFLAGS := -O1 -g -fPIC
endif
CFLAGS += $(INCFLAGS) $(COREDEFINES) -DHAVE_STDINT_H -DRIGHTSHIFT_IS_SAR -DLSB_FIRST -Wno-everything -std=gnu99
OBJS := $(patsubst $(FCEUMM)/%.c,$(OBJDIR)/%.o,$(SOURCES_C))
$(OBJDIR)/%.o: $(FCEUMM)/%.c
	@mkdir -p $(dir $@)
	@$(CC) $(CFLAGS) -c $< -o $@
$(ROOT)/out/$(TARGET)/libfceumm.a: $(OBJS)
	@llvm-ar rcs $@ $^
android: $(ROOT)/out/android/libfceumm.a nesjni.c
	$(CC) $(CFLAGS) -shared -nostdlib -fuse-ld=lld -Wl,--gc-sections -Wl,-soname,libmetroiddual.so \
	  -Wl,--version-script=jni.map -Wl,-z,max-page-size=16384 \
	  nesjni.c nescore.c $(ROOT)/out/android/libfceumm.a -L$(ROOT)/out/sysroot/usr/lib -lbuiltins -lm -llog -ldl -lc \
	  -o $(ROOT)/out/android/libmetroiddual.so
host: $(ROOT)/out/host/libfceumm.a drive.c nescore.c
	$(CC) $(CFLAGS) drive.c nescore.c $(ROOT)/out/host/libfceumm.a -lm -o $(ROOT)/out/host/drive
