package io.github.zerospades.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.Intent;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Bundle;
import android.hardware.Sensor;
import android.hardware.SensorManager;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.SeekBar;
import android.widget.TextView;

import io.github.zerospades.mobile.hud.GyroAim;

/** Android startup settings; the experimental native build installs assets before SDL. */
public final class LauncherActivity extends Activity {
    static final String EXTRA_ORIGINAL_STARTUP = "original_startup";
    private SharedPreferences preferences;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        if (BuildConfig.NATIVE_GAME) CrashDiagnostics.install(this);
        preferences = getSharedPreferences("graphics", MODE_PRIVATE);

        int padding = dp(24);
        ScrollView scroll = new ScrollView(this);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(padding, padding, padding, padding);
        scroll.addView(content);

        TextView title = new TextView(this);
        title.setText(R.string.title);
        title.setTextSize(30);
        content.addView(title);
        if (BuildConfig.NATIVE_GAME) {
            TextView subtitle = new TextView(this);
            subtitle.setText(R.string.native_subtitle);
            subtitle.setTextSize(16);
            subtitle.setPadding(0, dp(12), 0, dp(8));
            content.addView(subtitle);
        } else {
            addText(content, R.string.subtitle, 16);
            addText(content, R.string.status, 16);
        }

        Button start = new Button(this);
        start.setText(BuildConfig.NATIVE_GAME ? R.string.original_settings_play : R.string.start);
        start.setGravity(Gravity.CENTER);
        Button quick = new Button(this);
        quick.setText(R.string.quick_start);
        start.setOnClickListener(view -> startGame(true, start, quick));
        quick.setOnClickListener(view -> startGame(false, start, quick));
        content.addView(start, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        if (BuildConfig.NATIVE_GAME) addGyroSettings(content);

        if (BuildConfig.NATIVE_GAME) {
            TextView quickTitle = new TextView(this);
            quickTitle.setText(R.string.quick_start_graphics);
            quickTitle.setTextSize(20);
            quickTitle.setPadding(0, dp(18), 0, dp(4));
            content.addView(quickTitle);
            TextView quickNote = new TextView(this);
            quickNote.setText(R.string.quick_start_note);
            quickNote.setTextSize(14);
            content.addView(quickNote);
        } else addText(content, R.string.graphics, 20);
        addSetting(content, R.string.vsync, "r_vsync", true);
        addSetting(content, R.string.hdr, "r_hdr", false);
        addSetting(content, R.string.radiosity, "r_radiosity", false);
        addSetting(content, R.string.bloom, "r_bloom", false);
        addSetting(content, R.string.water_reflections, "water_reflections", false);
        addText(content, R.string.jump_hint, 14);
        if (BuildConfig.NATIVE_GAME) content.addView(quick, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        if (BuildConfig.NATIVE_GAME) {
            Button diagnostics = new Button(this);
            diagnostics.setText("View crash report");
            diagnostics.setOnClickListener(view -> showDiagnostics(diagnostics));
            content.addView(diagnostics, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        Button editor = new Button(this);
        editor.setText("Customize HUD");
        editor.setOnClickListener(view -> startActivity(new Intent(this, HudEditorActivity.class)));
        content.addView(editor, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        Button preview = new Button(this);
        preview.setText(R.string.preview_hud);
        preview.setOnClickListener(view -> startActivity(new Intent(this, HudPreviewActivity.class)));
        content.addView(preview, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
    }

    private void addGyroSettings(LinearLayout content) {
        SharedPreferences controls = getSharedPreferences("controls", MODE_PRIVATE);
        SensorManager sensors = (SensorManager)getSystemService(SENSOR_SERVICE);
        boolean available = sensors != null && sensors.getDefaultSensor(Sensor.TYPE_GYROSCOPE) != null;
        CheckBox gyro = new CheckBox(this);
        gyro.setText(available ? R.string.gyro_enable : R.string.gyro_unavailable);
        gyro.setChecked(available && controls.getBoolean("gyro", false));
        gyro.setEnabled(available);
        content.addView(gyro);

        TextView label = new TextView(this);
        label.setTextSize(16);
        content.addView(label);
        SeekBar slider = new SeekBar(this);
        slider.setMax(49); // 0.1x to 5.0x in 0.1x steps
        float saved = GyroAim.clampSensitivity(controls.getFloat("gyro_sensitivity", 1f));
        slider.setProgress(Math.max(0, Math.min(49, Math.round(saved * 10) - 1)));
        slider.setEnabled(gyro.isChecked());
        label.setText(getString(R.string.gyro_sensitivity, (slider.getProgress()+1)/10f));
        slider.setContentDescription(getString(R.string.gyro_sensitivity_description));
        slider.setOnSeekBarChangeListener(new SeekBar.OnSeekBarChangeListener() {
            @Override public void onProgressChanged(SeekBar bar, int progress, boolean fromUser) {
                float value = (progress + 1) / 10f;
                label.setText(getString(R.string.gyro_sensitivity, value));
                if (fromUser) controls.edit().putFloat("gyro_sensitivity", value).apply();
            }
            @Override public void onStartTrackingTouch(SeekBar bar) { }
            @Override public void onStopTrackingTouch(SeekBar bar) { }
        });
        content.addView(slider);
        TextView note = new TextView(this);
        note.setText(R.string.gyro_sensitivity_note);
        note.setTextSize(14);
        content.addView(note);
        gyro.setOnCheckedChangeListener((button, checked) -> {
            controls.edit().putBoolean("gyro", checked).apply();
            slider.setEnabled(checked);
        });
    }

    private void startGame(boolean showOriginal, Button start, Button quick) {
        if (!BuildConfig.NATIVE_GAME) {
            new AlertDialog.Builder(this).setTitle(R.string.not_ready_title)
                    .setMessage(R.string.not_ready).setPositiveButton(R.string.ok, null).show();
            return;
        }
        // Asset installation may copy tens of MB. Disable both entry points
        // until it finishes so two native activities cannot start at once.
        start.setEnabled(false);
        quick.setEnabled(false);
        new Thread(() -> {
            Exception failure = null;
            try {
                GameAssetsInstaller.install(getApplicationContext());
                SystemCaBundle.install(getApplicationContext());
            } catch (Exception error) { failure = error; }
            final Exception installFailure = failure;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                start.setEnabled(true);
                quick.setEnabled(true);
                if (installFailure != null) {
                    new AlertDialog.Builder(this).setTitle("Game startup preparation failed")
                            .setMessage(installFailure.getMessage())
                            .setPositiveButton(R.string.ok, null).show();
                    return;
                }
                Intent game = new Intent();
                game.setClassName(this, "io.github.zerospades.mobile.GameActivity");
                game.putExtra(EXTRA_ORIGINAL_STARTUP, showOriginal);
                startActivity(game);
            });
        }, "game-assets-installer").start();
    }

    private void showDiagnostics(Button button) {
        button.setEnabled(false);
        new Thread(() -> {
            String report;
            try { report = CrashDiagnostics.collect(getApplicationContext()); }
            catch (Throwable error) { report = "Could not collect diagnostics: " + error; }
            final String result = report;
            runOnUiThread(() -> {
                if (isFinishing() || isDestroyed()) return;
                button.setEnabled(true);
                new AlertDialog.Builder(this)
                        .setTitle("Crash report")
                        .setMessage("Review before sharing: this may include device details, " +
                                "file paths, server addresses and other log information. " +
                                "Nothing is sent automatically.\n\n" + result)
                        .setNegativeButton("Close", null)
                        .setNeutralButton("Copy", (dialog, which) -> {
                            ClipboardManager clipboard = (ClipboardManager) getSystemService(CLIPBOARD_SERVICE);
                            if (clipboard != null) clipboard.setPrimaryClip(ClipData.newPlainText("ZeroSpades crash report", result));
                        })
                        .setPositiveButton("Share", (dialog, which) -> {
                            Intent intent = new Intent(Intent.ACTION_SEND);
                            intent.setType("text/plain");
                            intent.putExtra(Intent.EXTRA_SUBJECT, "ZeroSpades crash report");
                            intent.putExtra(Intent.EXTRA_TEXT, result);
                            startActivity(Intent.createChooser(intent, "Share crash report"));
                        }).show();
            });
        }, "crash-diagnostics").start();
    }

    private void addText(LinearLayout content, int text, int size) {
        TextView view = new TextView(this);
        view.setText(text);
        view.setTextSize(size);
        view.setPadding(0, dp(12), 0, dp(8));
        content.addView(view);
    }

    private void addSetting(LinearLayout content, int label, String key, boolean defaultValue) {
        CheckBox check = new CheckBox(this);
        check.setText(label);
        check.setChecked(preferences.getBoolean(key, defaultValue));
        check.setOnCheckedChangeListener((button, checked) ->
                preferences.edit().putBoolean(key, checked).apply());
        content.addView(check);
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
