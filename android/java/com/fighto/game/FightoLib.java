package com.fighto.game;

import android.content.res.AssetManager;

/** JNI bridge to libfighto.so (the engine-free C++ game). */
final class FightoLib {
    private FightoLib() {}

    static void load() {
        System.loadLibrary("fighto");
    }

    static native boolean nativeInit(AssetManager assets, String filesDir, int width, int height);
    static native void nativeResize(int width, int height);
    /** Runs one game frame on the GL thread. Returns false when the game wants to quit. */
    static native boolean nativeFrame();
    /** action: 0 down, 1 move, 2 up, 3 cancel. Coordinates in pixels. */
    static native void nativeTouch(int action, int pointerId, float x, float y);
    static native void nativeKey(int keyCode, boolean down);
    static native void nativeAxes(float lx, float ly, float rx, float ry, float lt, float rt);
    static native void nativePause();
    static native void nativeResume();
    /** Fills interleaved 16-bit stereo PCM at 44100 Hz (called from the audio thread). */
    static native void nativeAudio(short[] pcm, int frames);
    static native boolean nativeWantsQuit();
}
