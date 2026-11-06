package io.github.zerospades.mobile.hud;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.text.TextUtils;

import io.github.zerospades.mobile.R;
import java.util.Collections;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;

import java.util.Locale;

/** Overlay only; the native game must supply state and handle input through Sink. */
public final class TouchHudView extends View {
    private static final int WHITE = Color.rgb(245, 245, 242);
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path path = new Path();
    private final TouchController controller;
    private HudLayout layout = new HudLayout(1568);
    private HudState state = new HudState(0, 0, 0, 0, 0, HudState.Tool.WEAPON, 0, 0, 0, 0, -1,
            -1, null, .5f, .5f, Collections.<String>emptyList());
    private final Bitmap[] toolIcons = new Bitmap[HudState.Tool.values().length];
    private final Bitmap[] weaponIcons = new Bitmap[HudState.Weapon.values().length];
    private float scale = 1;
    private boolean showPlaceholderMap = true;
    private boolean decorateNativeMap;

    /** The native renderer draws the actual map beneath this transparent view. */
    public void setShowNativeMap(boolean show) {
        decorateNativeMap = show;
        showPlaceholderMap = !show;
        invalidate();
    }

    public TouchHudView(Context context, TouchController.Sink sink) {
        super(context);
        controller = new TouchController(layout, sink);
        setFocusable(true);
        weaponIcons[HudState.Weapon.RIFLE.ordinal()] = BitmapFactory.decodeResource(getResources(), R.drawable.rifle);
        weaponIcons[HudState.Weapon.SMG.ordinal()] = BitmapFactory.decodeResource(getResources(), R.drawable.smg);
        weaponIcons[HudState.Weapon.SHOTGUN.ordinal()] = BitmapFactory.decodeResource(getResources(), R.drawable.shotgun);
        toolIcons[HudState.Tool.BLOCK.ordinal()] = BitmapFactory.decodeResource(getResources(), R.drawable.block);
        toolIcons[HudState.Tool.SPADE.ordinal()] = BitmapFactory.decodeResource(getResources(), R.drawable.spade);
        toolIcons[HudState.Tool.GRENADE.ordinal()] = BitmapFactory.decodeResource(getResources(), R.drawable.grenade);
    }

    /** Call on the Android UI thread. The map bitmap must remain valid while in use. */
    public void setState(HudState state) {
        if (state == null) throw new IllegalArgumentException("state required");
        this.state = state;
        controller.setBlockPaletteEnabled(decorateNativeMap && state.tool == HudState.Tool.BLOCK);
        invalidate();
    }

