#!/bin/bash
# Builds an aarch64 Android (API 23) sysroot from bionic headers + stub libs, plus compiler-rt builtins.
# Needs: clang lld llvm git; sources fetched into $SRC.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
SRC=${SRC:-$HOME/src}
SYS=$ROOT/out/sysroot
API=23
mkdir -p "$SRC"
[ -d "$SRC/bionic" ] || { git clone -q --depth 1 --filter=blob:none --sparse https://github.com/aosp-mirror/platform_bionic "$SRC/bionic";
  (cd "$SRC/bionic" && git sparse-checkout set --skip-checks libc/include libc/kernel/uapi libc/kernel/android/uapi libc/libc.map.txt libm/libm.map.txt libdl/libdl.map.txt); }
[ -d "$SRC/llvm" ] || { git clone -q --depth 1 --filter=blob:none --sparse https://github.com/llvm/llvm-project "$SRC/llvm";
  (cd "$SRC/llvm" && git sparse-checkout set compiler-rt/lib/builtins); }
[ -d "$SRC/fceumm" ] || git clone -q https://github.com/libretro/libretro-fceumm "$SRC/fceumm"
rm -rf "$SYS"; mkdir -p "$SYS/usr/include" "$SYS/usr/lib"
cp -r "$SRC/bionic/libc/include/." "$SYS/usr/include/"
for d in linux asm-generic drm misc mtd rdma scsi sound video xen; do
  [ -d "$SRC/bionic/libc/kernel/uapi/$d" ] && cp -r "$SRC/bionic/libc/kernel/uapi/$d" "$SYS/usr/include/"; done
cp -r "$SRC/bionic/libc/kernel/uapi/asm-arm64/asm" "$SYS/usr/include/asm"
cp -r "$SRC/bionic/libc/kernel/android/uapi/." "$SYS/usr/include/" 2>/dev/null || true
# Minimal headers for NDK libs we use
cat > "$SYS/usr/include/jni.h.note" <<<"jni.h comes from tools/include"
cp -r "$ROOT/tools/include/." "$SYS/usr/include/"
CC="clang --target=aarch64-linux-android$API --sysroot=$SYS"
# Stub shared libraries from bionic map files (+ hand lists for liblog/libandroid/libGLESv2/libEGL/libOpenSLES)
python3 "$ROOT/tools/mkstubs.py" "$SYS/usr/lib" "$SRC/bionic" "$ROOT/tools/stubs" $API
for f in "$SYS"/usr/lib/*.stub.c; do n=$(basename "$f" .stub.c)
  $CC -shared -nostdlib -fuse-ld=lld -Wl,-soname,$n.so -o "$SYS/usr/lib/$n.so" "$f"; rm "$f"; done
# compiler-rt builtins
B=$SRC/llvm/compiler-rt/lib/builtins; O=$ROOT/out/builtins; rm -rf "$O"; mkdir -p "$O"
for f in "$B"/*.c "$B"/aarch64/*.c; do
  $CC -O2 -fPIC -I"$B" -c "$f" -o "$O/$(basename "$f" .c).o" 2>/dev/null || true; done
llvm-ar rcs "$SYS/usr/lib/libbuiltins.a" "$O"/*.o
echo "sysroot ready: $SYS ($(ls "$O" | wc -l) builtins)"
