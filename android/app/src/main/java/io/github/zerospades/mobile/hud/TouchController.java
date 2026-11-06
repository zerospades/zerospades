package io.github.zerospades.mobile.hud;

import java.util.HashMap;
import java.util.Map;

/** Platform-independent multi-touch ownership. Coordinates are in HudLayout units. */
public final class TouchController {
    public enum Action { PAUSE, CHAT, FIRE, AIM, RELOAD, NEXT_TOOL, GRENADE, SPRINT, CROUCH, JUMP, LOADOUT, CAPTURE_COLOR }
    public interface Sink {
        /** Forward is negative y, strafe right is positive x; both are in [-1, 1]. */
        void move(float x, float y);
        /** Relative camera motion in HUD units, not absolute screen coordinates. */
        void look(float dx, float dy);
        /** The first press and last release are reported for each action. */
        void button(Action action, boolean pressed);
        /** Select a point in the real block palette, normalized to [0, 1). */
        default void palette(float x, float y) { }
    }
    private enum Kind { STICK, LOOK, BUTTON, PALETTE, IGNORED }
    private static final class Pointer {
        Kind kind;
        Action action;
        float x, y, startX, startY;
        long startedAt;
        boolean tapCandidate, wasDoubleTap, longPressed;
        Pointer(Kind kind, float x, float y) {
            this.kind = kind; this.x = this.startX = x; this.y = this.startY = y;
        }
    }
    private final Sink sink;
    private final Map<Integer, Pointer> pointers = new HashMap<>();
    private HudLayout layout;
    private float stickX, stickY;
    private boolean blockPaletteEnabled;
    private long lastStickTap = -1;
    private float lastTapX, lastTapY;
    private static final long TAP_MS = 220, DOUBLE_TAP_MS = 320;

