package io.github.zerospades.mobile;

import android.annotation.TargetApi;
import android.app.ActivityManager;
import android.app.ApplicationExitInfo;
import android.content.Context;
import android.os.Build;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.Date;
import java.util.List;

/** Local, opt-in diagnostics. No report leaves the device unless the user shares it. */
final class CrashDiagnostics {
    private static final String JAVA_CRASH = "last-java-crash.txt";
    private static final Object LOCK = new Object();
    private static boolean installed;
    private CrashDiagnostics() { }

    static synchronized void install(Context context) {
        if (installed) return;
        installed = true;
        final File crashFile = new File(context.getApplicationContext().getFilesDir(), JAVA_CRASH);
        final Thread.UncaughtExceptionHandler previous = Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread, error) -> {
            try {
                synchronized (LOCK) {
                    try (PrintWriter out = new PrintWriter(new OutputStreamWriter(
                            new FileOutputStream(crashFile), StandardCharsets.UTF_8))) {
                        out.println("Time: " + new Date());
                        out.println("Thread: " + thread.getName());
                        error.printStackTrace(out);
                    }
                }
            } catch (Throwable ignored) {
                // Never hide the original crash if diagnostic storage fails.
            }
            if (previous != null) previous.uncaughtException(thread, error);
            else {
                android.os.Process.killProcess(android.os.Process.myPid());
                System.exit(10);
            }
        });
    }

    static String collect(Context context) {
        StringBuilder report = new StringBuilder();
        report.append("ZeroSpades startup diagnostics\n")
                .append("Device: ").append(Build.MANUFACTURER).append(' ').append(Build.MODEL).append('\n')
                .append("Android: ").append(Build.VERSION.RELEASE).append(" (API ")
                .append(Build.VERSION.SDK_INT).append(")\n")
                .append("App: ").append(BuildConfig.VERSION_NAME).append('\n');
        // Put the engine log first so share targets that truncate long text do
        // not cut off the most useful startup information.
        appendFile(report, "Native engine log", new File(context.getFilesDir(),
                "user-resources/SystemMessages.log"), 24000);
        appendFile(report, "Last Java crash", new File(context.getFilesDir(), JAVA_CRASH), 12000);
        if (Build.VERSION.SDK_INT >= 30) Api30.appendExitInfo(context, report);
        return report.toString();
    }

    private static void appendFile(StringBuilder out, String title, File file, int limit) {
        out.append("\n--- ").append(title).append(" ---\n");
        if (!file.isFile()) { out.append("Not available.\n"); return; }
        out.append("Last modified: ").append(new Date(file.lastModified())).append('\n');
        try { out.append(DiagnosticFiles.tail(file, limit)).append('\n'); }
        catch (Exception e) { out.append("Could not read: ").append(e.getClass().getSimpleName()).append('\n'); }
    }

    @TargetApi(30)
    private static final class Api30 {
        static void appendExitInfo(Context context, StringBuilder out) {
            out.append("\n--- Recent process exits ---\n");
            try {
                ActivityManager manager = (ActivityManager) context.getSystemService(Context.ACTIVITY_SERVICE);
                if (manager == null) { out.append("Not available.\n"); return; }
                List<ApplicationExitInfo> exits = manager.getHistoricalProcessExitReasons(
                        context.getPackageName(), 0, 8);
                int found = 0;
                for (ApplicationExitInfo exit : exits) {
                    int reason = exit.getReason();
                    if (reason != ApplicationExitInfo.REASON_CRASH &&
                            reason != ApplicationExitInfo.REASON_CRASH_NATIVE &&
                            reason != ApplicationExitInfo.REASON_ANR) continue;
                    out.append("Time: ").append(new Date(exit.getTimestamp()))
                            .append(", reason: ").append(reason)
                            .append(", status: ").append(exit.getStatus())
                            .append(", description: ").append(exit.getDescription()).append('\n');
                    // Native traces are protobuf, not UTF-8 text; decode only
                    // the crashing thread rather than displaying binary data.
                    try (InputStream trace = exit.getTraceInputStream()) {
                        if (trace == null) out.append("Trace not available.\n");
                        else if (reason == ApplicationExitInfo.REASON_CRASH_NATIVE)
                            out.append(NativeTombstone.describe(trace)).append('\n');
                        else out.append(DiagnosticFiles.beginning(trace, 12000)).append('\n');
                    } catch (Exception e) {
                        out.append("Trace unavailable: ").append(e.getClass().getSimpleName()).append('\n');
                    }
                    if (++found == 1) break; // most recent relevant crash only
                }
                if (found == 0) out.append("No recent crash or ANR recorded by Android.\n");
            } catch (Exception e) {
                out.append("Unavailable: ").append(e.getClass().getSimpleName()).append('\n');
            }
        }
    }
}
