package io.github.zerospades.mobile;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.SharedPreferences;
import android.content.Intent;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.os.Bundle;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** Android startup settings; the experimental native build installs assets before SDL. */
public final class LauncherActivity extends Activity {
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
        addText(content, R.string.subtitle, 16);
        if (BuildConfig.NATIVE_GAME) {
            TextView status = new TextView(this);
            status.setText("Experimental native build: rendering and touch controls are still being integrated.");
            status.setTextSize(16);
            status.setPadding(0, dp(12), 0, dp(8));
            content.addView(status);
        } else {
            addText(content, R.string.status, 16);
        }
        addText(content, R.string.graphics, 20);

        // The experimental SDL activity passes these existing ZeroSpades
        // settings as per-run overrides; the preview has no native engine.
        addSetting(content, R.string.vsync, "r_vsync", true);
        addSetting(content, R.string.hdr, "r_hdr", false);
        addSetting(content, R.string.radiosity, "r_radiosity", false);
        addSetting(content, R.string.bloom, "r_bloom", false);
        addSetting(content, R.string.water_reflections, "water_reflections", false);
        addText(content, R.string.jump_hint, 14);

        Button start = new Button(this);
        start.setText(R.string.start);
        start.setGravity(Gravity.CENTER);
        start.setOnClickListener(view -> {
            if (!BuildConfig.NATIVE_GAME) {
                new AlertDialog.Builder(this).setTitle(R.string.not_ready_title)
                        .setMessage(R.string.not_ready).setPositiveButton(R.string.ok, null).show();
                return;
            }
            // Asset installation may copy tens of MB; keep the UI responsive
            // and never start the native client with missing required paks.
            start.setEnabled(false);
            new Thread(() -> {
                Exception failure = null;
                try {
                    GameAssetsInstaller.install(getApplicationContext());
                    SystemCaBundle.install(getApplicationContext());
                }
                catch (Exception error) { failure = error; }
                final Exception installFailure = failure;
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    start.setEnabled(true);
                    if (installFailure != null) {
                        new AlertDialog.Builder(this).setTitle("Game startup preparation failed")
                                .setMessage(installFailure.getMessage())
                                .setPositiveButton(R.string.ok, null).show();
                        return;
                    }
                    Intent game = new Intent();
                    game.setClassName(this, "io.github.zerospades.mobile.GameActivity");
                    startActivity(game);
                });
            }, "game-assets-installer").start();
        });
        content.addView(start, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        if (BuildConfig.NATIVE_GAME) {
            Button diagnostics = new Button(this);
            diagnostics.setText("View crash report");
            diagnostics.setOnClickListener(view -> showDiagnostics(diagnostics));
            content.addView(diagnostics, new LinearLayout.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        }
        Button preview = new Button(this);
        preview.setText(R.string.preview_hud);
        preview.setOnClickListener(view -> startActivity(new Intent(this, HudPreviewActivity.class)));
        content.addView(preview, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
        setContentView(scroll);
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
