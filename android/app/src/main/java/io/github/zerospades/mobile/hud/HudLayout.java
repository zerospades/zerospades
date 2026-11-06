package io.github.zerospades.mobile.hud;

/** Layout is expressed in a 980-unit high landscape viewport. */
public final class HudLayout {
    public static final float HEIGHT = 980f;
    // Mirrors Android's engine-rendered palette rectangle in PaletteView.cpp.
    public static final float PALETTE_X = 250f, PALETTE_Y = 390f, PALETTE_SIZE = 165f;
    public final float width;
    public final float stickX = 284f, stickY = 832f, stickRadius = 105f;
    public final Button[] buttons;

    public static final class Button {
        public final TouchController.Action action;
        public final float x, y, radius;
        public final float halfWidth, halfHeight;
        Button(TouchController.Action action, float x, float y, float radius) {
            this.action = action;
            this.x = x;
            this.y = y;
            this.radius = radius;
            this.halfWidth = 0;
            this.halfHeight = 0;
        }
        Button(TouchController.Action action, float x, float y, float halfWidth, float halfHeight) {
            this.action = action;
            this.x = x;
            this.y = y;
            this.radius = 0;
            this.halfWidth = halfWidth;
            this.halfHeight = halfHeight;
        }
    }

    public HudLayout(float width) {
        this.width = width;
        buttons = new Button[] {
            new Button(TouchController.Action.PAUSE, 40, 39, 29),
            new Button(TouchController.Action.CHAT, 255, 59, 29),
            new Button(TouchController.Action.FIRE, 137, 608, 52),
            new Button(TouchController.Action.AIM, width - 118, 627, 64),
            new Button(TouchController.Action.FIRE, width - 294, 725, 77),
            new Button(TouchController.Action.RELOAD, width - 68, 783, 50),
            new Button(TouchController.Action.NEXT_TOOL, width - 153, 70, 132, 53),
            new Button(TouchController.Action.GRENADE, width - 588, 922, 42),
            new Button(TouchController.Action.SPRINT, width - 393, 900, 60),
            new Button(TouchController.Action.CROUCH, width - 196, 900, 60)
        };
    }
}
