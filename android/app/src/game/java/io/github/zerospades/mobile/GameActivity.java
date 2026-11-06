package io.github.zerospades.mobile;

import android.content.SharedPreferences;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;
import io.github.zerospades.mobile.hud.ChatOverlay;
import io.github.zerospades.mobile.hud.ChatMessage;
import io.github.zerospades.mobile.hud.HudState;
import io.github.zerospades.mobile.hud.GyroAim;
import io.github.zerospades.mobile.hud.TouchController;
import io.github.zerospades.mobile.hud.NativeTouchSink;
import io.github.zerospades.mobile.hud.TouchHudView;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

/** Experimental SDL host for the ZeroSpades native shared library. */
public final class GameActivity extends SDLActivity {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private NativeTouchSink touchSink;
    private TouchHudView hud;
    private ChatOverlay chat;
    private int savedUiVisibility;
    private boolean wasFullscreen;
    private boolean hadClient;
    private List<String> killFeed = Collections.emptyList();
    private long joinedAt = -1;
    private boolean polling;
    private SensorManager sensors;
    private Sensor gyroscope;
    private final GyroAim gyroAim = new GyroAim();
    private final SensorEventListener gyroListener = new SensorEventListener() {
        @Override public void onSensorChanged(SensorEvent event) {
            if (!polling || hud == null || hud.getVisibility() != View.VISIBLE ||
                    !hasWindowFocus() || hud.isPieHeld() || (chat != null && chat.isOpen())) {
                gyroAim.reset(); return;
            }
            float[] delta = gyroAim.sample(event.timestamp, event.values[0], event.values[1], event.values[2],
                    getWindowManager().getDefaultDisplay().getRotation());
            if (delta != null && (delta[0] != 0 || delta[1] != 0)) touchSink.look(delta[0], delta[1]);
        }
        @Override public void onAccuracyChanged(Sensor sensor, int accuracy) { }
    };

    private final Runnable pollHud = new Runnable() {
        @Override public void run() {
            if (!polling || hud == null) return;
            int[] data = nativeReadHud();
            if (data != null && data.length == 14) {
                if (data[0] == 0) {
                    joinedAt = -1;
                    if (hadClient && chat != null) {
                        if (chat.isOpen()) chat.close();
                        chat.clearMessages();
                    }
                } else if (data[1] != 0 && joinedAt < 0) joinedAt = SystemClock.elapsedRealtime();
                hadClient = data[0] != 0;
                byte[] messages = nativeTakeChat();
                if (chat != null && messages != null)
                    for (ChatMessage message : ChatMessage.decode(messages))
                        chat.addMessage(message.prefix, message.name, message.suffix, message.color);
                byte[] kills = nativeTakeKillfeed();
                if (kills != null) killFeed = kills.length == 0 ? Collections.emptyList() :
                        Arrays.asList(new String(kills, StandardCharsets.UTF_8).split("\\n", -1));
                boolean gameplay = data[2] != 0 && (chat == null || !chat.isOpen());
                if (!gameplay && hud.getVisibility() == View.VISIBLE) hud.cancelTouches();
                hud.setVisibility(gameplay ? View.VISIBLE : View.GONE);
                if (gameplay) {
                    HudState.Tool tool;
                    switch (data[7]) {
                        case 0: tool = HudState.Tool.SPADE; break;
                        case 1: tool = HudState.Tool.BLOCK; break;
                        case 3: tool = HudState.Tool.GRENADE; break;
                        default: tool = HudState.Tool.WEAPON; break;
                    }
                    HudState.Weapon weapon = data[13] == 1 ? HudState.Weapon.SMG :
                            data[13] == 2 ? HudState.Weapon.SHOTGUN : HudState.Weapon.RIFLE;
                    hud.setState(new HudState(data[3], data[4], data[5], data[6], 100,
                            tool, weapon, data[8], data[9], data[10], data[11], data[12], joinedAt,
                            null, .5f, .5f, killFeed));
                }
            }
            ui.postDelayed(this, 33);
        }
    };

