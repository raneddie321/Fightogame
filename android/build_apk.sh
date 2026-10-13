#!/usr/bin/env bash
# FIGHTO - builds the Android APK without a game engine, without Gradle and (by default)
# without the NDK. Native code: clang --target=<abi>-linux-android21 against the minimal
# sysroot in android/sysroot plus generated link stubs for libc/libm/libdl.
# Java shell: javac + dx. Packaging: aapt, zipalign, apksigner.
#
# Usage: android/build_apk.sh [--abi arm64-v8a|x86_64|all]
# Environment (all optional):
#   ANDROID_SDK_ROOT   SDK with build-tools and a platform android.jar (default /usr/lib/android-sdk)
#   JAVA_HOME          JDK (for javac and jni.h)
#   FIGHTO_KEYSTORE, FIGHTO_KEYSTORE_PASS, FIGHTO_KEY_ALIAS   release signing key
#                      (default: a debug key generated locally in android/build, never committed)
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT=$(pwd)
OUT=$ROOT/android/build
ABIS="arm64-v8a x86_64"
if [ "${1:-}" = "--abi" ]; then
  case "${2:-}" in
    all) ;;
    arm64-v8a|x86_64) ABIS="$2" ;;
    *) echo "unknown ABI ${2:-}"; exit 1 ;;
  esac
fi

SDK=${ANDROID_SDK_ROOT:-${ANDROID_HOME:-/usr/lib/android-sdk}}
BT=""
for d in $(ls -d "$SDK"/build-tools/*/ 2>/dev/null | sort -V); do
  if [ -x "$d/aapt" ] && [ -e "$d/dx" -o -e "$d/d8" ]; then BT=${d%/}; fi
done
[ -n "$BT" ] || { echo "No usable Android build-tools found in $SDK"; exit 1; }
PLATFORM_JAR=$(ls "$SDK"/platforms/android-*/android.jar 2>/dev/null | sort -V | tail -1)
[ -f "$PLATFORM_JAR" ] || { echo "No platform android.jar found in $SDK"; exit 1; }
JAVAC=$(command -v javac)
JAVA_HOME=${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$JAVAC")")")}
[ -f "$JAVA_HOME/include/jni.h" ] || { echo "jni.h not found under $JAVA_HOME/include"; exit 1; }
for t in clang clang++ llvm-nm llvm-readelf llvm-strip; do command -v $t >/dev/null || { echo "missing $t"; exit 1; }; done

echo "== FIGHTO APK build"
echo "   build-tools: $BT"
echo "   platform:    $PLATFORM_JAR"
echo "   JDK:         $JAVA_HOME"
echo "   ABIs:        $ABIS"
rm -rf "$OUT/obj" "$OUT/stage" "$OUT/classes" "$OUT/stubs"
mkdir -p "$OUT/stage/lib" "$OUT/classes"

SOURCES="$(ls src/core/*.cpp src/gfx/*.cpp src/game/*.cpp src/ui/*.cpp src/audio/*.cpp) src/platform/platform_android.cpp src/platform/platform_keys.cpp"

# Standard bionic functions the game is allowed to import (anything else fails the build)
LIBC_OK="abort access calloc clock_gettime close exit free fsync getenv malloc memchr memcmp memcpy memmove memset open read realloc rename remove strcat strchr strcmp strcpy strlen strncmp strncpy strrchr strstr unlink write atoi atof strtol strtoul strtod strtof qsort abs labs sprintf snprintf vsnprintf"
LIBM_OK="sinf cosf tanf asinf acosf atanf atan2f sqrtf powf expf exp2f logf log2f log10f fabsf floorf ceilf roundf truncf fmodf fminf fmaxf hypotf copysignf sinhf coshf tanhf sincosf sin cos tan asin acos atan atan2 sqrt pow exp exp2 log log2 log10 fabs floor ceil round trunc fmod fmin fmax hypot ldexp frexp ldexpf frexpf copysign sinh cosh tanh sincos"
LIBDL_OK="dlopen dlsym dlclose dlerror"

