package io.github.zerospades.mobile.hud;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.os.SystemClock;
import android.text.TextPaint;
import android.text.TextUtils;
import android.view.MotionEvent;
import android.view.View;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.Locale;

/** Overlay only; the native game must supply state and handle input through Sink. */
public final class TouchHudView extends View {
    private static final int WHITE = Color.rgb(245, 245, 242);
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG | Paint.FILTER_BITMAP_FLAG);
    private final Path path = new Path();
    private final RectF mapRect = new RectF();
    private final TextPaint feedPaint = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    private long timerSeconds = Long.MIN_VALUE;
    private String timerText = "--:--";
    private final TouchController controller;
    private HudLayout layout = new HudLayout(1568);
    private final Map<String, HudLayout.Placement> placements;
    private boolean editing;
    private String selected;
    private float dragX, dragY;
    public interface SelectionListener { void onSelection(String id, float size); }
    public interface LayoutListener { void onMapLayout(float x, float y, float size); }
    private SelectionListener selectionListener;
    private LayoutListener layoutListener;
    private HudState state = new HudState(0, 0, 0, 0, 0, HudState.Tool.WEAPON, 0, 0, 0, 0, -1,
            -1, null, .5f, .5f, Collections.<String>emptyList());
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
        placements = HudLayoutStore.load(context);
        layout = new HudLayout(1568, placements);
        controller = new TouchController(layout, sink);
        setFocusable(true);
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
        rebuildLayout(w / scale);
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (getHeight() <= 0) return;
        canvas.save();
        canvas.scale(scale, scale);
        if (state.map != null || showPlaceholderMap || decorateNativeMap) {
            canvas.save(); canvas.translate(layout.map.x, layout.map.y); canvas.scale(layout.map.size, layout.map.size);
            canvas.translate(-138, -138); drawMap(canvas); canvas.restore();
        }
        canvas.save(); canvas.translate(layout.killFeed.x, layout.killFeed.y); canvas.scale(layout.killFeed.size, layout.killFeed.size);
        canvas.translate(-40, -270); drawKillFeed(canvas); canvas.restore();
        canvas.save(); canvas.translate(layout.scores.x, layout.scores.y); canvas.scale(layout.scores.size, layout.scores.size);
        canvas.translate(-layout.width/2, 0); drawScores(canvas); canvas.restore();
        canvas.save(); canvas.translate(layout.timer.x, layout.timer.y); canvas.scale(layout.timer.size, layout.timer.size);
        drawTimer(canvas); canvas.restore();
        HudLayout.Button tool = null;
        for (HudLayout.Button b : layout.buttons) if (b.action == TouchController.Action.NEXT_TOOL) tool = b;
        if (tool != null) {
            canvas.save(); canvas.translate(tool.x, tool.y); canvas.scale(tool.size, tool.size);
            canvas.translate(-(layout.width-153), -70); drawAmmo(canvas); canvas.restore();
        }
        // The real weapon skin supplies crosshair/scope visuals in the game.
        if (!decorateNativeMap) drawCrosshair(canvas, layout.width / 2, 488, 1);
        for (HudLayout.Button button : layout.buttons) drawButton(canvas, button);
        drawStick(canvas);
        if (state.fps >= 0) text(canvas, Integer.toString(state.fps), 68, 962, 22, WHITE, Paint.Align.LEFT);
        if (editing && selected != null) {
            HudLayout.Widget w = layout.find(selected);
            if (w != null) {
                paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(3); paint.setColor(0xffffdb64);
                canvas.drawCircle(w.x, w.y, 55*w.size, paint); paint.setStyle(Paint.Style.FILL);
            }
        }
        canvas.restore();
        if (!decorateNativeMap && state.joinedAtMillis >= 0) postInvalidateDelayed(250);
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
        paint.setColor(0x5510181b); c.drawCircle(x, y + 3, radius + 3, paint);
        paint.setColor(active ? 0xaa41666b : 0x8820272b); c.drawCircle(x, y, radius, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2.5f);
        paint.setColor(active ? 0xffa3e7dd : 0xbbf2f8f4); c.drawCircle(x, y, radius - 1.5f, paint);
        paint.setStrokeWidth(1); paint.setColor(0x557f9698);
        c.drawCircle(x, y, radius - 7, paint); paint.setStyle(Paint.Style.FILL);
    }
    private void drawMap(Canvas c) {
        float x = 138, y = 138, r = 99;
        if (!decorateNativeMap) {
            paint.setStyle(Paint.Style.FILL); paint.setColor(0x99323e52); c.drawCircle(x, y, r, paint);
        }
        c.save(); path.reset(); path.addCircle(x, y, r - 5, Path.Direction.CW); c.clipPath(path);
        Bitmap map = state.map;
        if (map != null && !map.isRecycled()) {
            mapRect.set(x-r, y-r, x+r, y+r);
            paint.setColor(Color.WHITE); c.drawBitmap(map, null, mapRect, paint);
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
        feedPaint.setTextSize(22);
        for (int i = first; i < state.killFeed.size(); i++) {
            String entry = state.killFeed.get(i);
            if (entry == null) continue;
            String clipped = TextUtils.ellipsize(entry, feedPaint,
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
    }
    private void drawTimer(Canvas c) {
        long seconds = state.joinedAtMillis < 0 ? -1 :
                Math.max(0, SystemClock.elapsedRealtime() - state.joinedAtMillis) / 1000;
        if (seconds != timerSeconds) {
            timerSeconds = seconds;
            timerText = seconds < 0 ? "--:--" :
                    String.format(Locale.ROOT, "%02d:%02d", seconds / 60, seconds % 60);
        }
        text(c, timerText, 0, 0, 30, WHITE, Paint.Align.CENTER);
    }
    private void drawAmmo(Canvas c) {
        float right = layout.width - 20;
        if (controller.pressed(TouchController.Action.NEXT_TOOL))
            fill(c, 0x447f8e88, right-265, 17, right, 123);
        String toolName = state.tool == HudState.Tool.WEAPON ? state.weapon.name() : state.tool.name();
        text(c, toolName, right-160, 72, 24, WHITE, Paint.Align.CENTER);
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
        float knob = layout.stickRadius * (49f/105f);
        c.drawCircle(x, y, knob, paint);
        paint.setStyle(Paint.Style.STROKE); paint.setStrokeWidth(2); paint.setColor(0xcce6e9e2);
        c.drawCircle(x, y, knob, paint); paint.setStyle(Paint.Style.FILL);
        text(c, "MOVE", x, y + knob*.18f, Math.max(12, knob*.32f), WHITE, Paint.Align.CENTER);
    }
    private void drawButton(Canvas c, HudLayout.Button b) {
        if (b.action == TouchController.Action.NEXT_TOOL) return; // tool panel is the button
        ring(c, b.x, b.y, b.radius, controller.pressed(b.action));
        String label;
        switch (b.action) {
            case PAUSE: label = "PAUSE"; break;
            case CHAT: label = "CHAT"; break;
            case FIRE: label = "FIRE"; break;
            case AIM: label = "AIM"; break;
            case RELOAD: label = "RELOAD"; break;
            case GRENADE: label = "GRENADE"; break;
            case SPRINT: label = "SPRINT"; break;
            case CROUCH: label = "CROUCH"; break;
            case JUMP: label = "JUMP"; break;
            case PIE_MENU: label = "PIE"; break;
            case FLASHLIGHT: label = "LIGHT"; break;
            default: label = b.action.name();
        }
        float font = Math.min(b.radius*.55f, (b.radius*1.55f)/(label.length()*.60f));
        text(c, label, b.x, b.y+font*.35f, font, WHITE, Paint.Align.CENTER);
    }
    private void drawCrosshair(Canvas c, float x, float y, float s) {
        line(c, WHITE, 5*s, x-24*s,y,x-9*s,y); line(c, WHITE, 5*s, x+9*s,y,x+24*s,y);
        line(c, WHITE, 5*s, x,y-24*s,x,y-9*s); line(c, WHITE, 5*s, x,y+9*s,x,y+24*s);
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        if (scale <= 0) return false;
        if (editing) return editTouch(event);
        int action = event.getActionMasked(), index = event.getActionIndex();
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                final int pointerId = event.getPointerId(index);
                final long startedAt = event.getEventTime();
                controller.down(pointerId, event.getX(index)/scale, event.getY(index)/scale, startedAt);
                // Avoid scheduling timers for ordinary movement and action touches.
                if (controller.isLongPressCandidate(pointerId)) postDelayed(() -> {
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
    public boolean isPieHeld() { return controller.pressed(TouchController.Action.PIE_MENU); }
    public void setEditing(boolean value, SelectionListener listener) {
        controller.cancel(); editing=value; selectionListener=listener; invalidate();
    }
    public void setLayoutListener(LayoutListener listener) { layoutListener=listener; notifyMap(); }
    private void notifyMap() {
        if (layoutListener != null) layoutListener.onMapLayout(layout.map.x/layout.width,
                layout.map.y/HudLayout.HEIGHT, layout.map.size);
    }
    private void rebuildLayout(float width) {
        layout = new HudLayout(width, placements);
        controller.setLayout(layout);
        notifyMap(); invalidate();
    }
    public Map<String, HudLayout.Placement> getPlacements() { return new HashMap<>(placements); }
    public void resetLayout() {
        placements.clear(); selected=null; rebuildLayout(layout.width);
        if (selectionListener != null) selectionListener.onSelection(null, 1);
    }
    public void resizeSelected(float size) {
        if (selected == null) return;
        HudLayout.Widget w=layout.find(selected);
        if (w == null) return;
        placements.put(selected, new HudLayout.Placement(w.x/layout.width, w.y/HudLayout.HEIGHT,
                Math.max(.5f, Math.min(2f,size))));
        rebuildLayout(layout.width);
    }
    private boolean editTouch(MotionEvent event) {
        float x=event.getX()/scale, y=event.getY()/scale;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                selected=findEditable(x,y);
                if (selected == null) return true;
                HudLayout.Widget w=layout.find(selected);
                dragX=x-w.x; dragY=y-w.y;
                if (selectionListener != null) selectionListener.onSelection(selected,w.size);
                invalidate(); return true;
            case MotionEvent.ACTION_MOVE:
                if (selected != null) {
                    HudLayout.Widget current=layout.find(selected);
                    float px=Math.max(0,Math.min(1,(x-dragX)/layout.width));
                    float py=Math.max(0,Math.min(1,(y-dragY)/HudLayout.HEIGHT));
                    placements.put(selected,new HudLayout.Placement(px,py,current.size));
                    rebuildLayout(layout.width);
                }
                return true;
            case MotionEvent.ACTION_UP: case MotionEvent.ACTION_CANCEL: return true;
            default: return true;
        }
    }
    private String findEditable(float x, float y) {
        String best=null; float distance=Float.MAX_VALUE;
        for (HudLayout.Button b : layout.buttons) {
            float rx=b.radius > 0 ? b.radius : b.halfWidth;
            float ry=b.radius > 0 ? b.radius : b.halfHeight;
            float dx=(x-b.x)/rx, dy=(y-b.y)/ry;
            float d=dx*dx+dy*dy;
            if (d<=1.2f && d<distance) { distance=d; best=b.id; }
        }
        float dx=(x-layout.stickX)/layout.stickRadius, dy=(y-layout.stickY)/layout.stickRadius;
        if (dx*dx+dy*dy<=1.2f && dx*dx+dy*dy<distance) { distance=dx*dx+dy*dy; best="stick"; }
        HudLayout.Widget[] widgets={layout.map,layout.timer,layout.killFeed,layout.scores};
        float[] rx={99,80,220,350}, ry={99,28,80,42};
        for (int i=0;i<widgets.length;i++) {
            HudLayout.Widget w=widgets[i];
            float cx=w.x+(i==2 ? 180*w.size : 0), cy=w.y+(i==2 ? 55*w.size : i==3 ? 35*w.size : 0);
            dx=(x-cx)/(rx[i]*w.size); dy=(y-cy)/(ry[i]*w.size);
            float d=dx*dx+dy*dy;
            if (d<=1 && d<distance) { distance=d; best=w.id; }
        }
        return best;
    }
    @Override protected void onDetachedFromWindow() { controller.cancel(); super.onDetachedFromWindow(); }
    @Override public void onWindowFocusChanged(boolean focus) {
        super.onWindowFocusChanged(focus);
        if (!focus) { controller.cancel(); invalidate(); }
    }
}
