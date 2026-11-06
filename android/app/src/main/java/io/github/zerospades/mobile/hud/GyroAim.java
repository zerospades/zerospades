package io.github.zerospades.mobile.hud;

/** Converts calibrated Android gyroscope rates into relative mouse-like deltas.
 *  Actual aiming and sensitivity are handled by the engine's normal MouseEvent path. */
public final class GyroAim {
    private static final float COUNTS_PER_RADIAN = 333.33334f;
    private static final float DEAD_ZONE = .015f;
    private long lastNanos;
    private final float[] delta = new float[2];
    public void reset() { lastNanos=0; }
    /** rotation is the Android Surface rotation value (0, 1, 2, 3). */
    public float[] sample(long timestamp, float x, float y, float z, int rotation) {
        if (!Float.isFinite(x) || !Float.isFinite(y) || !Float.isFinite(z) || timestamp <= 0) {
            reset(); return null;
        }
        long previous=lastNanos; lastNanos=timestamp;
        if (previous == 0 || timestamp <= previous || timestamp-previous > 250_000_000L) return null;
        float dt=Math.min(.05f, (timestamp-previous)*1e-9f);
        // Screen-aligned angular rates. Device rotation follows Surface's axes.
        float sx, sy;
        switch (rotation) {
            case 1: sx=-y; sy=x; break;
            case 2: sx=-x; sy=-y; break;
            case 3: sx=y; sy=-x; break;
            default: sx=x; sy=y;
        }
        float yaw=Math.abs(sy)<DEAD_ZONE ? 0 : -sy;
        float pitch=Math.abs(sx)<DEAD_ZONE ? 0 : -sx;
        delta[0]=yaw*dt*COUNTS_PER_RADIAN; delta[1]=pitch*dt*COUNTS_PER_RADIAN;
        return delta;
    }
}
