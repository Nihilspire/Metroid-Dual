#!/bin/bash
# Builds "Metroid Link X.Y.apk": the Link screen on its own, for Metroid running in RetroArch (network commands).
# Shares the Link page (app/assets/link) with Metroid Dual. Needs the same toolchain as build_apk.sh, no native code.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
ANDROID_JAR=${ANDROID_JAR:-$HOME/src/platforms/android-33/android.jar}
VER=$(grep -o 'VERSION = "[^"]*"' "$ROOT/linkapp/src/com/jack/metroidlink/MainActivity.java" | cut -d'"' -f2)
B=$ROOT/out/linkapk; rm -rf "$B"; mkdir -p "$B/gen" "$B/classes" "$B/assets"
sed -i "s/android:versionName=\"[^\"]*\"/android:versionName=\"$VER\"/" "$ROOT/linkapp/AndroidManifest.xml"
cp -r "$ROOT/app/assets/link" "$B/assets/"
aapt package -f -m -J "$B/gen" -M "$ROOT/linkapp/AndroidManifest.xml" -S "$ROOT/linkapp/res" -A "$B/assets" -I "$ANDROID_JAR" -F "$B/res.apk"
if ! javac -nowarn --release 8 -encoding UTF-8 -cp "$ANDROID_JAR" -d "$B/classes" $(find "$B/gen" "$ROOT/linkapp/src" -name '*.java') > "$B/javac.log" 2>&1; then
  grep -v "^Note:" "$B/javac.log"; echo "javac failed"; exit 1
fi
dalvik-exchange --dex --min-sdk-version=23 --output="$B/classes.dex" "$B/classes"
cp "$B/res.apk" "$B/unsigned.apk"
(cd "$B" && aapt add unsigned.apk classes.dex >/dev/null)
zipalign -f -p 4 "$B/unsigned.apk" "$B/aligned.apk"
KS=$ROOT/tools/metroiddual.keystore
OUT="$ROOT/out/Metroid Link $VER.apk"
apksigner sign --ks "$KS" --ks-pass pass:metroiddual --key-pass pass:metroiddual --ks-key-alias metroiddual --out "$OUT" "$B/aligned.apk"
apksigner verify "$OUT" && ls -la "$OUT"
