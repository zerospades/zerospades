package io.github.zerospades.mobile.hud;

import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;

/** Crisp, consistently weighted vector artwork, drawn in a 104-unit square. */
final class HudIcons {
    private static final int ICON = Color.rgb(245, 249, 246);
    private HudIcons() { }

    static void drawAction(Canvas c, Paint p, Path path, TouchController.Action action) {
        p.setStyle(Paint.Style.STROKE);
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setStrokeJoin(Paint.Join.ROUND);
        // A dark under-stroke preserves the icons on bright scenery.
        for (int pass = 0; pass < 2; pass++) {
            p.setColor(pass == 0 ? 0x99080f13 : ICON);
            p.setStrokeWidth(pass == 0 ? 9 : 5);
            actionShape(c, p, path, action);
        }
        p.setStyle(Paint.Style.FILL);
        p.setStrokeCap(Paint.Cap.BUTT);
    }

    private static void actionShape(Canvas c, Paint p, Path path, TouchController.Action action) {
        path.reset();
        switch (action) {
            case PAUSE:
                c.drawLine(-17, -17, 17, 17, p); c.drawLine(17, -17, -17, 17, p); break;
            case CHAT:
                path.moveTo(-25, -20); path.lineTo(25, -20); path.quadTo(29, -20, 29, -15);
                path.lineTo(29, 12); path.quadTo(29, 17, 24, 17);
                path.lineTo(6, 17); path.lineTo(-6, 26); path.lineTo(-5, 17);
                path.lineTo(-25, 17); path.quadTo(-29, 17, -29, 12);
                path.lineTo(-29, -15); path.quadTo(-29, -20, -25, -20); c.drawPath(path, p);
                c.drawLine(-15, -4, 15, -4, p); c.drawLine(-15, 6, 5, 6, p); break;
            case FIRE:
                c.save(); c.rotate(42);
                path.moveTo(0, -37); path.cubicTo(-10, -26, -13, -18, -13, -9);
                path.lineTo(-13, 28); path.quadTo(-13, 34, -8, 34);
                path.lineTo(8, 34); path.quadTo(13, 34, 13, 28);
                path.lineTo(13, -9); path.cubicTo(13, -18, 10, -26, 0, -37); path.close();
                c.drawPath(path, p); c.drawLine(-12, 19, 12, 19, p);
                c.drawLine(-12, -9, 12, -9, p); c.restore(); break;
            case AIM:
                c.drawCircle(0, 0, 24, p);
                c.drawLine(-37, 0, -15, 0, p); c.drawLine(15, 0, 37, 0, p);
                c.drawLine(0, -37, 0, -15, p); c.drawLine(0, 15, 0, 37, p);
                c.drawCircle(0, 0, 2, p); break;
            case RELOAD:
                c.drawArc(-28, -28, 28, 28, -110, 265, false, p);
                path.moveTo(-23, -32); path.lineTo(-12, -25); path.lineTo(-24, -18); c.drawPath(path, p);
                c.drawLine(-11, 0, 10, 0, p); c.drawLine(0, -10, 10, 0, p); c.drawLine(0, 10, 10, 0, p); break;
            case GRENADE:
                c.drawRoundRect(-20, -12, 20, 30, 12, 12, p);
                c.drawLine(-9, -13, -9, -22, p); c.drawLine(9, -13, 9, -22, p);
                c.drawLine(-9, -22, 9, -22, p); c.drawCircle(14, -28, 6, p);
                c.drawLine(-20, 2, 20, 2, p); c.drawLine(-20, 16, 20, 16, p);
                c.drawLine(0, -9, 0, 27, p); break;
            case SPRINT:
                c.drawCircle(7, -29, 7, p);
                path.moveTo(4, -19); path.lineTo(-7, -4); path.lineTo(8, 4);
                path.lineTo(17, 24); path.lineTo(28, 28); c.drawPath(path, p);
                path.reset(); path.moveTo(-7, -4); path.lineTo(-16, 15); path.lineTo(-29, 22);
                path.moveTo(-1, -14); path.lineTo(15, -8); path.lineTo(25, -15);
                path.moveTo(-4, -15); path.lineTo(-17, -8); path.lineTo(-25, -10); c.drawPath(path, p); break;
            case CROUCH:
                c.drawCircle(10, -27, 7, p);
                path.moveTo(4, -18); path.lineTo(-8, -5); path.lineTo(3, 5);
                path.lineTo(-13, 16); path.lineTo(3, 25); path.lineTo(25, 25);
                path.moveTo(4, -14); path.lineTo(-8, -8); path.lineTo(-24, -8);
                path.moveTo(-28, -3); path.lineTo(-9, -3); c.drawPath(path, p); break;
            case JUMP:
                path.moveTo(-19, -8); path.lineTo(0, -27); path.lineTo(19, -8);
                path.moveTo(0, -26); path.lineTo(0, 10); c.drawPath(path, p);
                c.drawArc(-29, 3, 29, 29, 15, 150, false, p);
                c.drawLine(-14, 30, 14, 30, p); break;
            default: break;
        }
    }

