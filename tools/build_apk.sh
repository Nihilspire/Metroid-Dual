#!/bin/bash
# Builds "Metroid Dual X.Y.apk". Run tools/setup_toolchain.sh once first (sysroot + compiler-rt).
# Needs: aapt zipalign apksigner dalvik-exchange openjdk-17, android.jar (API 33) at $ANDROID_JAR.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FCEUMM=${FCEUMM:-$HOME/src/fceumm}
ANDROID_JAR=${ANDROID_JAR:-$HOME/src/platforms/android-33/android.jar}
VER=$(grep -o 'VERSION = "[^"]*"' "$ROOT/app/src/com/jack/metroiddual/MainActivity.java" | cut -d'"' -f2)
B=$ROOT/out/apk; rm -rf "$B"; mkdir -p "$B/gen" "$B/classes" "$B/lib/arm64-v8a"
sed -i "s/android:versionName=\"[^\"]*\"/android:versionName=\"$VER\"/" "$ROOT/app/AndroidManifest.xml"
# native
if [ ! -f "$ROOT/out/android/libmetroiddual.so" ] || [ -n "$REBUILD_NATIVE" ] || [ "$ROOT/native/nesjni.c" -nt "$ROOT/out/android/libmetroiddual.so" ] || [ "$ROOT/native/nescore.c" -nt "$ROOT/out/android/libmetroiddual.so" ]; then
  make -C "$ROOT/native" TARGET=android FCEUMM="$FCEUMM" -j4 android
fi
cp "$ROOT/out/android/libmetroiddual.so" "$B/lib/arm64-v8a/"
# resources + java
aapt package -f -m -J "$B/gen" -M "$ROOT/app/AndroidManifest.xml" -S "$ROOT/app/res" -A "$ROOT/app/assets" -I "$ANDROID_JAR" -F "$B/res.apk"
if ! javac -nowarn --release 8 -encoding UTF-8 -cp "$ANDROID_JAR" -d "$B/classes" $(find "$B/gen" "$ROOT/app/src" -name '*.java') > "$B/javac.log" 2>&1; then
  grep -v "^Note:" "$B/javac.log"; echo "javac failed"; exit 1
fi
dalvik-exchange --dex --min-sdk-version=23 --output="$B/classes.dex" "$B/classes"
cp "$B/res.apk" "$B/unsigned.apk"
(cd "$B" && aapt add unsigned.apk classes.dex >/dev/null && aapt add -0 so unsigned.apk lib/arm64-v8a/libmetroiddual.so >/dev/null)
zipalign -f -p 16 "$B/unsigned.apk" "$B/aligned.apk"
KS=$ROOT/tools/metroiddual.keystore
[ -f "$KS" ] || keytool -genkeypair -keystore "$KS" -storepass metroiddual -keypass metroiddual -alias metroiddual \
  -keyalg RSA -keysize 2048 -validity 20000 -dname "CN=Metroid Dual, O=Jack" >/dev/null 2>&1
OUT="$ROOT/out/Metroid Dual $VER.apk"
apksigner sign --ks "$KS" --ks-pass pass:metroiddual --key-pass pass:metroiddual --ks-key-alias metroiddual --out "$OUT" "$B/aligned.apk"
apksigner verify "$OUT" && ls -la "$OUT"
