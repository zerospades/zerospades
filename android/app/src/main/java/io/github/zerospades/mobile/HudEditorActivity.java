package io.github.zerospades.mobile;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.SeekBar;
import android.widget.TextView;
import java.util.Arrays;
import io.github.zerospades.mobile.hud.HudLayoutStore;
import io.github.zerospades.mobile.hud.HudState;
import io.github.zerospades.mobile.hud.TouchController;
import io.github.zerospades.mobile.hud.TouchHudView;

/** Edits only a local layout; no native game input is sent from this screen. */
public final class HudEditorActivity extends Activity {
    private TouchHudView hud;
    private TextView selection;
    private SeekBar size;
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY |
                View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_FULLSCREEN);
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(0xff29383f);
        LinearLayout bar = new LinearLayout(this); bar.setBackgroundColor(0xff192127);
        selection = new TextView(this); selection.setTextColor(Color.WHITE);
        selection.setText("Drag an item; select it to resize"); selection.setTextSize(14);
        bar.addView(selection, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        size = new SeekBar(this); size.setMax(150); size.setProgress(50); size.setEnabled(false);
        bar.addView(size, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        Button reset = new Button(this); reset.setText("Reset"); reset.setOnClickListener(v -> hud.resetLayout()); bar.addView(reset);
        Button cancel = new Button(this); cancel.setText("Cancel"); cancel.setOnClickListener(v -> finish()); bar.addView(cancel);
        Button save = new Button(this); save.setText("Save"); save.setOnClickListener(v -> {
            HudLayoutStore.save(this, hud.getPlacements()); finish();
        }); bar.addView(save);
        root.addView(bar);
        hud = new TouchHudView(this, new TouchController.Sink() {
            @Override public void move(float x, float y) { }
            @Override public void look(float x, float y) { }
            @Override public void button(TouchController.Action action, boolean pressed) { }
        });
        hud.setEditing(true, (id, factor) -> {
            size.setEnabled(id != null);
            selection.setText(id == null ? "Drag an item; select it to resize" : id.replace('_', ' '));
            size.setProgress(Math.round((factor-.5f)*100));
        });
        size.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar s, int progress, boolean fromUser) {
                if (fromUser) hud.resizeSelected(.5f+progress/100f);
            }
            @Override public void onStartTrackingTouch(SeekBar s) { }
            @Override public void onStopTrackingTouch(SeekBar s) { }
        });
        hud.setState(new HudState(160, 125, 200, 100, 100, HudState.Tool.WEAPON,
                8, 72, 48, 3, 60, SystemClock.elapsedRealtime(), null, .5f, .5f,
                Arrays.asList("Example player [rifle] opponent")));
        root.addView(hud, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        setContentView(root);
    }
}
