package io.github.zerospades.mobile;

import android.app.Activity;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.LinearGradient;
import android.graphics.Paint;
import android.graphics.Shader;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.FrameLayout;

import java.util.Arrays;
import io.github.zerospades.mobile.hud.ChatOverlay;
import io.github.zerospades.mobile.hud.HudState;
import io.github.zerospades.mobile.hud.TouchController;
import io.github.zerospades.mobile.hud.TouchHudView;

/** A standalone illustration of the overlay; it does not run or simulate ZeroSpades. */
public final class HudPreviewActivity extends Activity {
    private TouchHudView hud;
    private ChatOverlay chat;
    private HudState.Tool tool = HudState.Tool.WEAPON;
    private long started;

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setSoftInputMode(WindowManager.LayoutParams.SOFT_INPUT_ADJUST_RESIZE);
        immersive();
        started = SystemClock.elapsedRealtime();
        FrameLayout container = new FrameLayout(this);
        container.addView(new Backdrop());
        hud = new TouchHudView(this, new TouchController.Sink() {
            @Override public void move(float x, float y) { }
            @Override public void look(float dx, float dy) { }
            @Override public void button(TouchController.Action action, boolean pressed) {
                if (!pressed) return;
                if (action == TouchController.Action.PAUSE) finish();
                else if (action == TouchController.Action.CHAT) {
                    getWindow().getDecorView().setSystemUiVisibility(0);
                    chat.open();
                } else if (action == TouchController.Action.NEXT_TOOL) {
                    switch (tool) {
                        case SPADE: tool = HudState.Tool.BLOCK; break;
                        case BLOCK: tool = HudState.Tool.WEAPON; break;
                        case WEAPON: tool = HudState.Tool.GRENADE; break;
                        case GRENADE: tool = HudState.Tool.SPADE; break;
                    }
                    updateHud();
                }
            }
        });
        container.addView(hud);
        chat = new ChatOverlay(this, new ChatOverlay.Sender() {
            @Override public void send(String message, boolean global) { chat.addMessage(getString(R.string.preview_message, message)); }
        });
        chat.setOnClosed(new Runnable() { @Override public void run() { immersive(); } });
        chat.addMessage(getString(R.string.preview_chat));
        FrameLayout.LayoutParams chatParams = new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT, Gravity.CENTER);
        int margin = (int)(getResources().getDisplayMetrics().density * 12);
        chatParams.setMargins(margin, margin, margin, margin);
        container.addView(chat, chatParams);
        updateHud();
        setContentView(container);
    }
    private void updateHud() {
        // Example data, including the timer since this preview opened; not server values.
        hud.setState(new HudState(160, 125, 200, 100, 100, tool, 8, 72, 48, 3, 60,
                started, null, .5f, .5f,
                Arrays.asList("Example player  [rifle]  Example opponent", "Example opponent  [spade]  Example player")));
    }
    private void immersive() {
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN);
    }
    @Override public void onBackPressed() {
        if (chat != null && chat.isOpen()) chat.close(); else super.onBackPressed();
    }
    private final class Backdrop extends View {
        private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
        Backdrop() { super(HudPreviewActivity.this); }
        @Override protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            paint.setShader(new LinearGradient(0, 0, 0, h, 0xff536b87, 0xff293530, Shader.TileMode.CLAMP));
            canvas.drawRect(0, 0, w, h, paint);
            paint.setShader(null);
            paint.setColor(0xff394238);
            canvas.drawRect(0, h*.54f, w, h, paint);
            paint.setColor(Color.WHITE);
            paint.setTextAlign(Paint.Align.CENTER);
            paint.setTextSize(Math.max(14, h*.022f));
            paint.setShadowLayer(3, 1, 1, Color.BLACK);
            canvas.drawText(getString(R.string.preview_watermark), w/2, h*.43f, paint);
            paint.clearShadowLayer();
        }
    }
}