    @Override protected void onCreate(Bundle state) {
        // Also install when Android restores this activity in a fresh process.
        CrashDiagnostics.install(this);
        super.onCreate(state);
        if (mBrokenLibraries || mLayout == null) return;
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        touchSink = new NativeTouchSink();
        sensors = (SensorManager)getSystemService(SENSOR_SERVICE);
        gyroscope = sensors != null ? sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE) : null;
        hud = new TouchHudView(this, new TouchController.Sink() {
            @Override public void move(float x, float y) { touchSink.move(x, y); }
            @Override public void look(float x, float y) { touchSink.look(x, y); }
            @Override public void palette(float x, float y) { touchSink.palette(x, y); }
            @Override public void cancelPie() { touchSink.cancelPie(); }
            @Override public void button(TouchController.Action action, boolean pressed) {
                if (action == TouchController.Action.CHAT) {
                    if (pressed) {
                        // Defer cancellation until the touch controller finishes processing down.
                        touchSink.reset();
                        hud.post(() -> openChat());
                    }
                } else touchSink.button(action, pressed);
            }
        });
        // The renderer draws the real circular map at the matching HUD position.
        hud.setLayoutListener(GameActivity::nativeSetMapLayout);
        hud.setShowNativeMap(true);
        hud.setVisibility(View.GONE);
        mLayout.addView(hud, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        chat = new ChatOverlay(this, (message, global) ->
                nativeSendChat(message.getBytes(StandardCharsets.UTF_8), global));
        chat.setOnClosed(() -> {
            if (wasFullscreen) getWindow().addFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
            getWindow().getDecorView().setSystemUiVisibility(savedUiVisibility);
            if (mSurface != null) mSurface.requestFocus();
        });
        android.widget.RelativeLayout.LayoutParams chatParams = new android.widget.RelativeLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT);
        int margin = Math.round(12 * getResources().getDisplayMetrics().density);
        chatParams.setMargins(margin, margin, margin, margin);
        mLayout.addView(chat, chatParams);
    }

    private void openChat() {
        if (!polling || chat == null || chat.isOpen() || hud.getVisibility() != View.VISIBLE) return;
        hud.cancelTouches();
        touchSink.reset();
        hud.setVisibility(View.GONE);
        savedUiVisibility = getWindow().getDecorView().getSystemUiVisibility();
        wasFullscreen = (getWindow().getAttributes().flags & WindowManager.LayoutParams.FLAG_FULLSCREEN) != 0;
        getWindow().clearFlags(WindowManager.LayoutParams.FLAG_FULLSCREEN);
        getWindow().getDecorView().setSystemUiVisibility(0);
        chat.open();
    }

    @Override public void onBackPressed() {
        if (chat != null && chat.isOpen()) chat.close();
        else super.onBackPressed();
    }

    @Override public void onSystemUiVisibilityChange(int visibility) {
        // SDL normally re-hides system UI after two seconds; defer that while
        // the Android IME owns the screen so the composer stays accessible.
        if (chat == null || !chat.isOpen()) super.onSystemUiVisibilityChange(visibility);
    }

    @Override protected void onResume() {
        super.onResume();
        if (hud != null && !polling) { polling = true; ui.post(pollHud); }
        gyroAim.reset();
        if (sensors != null && gyroscope != null && getSharedPreferences("controls", MODE_PRIVATE).getBoolean("gyro", false))
            sensors.registerListener(gyroListener, gyroscope, SensorManager.SENSOR_DELAY_GAME);
    }

    @Override protected void onPause() {
        if (sensors != null) sensors.unregisterListener(gyroListener);
        gyroAim.reset();
        polling = false;
        ui.removeCallbacks(pollHud);
        if (hud != null) hud.cancelTouches();
        if (touchSink != null) touchSink.reset();
        if (chat != null && chat.isOpen()) chat.close();
        super.onPause();
    }

    @Override protected void onDestroy() {
        polling = false;
        ui.removeCallbacks(pollHud);
        super.onDestroy();
    }

    private static native void nativeSetMapLayout(float x, float y, float size);
    private static native int[] nativeReadHud();
    private static native void nativeSendChat(byte[] utf8, boolean global);
    private static native byte[] nativeTakeChat();
    private static native byte[] nativeTakeKillfeed();

    @Override protected String[] getLibraries() {
        // SDL 2.32.10 is linked into zerospades; its JNI callbacks are retained
        // by the native shared-library link. The NDK C++ runtime is packaged.
        return new String[] { "c++_shared", "zerospades" };
    }

    @Override protected String[] getArguments() {
        SharedPreferences preferences = getSharedPreferences("graphics", MODE_PRIVATE);
        List<String> args = new ArrayList<>();
        boolean showOriginal = getIntent() != null &&
                getIntent().getBooleanExtra(LauncherActivity.EXTRA_ORIGINAL_STARTUP, false);
        override(args, "cl_showStartupWindow", showOriginal);
        if (!showOriginal) {
            // Quick start preserves the previously tested mobile defaults.
            // Do not override any graphics values when the original screen is
            // shown: its controls must apply to the game and be saved normally.
            // The desktop software backend cannot render to the SDL Android surface.
            args.add("--override-cvar");
            args.add("r_renderer=gl");
            override(args, "r_vsync", preferences.getBoolean("r_vsync", true));
            override(args, "r_hdr", preferences.getBoolean("r_hdr", false));
            override(args, "r_radiosity", preferences.getBoolean("r_radiosity", false));
            override(args, "r_bloom", preferences.getBoolean("r_bloom", false));
            args.add("--override-cvar");
            args.add("r_water=" + (preferences.getBoolean("water_reflections", false) ? "2" : "1"));
        }
        return args.toArray(new String[0]);
    }

    private static void override(List<String> args, String setting, boolean value) {
        args.add("--override-cvar");
        args.add(setting + "=" + (value ? "1" : "0"));
    }
}