    /** Draws the currently selected tool as a clean profile in a wide 100x60 box. */
    static void drawEquipment(Canvas c, Paint p, Path path, HudState.Tool tool, HudState.Weapon weapon) {
        p.setColor(ICON); p.setStyle(Paint.Style.STROKE); p.setStrokeWidth(4);
        p.setStrokeCap(Paint.Cap.ROUND); p.setStrokeJoin(Paint.Join.ROUND); path.reset();
        if (tool == HudState.Tool.BLOCK) {
            path.moveTo(0, -26); path.lineTo(27, -14); path.lineTo(27, 15);
            path.lineTo(0, 27); path.lineTo(-27, 15); path.lineTo(-27, -14); path.close();
            path.moveTo(-27, -14); path.lineTo(0, -1); path.lineTo(27, -14);
            path.moveTo(0, -1); path.lineTo(0, 27); c.drawPath(path, p);
        } else if (tool == HudState.Tool.SPADE) {
            c.save(); c.rotate(40);
            c.drawLine(0, -29, 0, 10, p); c.drawRoundRect(-10, -35, 10, -27, 3, 3, p);
            path.moveTo(-14, 9); path.lineTo(14, 9); path.lineTo(12, 22);
            path.quadTo(0, 37, -12, 22); path.close(); c.drawPath(path, p); c.restore();
        } else if (tool == HudState.Tool.GRENADE) {
            actionShape(c, p, path, TouchController.Action.GRENADE);
        } else {
            // Three distinctive profiles: long rifle, compact SMG and short shotgun.
            float muzzle = weapon == HudState.Weapon.RIFLE ? -48 : weapon == HudState.Weapon.SMG ? -36 : -45;
            float stock = weapon == HudState.Weapon.SMG ? 42 : 48;
            path.moveTo(muzzle, -5); path.lineTo(-14, -5); path.lineTo(-10, -11);
            path.lineTo(17, -11); path.lineTo(22, -7); path.lineTo(stock, -7);
            path.lineTo(stock, 4); path.lineTo(22, 4); path.lineTo(15, 9);
            path.lineTo(9, 9); path.lineTo(5, 24); path.lineTo(-4, 23);
            path.lineTo(-1, 8); path.lineTo(-16, 8); path.lineTo(-19, 4);
            path.lineTo(muzzle, 4); path.close(); c.drawPath(path, p);
            c.drawLine(-8, -15, 8, -15, p);
            if (weapon == HudState.Weapon.SMG) {
                c.drawLine(-8, 9, -12, 24, p); c.drawLine(-12, 24, -3, 25, p);
            } else if (weapon == HudState.Weapon.SHOTGUN) {
                c.drawLine(-40, 9, -19, 9, p);
            } else {
                c.drawLine(-29, -9, -15, -9, p);
            }
        }
        p.setStyle(Paint.Style.FILL); p.setStrokeCap(Paint.Cap.BUTT);
    }
}
