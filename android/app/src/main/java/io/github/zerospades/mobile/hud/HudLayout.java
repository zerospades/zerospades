package io.github.zerospades.mobile.hud;

import java.util.Collections;
import java.util.Map;

/** Layout uses a 980-unit high landscape viewport. Edited positions are normalized. */
public final class HudLayout {
    public static final float HEIGHT = 980f;
    public static final float PALETTE_X = 250f, PALETTE_Y = 390f, PALETTE_SIZE = 165f;
    public final float width;
    public final float stickX, stickY, stickRadius;
    public final Widget map, scores, timer, killFeed;
    public final Button[] buttons;

    /** Stored coordinates are fractions of screen width and height, plus a size multiplier. */
    public static final class Placement {
        public final float x, y, size;
        public Placement(float x, float y, float size) { this.x=x; this.y=y; this.size=size; }
        public boolean valid() {
            return Float.isFinite(x) && Float.isFinite(y) && Float.isFinite(size) &&
                    x >= 0 && x <= 1 && y >= 0 && y <= 1 && size >= .5f && size <= 2f;
        }
    }
    public static final class Widget {
        public final String id;
        public final float x, y, size;
        Widget(String id, float x, float y, float size) { this.id=id; this.x=x; this.y=y; this.size=size; }
    }
    public static final class Button {
        public final String id;
        public final TouchController.Action action;
        public final float x, y, radius, halfWidth, halfHeight, size;
        Button(String id, TouchController.Action action, Widget w, float radius, float halfWidth, float halfHeight) {
            this.id=id; this.action=action; x=w.x; y=w.y; size=w.size;
            this.radius=radius*w.size; this.halfWidth=halfWidth*w.size; this.halfHeight=halfHeight*w.size;
        }
    }
    public static final String[] EDITABLE_IDS = {
        "stick", "pause", "chat", "fire_left", "aim", "fire_right", "jump", "reload",
        "tool", "grenade", "sprint", "crouch", "pie", "flashlight", "map", "scores", "timer", "killfeed"
    };
    private final Map<String, Placement> overrides;
    public HudLayout(float width) { this(width, Collections.<String, Placement>emptyMap()); }
    public HudLayout(float width, Map<String, Placement> overrides) {
        this.width=Math.max(1, width);
        this.overrides=overrides;
        Widget stick = widget("stick", 284, 832);
        stickX=stick.x; stickY=stick.y; stickRadius=105*stick.size;
        map=widget("map", 138, 138);
        scores=widget("scores", this.width/2, 0);
        timer=widget("timer", this.width/2, 88);
        killFeed=widget("killfeed", 40, 270);
        buttons = new Button[] {
            button("pause", TouchController.Action.PAUSE, 40, 39, 29),
            button("chat", TouchController.Action.CHAT, 255, 59, 29),
            button("fire_left", TouchController.Action.FIRE, 137, 608, 52),
            button("aim", TouchController.Action.AIM, this.width-118, 627, 64),
            button("fire_right", TouchController.Action.FIRE, this.width-294, 725, 77),
            button("jump", TouchController.Action.JUMP, this.width-465, 690, 50),
            button("reload", TouchController.Action.RELOAD, this.width-68, 783, 50),
            rect("tool", TouchController.Action.NEXT_TOOL, this.width-153, 70, 132, 53),
            button("grenade", TouchController.Action.GRENADE, this.width-588, 922, 42),
            button("sprint", TouchController.Action.SPRINT, this.width-393, 900, 60),
            button("crouch", TouchController.Action.CROUCH, this.width-196, 900, 60),
            button("pie", TouchController.Action.PIE_MENU, this.width/2-65, 933, 37),
            button("flashlight", TouchController.Action.FLASHLIGHT, this.width/2+65, 933, 37)
        };
    }
    private Widget widget(String id, float x, float y) {
        Placement p=overrides.get(id);
        if (p != null && p.valid()) return new Widget(id, p.x*width, p.y*HEIGHT, p.size);
        return new Widget(id, x, y, 1);
    }
    private Button button(String id, TouchController.Action action, float x, float y, float radius) {
        return new Button(id, action, widget(id,x,y), radius, 0, 0);
    }
    private Button rect(String id, TouchController.Action action, float x, float y, float w, float h) {
        return new Button(id, action, widget(id,x,y), 0, w, h);
    }
    public Widget find(String id) {
        if ("stick".equals(id)) return new Widget(id, stickX, stickY, stickRadius/105);
        if ("map".equals(id)) return map;
        if ("scores".equals(id)) return scores;
        if ("timer".equals(id)) return timer;
        if ("killfeed".equals(id)) return killFeed;
        for (Button b : buttons) if (b.id.equals(id)) return new Widget(id, b.x, b.y, b.size);
        return null;
    }
}