    public TouchController(HudLayout layout, Sink sink) {
        if (layout == null || sink == null) throw new IllegalArgumentException("layout and sink required");
        this.layout = layout;
        this.sink = sink;
    }
    public void setLayout(HudLayout layout) { cancel(); this.layout = layout; }
    public void setBlockPaletteEnabled(boolean enabled) { blockPaletteEnabled = enabled; }
    public float stickX() { return stickX; }
    public float stickY() { return stickY; }
    public boolean pressed(Action action) {
        for (Pointer p : pointers.values()) if (p.kind == Kind.BUTTON && p.action == action) return true;
        return false;
    }
    public void down(int id, float x, float y) {
        down(id, x, y, System.nanoTime() / 1_000_000L);
    }
    public void down(int id, float x, float y, long timeMs) {
        if (pointers.containsKey(id)) return;
        HudLayout.Button best = null;
        float bestDistance = Float.MAX_VALUE;
        for (HudLayout.Button b : layout.buttons) {
            float dx = x - b.x, dy = y - b.y;
            float distance;
            if (b.radius == 0) {
                if (Math.abs(dx) > b.halfWidth || Math.abs(dy) > b.halfHeight) continue;
                distance = 0;
            } else {
                distance = (dx * dx + dy * dy) / (b.radius * b.radius);
            }
            if (distance <= 1.15f * 1.15f && distance < bestDistance) {
                best = b;
                bestDistance = distance;
            }
        }
        if (best != null) {
            boolean alreadyPressed = pressed(best.action);
            Pointer p = new Pointer(Kind.BUTTON, x, y);
            p.action = best.action;
            p.startedAt = timeMs;
            if (p.action == Action.NEXT_TOOL)
                for (Pointer other : pointers.values())
                    if (other.kind == Kind.BUTTON && other.action == Action.NEXT_TOOL && other.longPressed)
                        p.longPressed = true;
            pointers.put(id, p);
            // Tool switch is a tap; holding it opens the existing loadout menu.
            if (!alreadyPressed && p.action != Action.NEXT_TOOL) sink.button(p.action, true);
            return;
        }
        if (blockPaletteEnabled && inPalette(x, y)) {
            Pointer p = new Pointer(Kind.PALETTE, x, y);
            p.startedAt = timeMs; p.tapCandidate = true;
            pointers.put(id, p);
            selectPalette(x, y);
            return;
        }
        float dx = x - layout.stickX, dy = y - layout.stickY;
        if (dx * dx + dy * dy <= layout.stickRadius * layout.stickRadius * 1.44f) {
            for (Pointer p : pointers.values()) {
                if (p.kind == Kind.STICK) {
                    pointers.put(id, new Pointer(Kind.IGNORED, x, y));
                    return;
                }
            }
            Pointer p = new Pointer(Kind.STICK, x, y);
            p.startedAt = timeMs;
            p.tapCandidate = true;
            float tapDx = x - lastTapX, tapDy = y - lastTapY;
            float maxDistance = layout.stickRadius * .5f;
            if (lastStickTap >= 0 && timeMs >= lastStickTap && timeMs - lastStickTap <= DOUBLE_TAP_MS
                    && tapDx * tapDx + tapDy * tapDy <= maxDistance * maxDistance) {
                p.wasDoubleTap = true;
                lastStickTap = -1;
                // Native input holds this pulse for a complete game frame.
                sink.button(Action.JUMP, true);
                sink.button(Action.JUMP, false);
            }
            pointers.put(id, p);
            updateStick(x, y);
        } else {
            // Only one look pointer can own camera movement. Extra fingers cannot jump the camera.
            Kind kind = Kind.LOOK;
            for (Pointer p : pointers.values()) if (p.kind == Kind.LOOK) kind = Kind.IGNORED;
            pointers.put(id, new Pointer(kind, x, y));
        }
    }
    public void move(int id, float x, float y) {
        Pointer p = pointers.get(id);
        if (p == null) return;
        if (p.kind == Kind.STICK) {
            float dx = x - p.startX, dy = y - p.startY;
            float slop = layout.stickRadius * .25f;
            if (dx * dx + dy * dy > slop * slop) p.tapCandidate = false;
            updateStick(x, y);
        } else if (p.kind == Kind.PALETTE) {
            float dx = x - p.startX, dy = y - p.startY;
            if (dx * dx + dy * dy > 20f * 20f) p.tapCandidate = false;
            if (!p.longPressed && blockPaletteEnabled && inPalette(x, y)) selectPalette(x, y);
        } else if (p.kind == Kind.LOOK) sink.look(x - p.x, y - p.y);
        p.x = x;
        p.y = y;
    }
    public void up(int id) { up(id, System.nanoTime() / 1_000_000L); }
    public void up(int id, long timeMs) {
        Pointer p = pointers.remove(id);
        if (p == null) return;
        if (p.kind == Kind.STICK) {
            if (!p.wasDoubleTap && p.tapCandidate && timeMs >= p.startedAt && timeMs - p.startedAt <= TAP_MS) {
                lastStickTap = timeMs; lastTapX = p.startX; lastTapY = p.startY;
            } else lastStickTap = -1;
            stickX = stickY = 0;
            sink.move(0, 0);
        } else if (p.kind == Kind.BUTTON && !pressed(p.action)) {
            if (p.action == Action.NEXT_TOOL) {
                if (!p.longPressed) { sink.button(p.action, true); sink.button(p.action, false); }
            } else sink.button(p.action, false);
        }
    }
    /** Only tool-panel and palette touches need a delayed hold callback. */
    public boolean isLongPressCandidate(int id) {
        Pointer p = pointers.get(id);
        return p != null && (p.kind == Kind.PALETTE ||
                (p.kind == Kind.BUTTON && p.action == Action.NEXT_TOOL));
    }
    /** True only for the same still-held tool-panel pointer that scheduled the callback. */
    public boolean longPressTool(int id, long startedAt) {
        Pointer p = pointers.get(id);
        if (p == null || p.kind != Kind.BUTTON || p.action != Action.NEXT_TOOL ||
                p.startedAt != startedAt || p.longPressed) return false;
        // All pointers on this shared button belong to the same long press.
        for (Pointer other : pointers.values())
            if (other.kind == Kind.BUTTON && other.action == Action.NEXT_TOOL) other.longPressed = true;
        sink.button(Action.LOADOUT, true);
        sink.button(Action.LOADOUT, false);
        return true;
    }
    /** Holding a stationary palette touch grabs the block currently under the crosshair. */
    public boolean longPressPalette(int id, long startedAt) {
        Pointer p = pointers.get(id);
        if (!blockPaletteEnabled || p == null || p.kind != Kind.PALETTE ||
                p.startedAt != startedAt || p.longPressed || !p.tapCandidate) return false;
        p.longPressed = true;
        sink.button(Action.CAPTURE_COLOR, true);
        sink.button(Action.CAPTURE_COLOR, false);
        return true;
    }
    public void cancel() {
        // Removing one at a time ensures shared actions have exactly one release.
        // A cancelled tool tap must not switch tools.
        for (Pointer p : pointers.values()) if (p.action == Action.NEXT_TOOL) p.longPressed = true;
        for (Integer id : pointers.keySet().toArray(new Integer[0])) up(id);
        lastStickTap = -1;
    }
    private boolean inPalette(float x, float y) {
        return x >= HudLayout.PALETTE_X && x < HudLayout.PALETTE_X + HudLayout.PALETTE_SIZE &&
                y >= HudLayout.PALETTE_Y && y < HudLayout.PALETTE_Y + HudLayout.PALETTE_SIZE;
    }
    private void selectPalette(float x, float y) {
        sink.palette((x - HudLayout.PALETTE_X) / HudLayout.PALETTE_SIZE,
                (y - HudLayout.PALETTE_Y) / HudLayout.PALETTE_SIZE);
    }
    private void updateStick(float x, float y) {
        float dx = (x - layout.stickX) / layout.stickRadius;
        float dy = (y - layout.stickY) / layout.stickRadius;
        float magnitude = (float)Math.sqrt(dx * dx + dy * dy);
        if (magnitude > 1) { dx /= magnitude; dy /= magnitude; }
        stickX = dx; stickY = dy;
        // Small dead zone avoids accidental drift while retaining full range.
        float strength = Math.min(1, magnitude);
        if (strength < 0.08f) sink.move(0, 0);
        else {
            float scale = (strength - 0.08f) / (0.92f * strength);
            sink.move(dx * scale, dy * scale);
        }
    }
}
