# Minimal Android (bionic, LP64) sysroot headers

FIGHTO's native library is compiled with plain `clang --target=aarch64-linux-android21`
because the Android NDK could not be downloaded in the build environment. These headers
declare only the standard C / POSIX functions the game uses, with bionic's 64-bit ABI
types and constants. They are original, minimal declarations (not copied from bionic).
At runtime the real system libraries (`libc.so`, `libm.so`, `libdl.so`) provide the
implementations; `build_apk.sh` links against generated stub libraries only to record
the `DT_NEEDED` entries.

