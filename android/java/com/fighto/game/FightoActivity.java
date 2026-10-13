package com.fighto.game;

import android.app.Activity;
import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.opengl.GLSurfaceView;
import android.os.Build;
import android.os.Bundle;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Toast;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/**
 * FIGHTO Android host. Owns the OpenGL ES 3 surface, forwards touch / gamepad /
 * keyboard input to the native game on the GL thread and streams audio.
 */
public class FightoActivity extends Activity {
    private GLSurfaceView view;
    private AudioThread audio;
    private volatile boolean nativeReady;
    private volatile boolean contextLost;
    private boolean surfaceCreatedOnce;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        FightoLib.load();
        view = new GLSurfaceView(this);
        view.setEGLContextClientVersion(3);
        view.setEGLConfigChooser(8, 8, 8, 0, 24, 8);
        view.setPreserveEGLContextOnPause(true);
        view.setRenderer(new GameRenderer());
        view.setFocusable(true);
        view.setFocusableInTouchMode(true);
        setContentView(view);
        view.requestFocus();
        hideSystemUi();
        audio = new AudioThread();
        audio.start();
    }

    private void hideSystemUi() {
        if (Build.VERSION.SDK_INT >= 19) {
            view.setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                    | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                    | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN);
        }
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        if (hasFocus) hideSystemUi();
    }

    @Override
    protected void onPause() {
        super.onPause();
        audio.paused = true;
        view.queueEvent(new Runnable() {
            public void run() { FightoLib.nativePause(); }
        });
        view.onPause();
    }

    @Override
    protected void onResume() {
        super.onResume();
        view.onResume();
        view.queueEvent(new Runnable() {
            public void run() { FightoLib.nativeResume(); }
        });
        audio.paused = false;
        hideSystemUi();
    }

    @Override
    protected void onDestroy() {
        audio.running = false;
        super.onDestroy();
        // Native GPU state cannot outlive its EGL context: start fresh next launch
        if (contextLost || isFinishing()) android.os.Process.killProcess(android.os.Process.myPid());
    }

    private void showMessageAndClose(final String msg) {
        runOnUiThread(new Runnable() {
            public void run() {
                Toast.makeText(FightoActivity.this, msg, Toast.LENGTH_LONG).show();
                finish();
            }
        });
    }

    private final class GameRenderer implements GLSurfaceView.Renderer {
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            if (surfaceCreatedOnce && nativeReady) {
                // The driver discarded the EGL context despite setPreserveEGLContextOnPause.
                contextLost = true;
                nativeReady = false;
                showMessageAndClose("FIGHTO lost its graphics context. Your last save is safe - please restart the game.");
            }
            surfaceCreatedOnce = true;
        }

        public void onSurfaceChanged(GL10 gl, int width, int height) {
            if (contextLost) return;
            if (!nativeReady) {
                nativeReady = FightoLib.nativeInit(getAssets(), getFilesDir().getAbsolutePath(), width, height);
                if (!nativeReady) showMessageAndClose("FIGHTO could not start: this device needs OpenGL ES 3.0.");
            } else {
                FightoLib.nativeResize(width, height);
            }
        }

        public void onDrawFrame(GL10 gl) {
            if (!nativeReady) return;
            if (!FightoLib.nativeFrame()) {
                nativeReady = false;
                runOnUiThread(new Runnable() {
                    public void run() { finish(); }
                });
            }
        }
    }

    // ------------------------------------------------------------------ input
    private static final class TouchEvent implements Runnable {
        final int action, id;
        final float x, y;
        TouchEvent(int action, int id, float x, float y) { this.action = action; this.id = id; this.x = x; this.y = y; }
        public void run() { FightoLib.nativeTouch(action, id, x, y); }
    }

    private static final class KeyEv implements Runnable {
        final int code;
        final boolean down;
        KeyEv(int code, boolean down) { this.code = code; this.down = down; }
        public void run() { FightoLib.nativeKey(code, down); }
    }

    private static final class AxesEv implements Runnable {
        final float lx, ly, rx, ry, lt, rt;
        AxesEv(float lx, float ly, float rx, float ry, float lt, float rt) { this.lx = lx; this.ly = ly; this.rx = rx; this.ry = ry; this.lt = lt; this.rt = rt; }
        public void run() { FightoLib.nativeAxes(lx, ly, rx, ry, lt, rt); }
    }

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int masked = e.getActionMasked();
        switch (masked) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                int i = e.getActionIndex();
                view.queueEvent(new TouchEvent(0, e.getPointerId(i), e.getX(i), e.getY(i)));
                break;
            }
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < e.getPointerCount(); i++) view.queueEvent(new TouchEvent(1, e.getPointerId(i), e.getX(i), e.getY(i)));
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                int i = e.getActionIndex();
                view.queueEvent(new TouchEvent(2, e.getPointerId(i), e.getX(i), e.getY(i)));
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                for (int i = 0; i < e.getPointerCount(); i++) view.queueEvent(new TouchEvent(3, e.getPointerId(i), e.getX(i), e.getY(i)));
                break;
            default:
                break;
        }
        return true;
    }

    private static float axis(MotionEvent e, int a) {
        float v = e.getAxisValue(a);
        return Math.abs(v) < 0.12f ? 0.0f : v;
    }

    private float hatX, hatY;

    @Override
    public boolean onGenericMotionEvent(MotionEvent e) {
        if ((e.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK && e.getAction() == MotionEvent.ACTION_MOVE) {
            float lt = Math.max(axis(e, MotionEvent.AXIS_LTRIGGER), axis(e, MotionEvent.AXIS_BRAKE));
            float rt = Math.max(axis(e, MotionEvent.AXIS_RTRIGGER), axis(e, MotionEvent.AXIS_GAS));
            view.queueEvent(new AxesEv(axis(e, MotionEvent.AXIS_X), axis(e, MotionEvent.AXIS_Y), axis(e, MotionEvent.AXIS_Z), axis(e, MotionEvent.AXIS_RZ), lt, rt));
            // D-pad reported as a hat on many controllers
            float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X), hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
            if (hx != hatX) {
                if (hatX < -0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_LEFT, false));
                if (hatX > 0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_RIGHT, false));
                if (hx < -0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_LEFT, true));
                if (hx > 0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_RIGHT, true));
                hatX = hx;
            }
            if (hy != hatY) {
                if (hatY < -0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_UP, false));
                if (hatY > 0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_DOWN, false));
                if (hy < -0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_UP, true));
                if (hy > 0.5f) view.queueEvent(new KeyEv(KeyEvent.KEYCODE_DPAD_DOWN, true));
                hatY = hy;
            }
            return true;
        }
        return super.onGenericMotionEvent(e);
    }

    private static boolean isSystemKey(int code) {
        return code == KeyEvent.KEYCODE_VOLUME_UP || code == KeyEvent.KEYCODE_VOLUME_DOWN || code == KeyEvent.KEYCODE_VOLUME_MUTE
                || code == KeyEvent.KEYCODE_HOME || code == KeyEvent.KEYCODE_POWER || code == KeyEvent.KEYCODE_APP_SWITCH;
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent e) {
        if (isSystemKey(code)) return super.onKeyDown(code, e);
        if (e.getRepeatCount() == 0) view.queueEvent(new KeyEv(code, true));
        return true;
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent e) {
        if (isSystemKey(code)) return super.onKeyUp(code, e);
        view.queueEvent(new KeyEv(code, false));
        return true;
    }

    // ------------------------------------------------------------------ audio
    private static final class AudioThread extends Thread {
        volatile boolean running = true;
        volatile boolean paused = false;

        AudioThread() { super("FightoAudio"); }

        @Override
        public void run() {
            final int rate = 44100;
            final int frames = 1024;
            int min = AudioTrack.getMinBufferSize(rate, AudioFormat.CHANNEL_OUT_STEREO, AudioFormat.ENCODING_PCM_16BIT);
            AudioTrack track;
            try {
                track = new AudioTrack(AudioManager.STREAM_MUSIC, rate, AudioFormat.CHANNEL_OUT_STEREO,
                        AudioFormat.ENCODING_PCM_16BIT, Math.max(min, frames * 4 * 2), AudioTrack.MODE_STREAM);
            } catch (RuntimeException ex) {
                return;  // no audio device: the game runs silently
            }
            short[] pcm = new short[frames * 2];
            track.play();
            boolean playing = true;
            while (running) {
                if (paused) {
                    if (playing) { track.pause(); playing = false; }
                    try { Thread.sleep(30); } catch (InterruptedException ie) { break; }
                    continue;
                }
                if (!playing) { track.play(); playing = true; }
                FightoLib.nativeAudio(pcm, frames);
                track.write(pcm, 0, pcm.length);
            }
            track.stop();
            track.release();
        }
    }
}