for ABI in $ABIS; do
  case $ABI in
    arm64-v8a) TRIPLE=aarch64-linux-android21; ARCHFLAGS="-mno-outline-atomics" ;;
    x86_64) TRIPLE=x86_64-linux-android21; ARCHFLAGS="" ;;
  esac
  OBJ=$OUT/obj/$ABI
  mkdir -p "$OBJ" "$OUT/stubs/$ABI" "$OUT/stage/lib/$ABI"
  echo "== Compiling native code for $ABI ($TRIPLE)"
  CXXFLAGS="--target=$TRIPLE -std=c++17 -O2 -fPIC -fno-exceptions -fno-rtti -fno-stack-protector $ARCHFLAGS \
    -fvisibility=hidden -ffunction-sections -fdata-sections -nostdinc++ -nostdlibinc \
    -isystem $ROOT/android/sysroot/include -isystem $JAVA_HOME/include -isystem $JAVA_HOME/include/linux \
    -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function"
  PIDS=""
  for f in $SOURCES; do
    o=$OBJ/$(basename "$f" .cpp).o
    clang++ $CXXFLAGS -c "$f" -o "$o" &
    PIDS="$PIDS $!"
  done
  FAIL=0
  for p in $PIDS; do wait $p || FAIL=1; done
  [ $FAIL = 0 ] || { echo "native compile failed"; exit 1; }

  # External symbols = undefined minus defined across all objects
  llvm-nm --defined-only --format=just-symbols "$OBJ"/*.o 2>/dev/null | sort -u > "$OBJ/defined.txt"
  llvm-nm --undefined-only --format=just-symbols "$OBJ"/*.o 2>/dev/null | sort -u > "$OBJ/undefined.txt"
  comm -23 "$OBJ/undefined.txt" "$OBJ/defined.txt" > "$OBJ/external.txt"
  : > "$OUT/stubs/$ABI/libc.c"; : > "$OUT/stubs/$ABI/libm.c"; : > "$OUT/stubs/$ABI/libdl.c"
  BAD=""
  while read -r sym; do
    [ -z "$sym" ] && continue
    if [[ " $LIBC_OK " == *" $sym "* ]]; then echo "void $sym(void) {}" >> "$OUT/stubs/$ABI/libc.c"
    elif [[ " $LIBM_OK " == *" $sym "* ]]; then echo "void $sym(void) {}" >> "$OUT/stubs/$ABI/libm.c"
    elif [[ " $LIBDL_OK " == *" $sym "* ]]; then echo "void $sym(void) {}" >> "$OUT/stubs/$ABI/libdl.c"
    else BAD="$BAD $sym"; fi
  done < "$OBJ/external.txt"
  [ -z "$BAD" ] || { echo "Unexpected external symbols (not standard bionic):$BAD"; exit 1; }
  echo "   imports: $(tr '\n' ' ' < "$OBJ/external.txt")"
  for lib in c m dl; do
    clang --target=$TRIPLE -shared -nostdlib -fno-builtin -w -fPIC -fuse-ld=lld -Wl,-soname,lib$lib.so \
      -o "$OUT/stubs/$ABI/lib$lib.so" "$OUT/stubs/$ABI/lib$lib.c"
  done

  echo "== Linking libfighto.so ($ABI)"
  SO=$OUT/stage/lib/$ABI/libfighto.so
  clang++ --target=$TRIPLE -shared -nostdlib -fuse-ld=lld -o "$SO" "$OBJ"/*.o \
    -Wl,-soname,libfighto.so -Wl,--gc-sections -Wl,-z,defs -Wl,-z,noexecstack -Wl,-z,relro -Wl,-z,now \
    -Wl,-z,max-page-size=16384 -Wl,--hash-style=both -Wl,--build-id=sha1 \
    -L"$OUT/stubs/$ABI" -lc -lm -ldl
  llvm-strip --strip-unneeded "$SO"
  echo "   NEEDED: $(llvm-readelf -d "$SO" | grep NEEDED | sed 's/.*\[\(.*\)\]/\1/' | tr '\n' ' ')"
  JNI=$(llvm-nm -D --defined-only --format=just-symbols "$SO" | grep -c '^Java_com_fighto_game_FightoLib_' || true)
  echo "   JNI exports: $JNI"
  [ "$JNI" -ge 10 ] || { echo "JNI entry points missing"; exit 1; }
done

echo "== Compiling Java shell"
"$JAVAC" --release 8 -Xlint:-options -nowarn -classpath "$PLATFORM_JAR" -d "$OUT/classes" android/java/com/fighto/game/*.java
if [ -e "$BT/dx" ]; then
  "$BT/dx" --dex --min-sdk-version=21 --output="$OUT/stage/classes.dex" "$OUT/classes"
else
  "$BT/d8" --min-api 21 --lib "$PLATFORM_JAR" --output "$OUT/stage" $(find "$OUT/classes" -name '*.class')
fi

echo "== Packaging"
UNSIGNED=$OUT/FIGHTO-unsigned.apk
ALIGNED=$OUT/FIGHTO-aligned.apk
rm -f "$UNSIGNED" "$ALIGNED"
"$BT/aapt" package -f -M android/AndroidManifest.xml -S android/res -A assets -I "$PLATFORM_JAR" -F "$UNSIGNED"
(cd "$OUT/stage" && "$BT/aapt" add -f "$UNSIGNED" classes.dex $(find lib -name '*.so' | sort) > /dev/null)
"$BT/zipalign" -p -f 4 "$UNSIGNED" "$ALIGNED"

KS=${FIGHTO_KEYSTORE:-$OUT/fighto-debug.keystore}
KPASS=${FIGHTO_KEYSTORE_PASS:-android}
ALIAS=${FIGHTO_KEY_ALIAS:-fighto}
if [ ! -f "$KS" ]; then
  echo "== Generating a local debug signing key (not committed): $KS"
  keytool -genkeypair -keystore "$KS" -storepass "$KPASS" -keypass "$KPASS" -alias "$ALIAS" \
    -keyalg RSA -keysize 2048 -validity 10000 -dname "CN=FIGHTO Debug, O=FIGHTO, C=US" > /dev/null 2>&1
fi
mkdir -p dist
APK=$ROOT/dist/FIGHTO.apk
"$BT/apksigner" sign --ks "$KS" --ks-pass pass:"$KPASS" --key-pass pass:"$KPASS" --ks-key-alias "$ALIAS" \
  --min-sdk-version 21 --out "$APK" "$ALIGNED"

echo "== Verifying"
"$BT/apksigner" verify --verbose "$APK" | head -6
"$BT/zipalign" -c -p 4 "$APK" && echo "   zipalign: OK"
"$BT/aapt" dump badging "$APK" | grep -E "^package|^sdkVersion|^targetSdkVersion|^native-code|^application-label:|^launchable-activity|uses-gl-es" | sed 's/^/   /'
echo "== Built $APK ($(du -h "$APK" | cut -f1))"
