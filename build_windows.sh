#!/usr/bin/env bash
# Builds the Windows release (FIGHTO.exe) with MinGW-w64 and packages it in dist/FIGHTO_Windows.
# Requires: cmake, ninja, x86_64-w64-mingw32-g++-posix, x86_64-w64-mingw32-windres, zip (optional).
set -euo pipefail
cd "$(dirname "$0")"
BUILD=build-win
DIST=dist/FIGHTO_Windows
cmake -S . -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake
cmake --build "$BUILD"
rm -rf "$DIST"
mkdir -p "$DIST"
cp "$BUILD/FIGHTO.exe" "$DIST/"
x86_64-w64-mingw32-strip "$DIST/FIGHTO.exe"
cp -r assets "$DIST/assets"
cp windows/README_WINDOWS.txt "$DIST/README.txt"
mkdir -p "$DIST/licenses"
cp third_party/stb/LICENSE "$DIST/licenses/stb_LICENSE.txt"
cp assets/fonts/LICENSE-Inter.txt "$DIST/licenses/Inter_OFL.txt" 2>/dev/null || true
if command -v zip >/dev/null 2>&1; then
  (cd dist && rm -f FIGHTO_Windows.zip && zip -qr FIGHTO_Windows.zip FIGHTO_Windows)
  echo "Packaged dist/FIGHTO_Windows.zip"
fi
echo "Windows build ready in $DIST"