    /** Release pointer-owned actions before hiding the HUD or leaving gameplay. */
    public void cancelTouches() { controller.cancel(); invalidate(); }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (h <= 0) return;
        scale = h / HudLayout.HEIGHT;
        layout = new HudLayout(w / scale);
        controller.setLayout(layout);
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (getHeight() <= 0) return;
        canvas.save();
        canvas.scale(scale, scale);
        if (state.map != null || showPlaceholderMap || decorateNativeMap) drawMap(canvas);
        drawKillFeed(canvas);
        drawScores(canvas);
        drawAmmo(canvas);
        // The real weapon skin supplies crosshair/scope visuals in the game.
        if (!decorateNativeMap) drawCrosshair(canvas, layout.width / 2, 488, 1);
        for (HudLayout.Button button : layout.buttons) drawButton(canvas, button);
        drawStick(canvas);
        if (state.fps >= 0) text(canvas, Integer.toString(state.fps), 68, 962, 22, WHITE, Paint.Align.LEFT);
        canvas.restore();
        if (state.joinedAtMillis >= 0) postInvalidateDelayed(250);
    }

    private void fill(Canvas c, int color, float l, float t, float r, float b) {
        paint.setStyle(Paint.Style.FILL); paint.setColor(color);
        c.drawRect(l, t, r, b, paint);
    }
    private void line(Canvas c, int color, float stroke, float x1, float y1, float x2, float y2) {
        paint.setColor(color); paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(stroke);
        paint.setStrokeCap(Paint.Cap.BUTT);
        c.drawLine(x1, y1, x2, y2, paint);
        paint.setStyle(Paint.Style.FILL);
    }
    private void text(Canvas c, String text, float x, float y, float size, int color, Paint.Align align) {
        paint.setStyle(Paint.Style.FILL); paint.setTextAlign(align); paint.setColor(color);
        paint.setTypeface(android.graphics.Typeface.create("sans-serif", android.graphics.Typeface.NORMAL));
        paint.setTextSize(size); paint.setShadowLayer(2, 1, 2, Color.BLACK);
        c.drawText(text, x, y, paint); paint.clearShadowLayer();
    }
    private void ring(Canvas c, float x, float y, float radius, boolean active) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(active ? 0x88737c78 : 0x55090d0b); c.drawCircle(x, y, radius + 7, paint);
        paint.setColor(active ? 0x889fa9a4 : 0x33121614); c.drawCircle(x, y, radius, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(0xbbe5e9e3);
        c.drawCircle(x, y, radius - 1, paint); paint.setStyle(Paint.Style.FILL);
    }
    private void drawMap(Canvas c) {
        float x = 138, y = 138, r = 99;
        if (!decorateNativeMap) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(0x99323e52); c.drawCircle(x, y, r, paint);
        }
        c.save(); path.reset(); path.addCircle(x, y, r - 5, Path.Direction.CW); c.clipPath(path);
        Bitmap map = state.map;
        if (map != null && !map.isRecycled()) {
            paint.setColor(Color.WHITE); c.drawBitmap(map, null, new RectF(x-r, y-r, x+r, y+r), paint);
            if (Float.isFinite(state.playerX) && Float.isFinite(state.playerY)) {
                float px = x-r + clamp(state.playerX) * 2*r, py = y-r + clamp(state.playerY) * 2*r;
                paint.setColor(0xffff832c); c.drawCircle(px, py, 7, paint);
            }
        }
        c.restore();
        for (int i = 0; i < 48; i++) {
            double a = Math.PI * 2 * i / 48;
            float outer = r, inner = r - (i % 4 == 0 ? 8 : 4);
            line(c, 0xbbe7eae5, 1.5f, x+(float)Math.sin(a)*inner, y-(float)Math.cos(a)*inner,
                    x+(float)Math.sin(a)*outer, y-(float)Math.cos(a)*outer);
        }
        text(c, "N", x, y-r+16, 18, WHITE, Paint.Align.CENTER);
    }
    private static float clamp(float n) { return Math.max(0, Math.min(1, n)); }
    private void drawKillFeed(Canvas c) {
        int first = Math.max(0, state.killFeed.size() - 5);
        paint.setTextSize(22);
        for (int i = first; i < state.killFeed.size(); i++) {
            String entry = state.killFeed.get(i);
            if (entry == null) continue;
            String clipped = TextUtils.ellipsize(entry, new android.text.TextPaint(paint),
                    Math.min(430, layout.width * .30f), TextUtils.TruncateAt.END).toString();
            text(c, clipped, 40, 270 + (i - first) * 27, 22, WHITE, Paint.Align.LEFT);
        }
    }
    private void drawScores(Canvas c) {
        float center = layout.width / 2;
        float fraction1 = state.scoreFraction(state.teamOneScore);
        float fraction2 = state.scoreFraction(state.teamTwoScore);
        // Current scores above their own bars. No flags; an unknown limit has no fill.
        text(c, Integer.toString(state.teamOneScore), center-240, 45, 30, WHITE, Paint.Align.CENTER);
        text(c, Integer.toString(state.teamTwoScore), center+240, 45, 30, WHITE, Paint.Align.CENTER);
        fill(c, 0x55302c35, center-350, 56, center-130, 69);
        fill(c, 0x55404a55, center+130, 56, center+350, 69);
        fill(c, 0xbb922b37, center-350, 56, center-350+220*fraction1, 69);
        fill(c, 0xbb529caf, center+130, 56, center+130+220*fraction2, 69);
        String time = "--:--";
        if (state.joinedAtMillis >= 0) {
            long seconds = Math.max(0, SystemClock.elapsedRealtime() - state.joinedAtMillis) / 1000;
            time = String.format(Locale.ROOT, "%02d:%02d", seconds / 60, seconds % 60);
        }
        text(c, time, center, 52, 30, WHITE, Paint.Align.CENTER);
    }
    private void drawAmmo(Canvas c) {
        float right = layout.width - 20;
        if (controller.pressed(TouchController.Action.NEXT_TOOL))
            fill(c, 0x447f8e88, right-265, 17, right, 123);
        Bitmap icon = state.tool == HudState.Tool.WEAPON
                ? weaponIcons[state.weapon.ordinal()] : toolIcons[state.tool.ordinal()];
        if (icon != null && !icon.isRecycled()) {
            paint.setColor(WHITE); paint.setStyle(Paint.Style.FILL);
            c.drawBitmap(icon, null, new RectF(right-215, 24, right-105, 104), paint);
        }
        int count = state.tool == HudState.Tool.WEAPON ? state.ammo :
                state.tool == HudState.Tool.GRENADE ? state.grenades : state.blocks;
        text(c, Integer.toString(Math.max(0, count)), right-10, 72, 44, WHITE, Paint.Align.RIGHT);
        if (state.tool == HudState.Tool.WEAPON)
            text(c, Integer.toString(Math.max(0, state.reserveAmmo)), right-10, 103, 25, 0xffb6b9bd, Paint.Align.RIGHT);
        line(c, WHITE, 4, right-265, 128, right, 128);
        fill(c, 0x88481313, right-265, 143, right, 174);
        float health = state.maxHealth > 0 ? clamp((float)state.health / state.maxHealth) : 0;
        fill(c, 0xffd70a14, right-265, 143, right-265 + 265*health, 174);
        text(c, Math.max(0, state.health) + " +", right-8, 167, 25, WHITE, Paint.Align.RIGHT);
    }
    private void drawStick(Canvas c) {
        ring(c, layout.stickX, layout.stickY, layout.stickRadius, false);
        paint.setStyle(Paint.Style.FILL); paint.setColor(0x887e877e);
        float x = layout.stickX + controller.stickX()*layout.stickRadius*.55f;
        float y = layout.stickY + controller.stickY()*layout.stickRadius*.55f;
        c.drawCircle(x, y, 49, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(0xcce6e9e2);
        c.drawCircle(x, y, 49, paint); paint.setStyle(Paint.Style.FILL);
    }
    private void drawButton(Canvas c, HudLayout.Button b) {
        if (b.action == TouchController.Action.NEXT_TOOL) return; // tool panel is the button
        ring(c, b.x, b.y, b.radius, controller.pressed(b.action));
        c.save(); c.translate(b.x, b.y); c.scale(b.radius/52f, b.radius/52f);
        switch (b.action) {
            case PAUSE:
                line(c, WHITE, 7, -15, -15, 15, 15); line(c, WHITE, 7, 15, -15, -15, 15); break;
            case CHAT:
                paint.setColor(WHITE); c.drawRoundRect(-27, -22, 27, 17, 7, 7, paint);
                triangle(c, 3, 14, 17, 14, 16, 25, WHITE);
                paint.setColor(0xff4c5964); c.drawCircle(-13, -2, 3, paint); c.drawCircle(0, -2, 3, paint); c.drawCircle(13, -2, 3, paint); break;
            case FIRE: bullet(c); break;
            case AIM: drawCrosshair(c, 0, 0, 1.8f); break;
            case RELOAD: reload(c); break;
            case NEXT_TOOL: gun(c); break;
            case GRENADE: grenade(c); break;
            case SPRINT: running(c); break;
            case CROUCH: crouching(c); break;
        }
        c.restore();
    }
    private void triangle(Canvas c, float x1,float y1,float x2,float y2,float x3,float y3,int color) {
        path.reset(); path.moveTo(x1,y1); path.lineTo(x2,y2); path.lineTo(x3,y3); path.close();
        paint.setStyle(Paint.Style.FILL); paint.setColor(color); c.drawPath(path, paint);
    }
    private void drawCrosshair(Canvas c, float x, float y, float s) {
        line(c, WHITE, 5*s, x-24*s,y,x-9*s,y); line(c, WHITE, 5*s, x+9*s,y,x+24*s,y);
        line(c, WHITE, 5*s, x,y-24*s,x,y-9*s); line(c, WHITE, 5*s, x,y+9*s,x,y+24*s);
        if (s > 1) { paint.setColor(WHITE); c.drawCircle(x, y, 3*s, paint); }
    }
    private void bullet(Canvas c) {
        c.save(); c.rotate(42); paint.setColor(WHITE); paint.setStyle(Paint.Style.FILL);
        path.reset(); path.moveTo(0,-42); path.cubicTo(-9,-30,-12,-20,-12,-12);
        path.lineTo(-12,37); path.lineTo(12,37); path.lineTo(12,-12);
        path.cubicTo(12,-20,9,-30,0,-42); path.close(); c.drawPath(path,paint);
        fill(c, 0xff46514a, -7, 25, 7, 31); fill(c, 0xff46514a, -5, -2, 5, 19);
        c.restore();
    }
    private void gun(Canvas c) {
        paint.setColor(WHITE); paint.setStrokeWidth(5); paint.setStyle(Paint.Style.STROKE);
        c.drawLine(-47,0,33,0,paint); c.drawLine(-43,-4,-43,4,paint);
        paint.setStyle(Paint.Style.FILL); c.drawRect(-7,-5,33,5,paint);
        triangle(c, 32,-5, 50,1, 49,8, WHITE);
        triangle(c, 3,4, 16,4, 18,17, WHITE);
        line(c, WHITE, 2, -4, 6, 22, 17);
    }
    private void reload(Canvas c) {
        paint.setColor(WHITE); paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(7);
        c.drawArc(-29,-29,29,29,35,265,false,paint); c.drawArc(-29,-29,29,29,215,85,false,paint);
        paint.setStyle(Paint.Style.FILL);
        triangle(c, -32,-34, -31,-10, -10,-24, WHITE);
        triangle(c, 32,34, 31,10, 10,24, WHITE);
    }
    private void grenade(Canvas c) {
        paint.setColor(WHITE); c.drawOval(-24,-12,24,30,paint); c.drawRect(-11,-22,10,-12,paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(4); c.drawCircle(12,-25,7,paint);
        c.drawArc(-25,-8,25,27,5,175,false,paint); paint.setStyle(Paint.Style.FILL);
    }
    private void running(Canvas c) {
        paint.setColor(WHITE); c.drawCircle(0,-31,9,paint);
        line(c,WHITE,12,-3,-20,10,3); line(c,WHITE,10,1,-13,22,-5);
        line(c,WHITE,10,-3,-12,-20,0); line(c,WHITE,12,8,2,-7,19);
        line(c,WHITE,11,-7,19,-21,28); line(c,WHITE,11,8,2,21,20); line(c,WHITE,11,21,20,31,23);
    }
    private void crouching(Canvas c) {
        paint.setColor(WHITE); c.drawCircle(5,-28,9,paint); c.drawRect(-5,-18,17,8,paint);
        line(c,WHITE,10,7,-12,-16,-3); line(c,WHITE,9,-16,-3,-28,-3);
        line(c,WHITE,11,6,5,-12,16); line(c,WHITE,11,-12,16,5,25);
        line(c,WHITE,11,5,25,23,25); line(c,WHITE,4,-35,-8,-5,-8);
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        if (scale <= 0) return false;
        int action = event.getActionMasked(), index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                final int pointerId = event.getPointerId(index);
                final long startedAt = event.getEventTime();
                controller.down(pointerId, event.getX(index)/scale, event.getY(index)/scale, startedAt);
                // Only the same still-held tool-panel or palette pointer acts.
                postDelayed(() -> {
                    if (controller.longPressTool(pointerId, startedAt) ||
                            controller.longPressPalette(pointerId, startedAt)) invalidate();
                }, 600);
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i=0; i<event.getPointerCount(); i++)
                    controller.move(event.getPointerId(i), event.getX(i)/scale, event.getY(i)/scale);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                controller.move(event.getPointerId(index), event.getX(index)/scale, event.getY(index)/scale);
                controller.up(event.getPointerId(index), event.getEventTime()); break;
            case MotionEvent.ACTION_CANCEL: controller.cancel(); break;
            default: return false;
        }
        invalidate();
        return true;
    }
    @Override protected void onDetachedFromWindow() { controller.cancel(); super.onDetachedFromWindow(); }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (!focus) { controller.cancel(); invalidate(); }
    }
}
