package io.github.zerospades.mobile.hud;

/**
 * For a future SDL game activity after the native game library has loaded.
 * Do not use with the standalone HUD preview: the preview has no native game.
 * Calls enqueue events; the SDL loop delivers them on the game thread.
 */
public final class NativeTouchSink implements TouchController.Sink {
    @Override public void move(float x, float y) { nativeMove(x, y); }
    @Override public void look(float dx, float dy) { nativeLook(dx, dy); }
    @Override public void palette(float x, float y) { nativePalette(x, y); }
    @Override public void button(TouchController.Action action, boolean pressed) {
        final int id;
        switch (action) {
            case PAUSE: id = 0; break;
            case CHAT: id = 1; break;
            case FIRE: id = 2; break;
            case AIM: id = 3; break;
            case RELOAD: id = 4; break;
            case NEXT_TOOL: id = 5; break;
            case GRENADE: id = 6; break;
            case SPRINT: id = 7; break;
            case CROUCH: id = 8; break;
            case JUMP: id = 9; break;
            case LOADOUT: id = 10; break;
            case CAPTURE_COLOR: id = 11; break;
            default: throw new IllegalArgumentException("Unknown touch action");
        }
        nativeButton(id, pressed);
    }
    /** Release held inputs on activity pause before the native engine is stopped. */
    public void reset() { nativeReset(); }
    private static native void nativeMove(float x, float y);
    private static native void nativeLook(float dx, float dy);
    private static native void nativePalette(float x, float y);
    private static native void nativeButton(int action, boolean down);
    private static native void nativeReset();
}
