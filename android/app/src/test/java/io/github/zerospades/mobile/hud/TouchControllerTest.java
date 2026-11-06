package io.github.zerospades.mobile.hud;

import org.junit.Test;
import java.util.ArrayList;
import java.util.List;
import static org.junit.Assert.*;

public class TouchControllerTest {
    private static class Recorder implements TouchController.Sink {
        final List<String> buttons = new ArrayList<>();
        float x, y, lookX, lookY, paletteX, paletteY;
        int moves, paletteSelections;
        @Override public void move(float x, float y) { this.x = x; this.y = y; moves++; }
        @Override public void look(float x, float y) { lookX += x; lookY += y; }
        @Override public void palette(float x, float y) { paletteX = x; paletteY = y; paletteSelections++; }
        @Override public void button(TouchController.Action a, boolean p) { buttons.add(a + ":" + p); }
    }
    @Test public void movementIsClampedAndReleasedOnCancel() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 284, 832); c.move(1, 494, 832);
        assertEquals(1, r.x, .001); assertEquals(0, r.y, .001);
        c.cancel(); assertEquals(0, r.x, .001); assertEquals(0, r.y, .001);
        assertEquals(0, c.stickX(), .001);
    }
    @Test public void twoFireButtonsAreReferenceCounted() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 137, 608); c.down(2, 1274, 725);
        assertEquals(1, r.buttons.size()); assertTrue(c.pressed(TouchController.Action.FIRE));
        c.up(1); assertEquals(1, r.buttons.size());
        c.up(2); assertEquals("FIRE:false", r.buttons.get(1));
        assertFalse(c.pressed(TouchController.Action.FIRE));
    }
    @Test public void simultaneousStickAndLookAndActionsKeepTheirOwners() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 284, 832); c.down(2, 800, 500); c.down(3, 1450, 627);
        c.move(1, 284, 727); c.move(2, 820, 490); c.move(3, 1300, 400);
        assertEquals(-1, r.y, .001); assertEquals(20, r.lookX, .001); assertEquals(-10, r.lookY, .001);
        assertTrue(c.pressed(TouchController.Action.AIM));
        c.cancel(); assertEquals("AIM:false", r.buttons.get(1)); assertEquals(0, r.y, .001);
    }
    @Test public void extraLookFingerDoesNotStealCamera() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 800, 500); c.down(2, 900, 500); c.move(2, 1000, 500);
        assertEquals(0, r.lookX, .001); c.move(1, 810, 500); assertEquals(10, r.lookX, .001);
        c.up(1); c.move(2, 1010, 500); assertEquals(10, r.lookX, .001);
    }
    @Test public void layoutChangeReleasesHeldButtons() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 137, 608); c.setLayout(new HudLayout(1800));
        assertEquals("FIRE:false", r.buttons.get(1));
    }
    @Test public void stickDeadZoneAndDiagonalClamp() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 284, 832); c.move(1, 288, 832);
        assertEquals(0, r.x, .001);
        c.move(1, 494, 1042);
        assertEquals(1, Math.sqrt(r.x*r.x + r.y*r.y), .001);
        c.up(1); assertEquals(0, r.x, .001); assertEquals(0, r.y, .001);
    }
    @Test public void resizedButtonsStayOnRightEdge() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(2000), r);
        c.down(8, 2000-68, 783);
        assertEquals("RELOAD:true", r.buttons.get(0));
        c.up(8); assertEquals("RELOAD:false", r.buttons.get(1));
    }
    @Test public void doubleTapStickJumpsWithoutAddingAButton() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 284, 832, 1000); c.up(1, 1080);
        c.down(2, 284, 832, 1200);
        assertEquals("JUMP:true", r.buttons.get(0));
        assertEquals("JUMP:false", r.buttons.get(1));
        c.move(2, 284, 727); assertEquals(-1, r.y, .001);
        c.up(2, 1300);
        c.down(3, 284, 832, 1400); c.up(3, 1450);
        assertEquals(2, r.buttons.size()); // a double tap isn't reused as the first of a triple
    }
    @Test public void dragLongTapTimeoutAndCancelDoNotJump() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 284, 832, 1000); c.move(1, 340, 832); c.up(1, 1050);
        c.down(2, 284, 832, 1100); c.up(2, 1400); // held too long
        c.down(3, 284, 832, 1450); c.up(3, 1500);
        c.down(4, 284, 832, 1900); c.up(4, 1950); // outside double-tap interval
        c.cancel();
        c.down(5, 284, 832, 2000); c.up(5, 2050); // cancel cleared tap history
        assertTrue(r.buttons.isEmpty());
    }
    @Test public void blockPaletteOwnsTouchesOnlyWhileEnabled() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 300, 430); c.move(1, 310, 440); c.up(1);
        assertEquals(10, r.lookX, .001); assertEquals(0, r.paletteSelections);
        c.setBlockPaletteEnabled(true);
        c.down(2, 250, 390);
        assertEquals(1, r.paletteSelections); assertEquals(0, r.paletteX, .001);
        c.move(2, 332.5f, 472.5f);
        assertEquals(.5f, r.paletteX, .001); assertEquals(.5f, r.paletteY, .001);
        c.move(2, 416, 556); // outside; does not turn the camera or send out-of-range points
        assertEquals(2, r.paletteSelections); assertEquals(10, r.lookX, .001);
        c.up(2);
        c.setBlockPaletteEnabled(false);
        c.down(3, 300, 430); c.move(3, 305, 430); c.up(3);
        assertEquals(15, r.lookX, .001);
    }
    @Test public void paletteDoesNotStealOtherPointers() {
        Recorder r = new Recorder(); HudLayout l = new HudLayout(1568);
        TouchController c = new TouchController(l, r); c.setBlockPaletteEnabled(true);
        c.down(1, l.stickX, l.stickY); c.move(1, l.stickX, l.stickY - l.stickRadius);
        c.down(2, 300, 430); c.move(2, 310, 440);
        assertEquals(-1, r.y, .001); assertEquals(2, r.paletteSelections);
        c.up(2); assertEquals(-1, r.y, .001);
        c.up(1); assertEquals(0, r.y, .001);
    }
    @Test public void holdingPaletteGrabsAimedColorWithoutSelectingAgainOnRelease() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.setBlockPaletteEnabled(true);
        c.down(1, 300, 430, 1000);
        assertFalse(c.longPressPalette(1, 999));
        assertTrue(c.longPressPalette(1, 1000));
        assertEquals("CAPTURE_COLOR:true", r.buttons.get(0));
        assertEquals("CAPTURE_COLOR:false", r.buttons.get(1));
        c.move(1, 300, 430); c.up(1, 1700);
        assertEquals(1, r.paletteSelections);
        c.down(2, 300, 430, 2000); c.move(2, 330, 430);
        assertFalse(c.longPressPalette(2, 2000));
        c.cancel(); assertFalse(c.longPressPalette(2, 2000));
    }
    @Test public void holdToolPanelOpensLoadoutWithoutCycling() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 1450, 70, 1000);
        assertTrue(r.buttons.isEmpty());
        assertFalse(c.longPressTool(1, 999));
        assertTrue(c.longPressTool(1, 1000));
        assertFalse(c.longPressTool(1, 1000));
        c.up(1, 1700);
        assertEquals(2, r.buttons.size());
        assertEquals("LOADOUT:true", r.buttons.get(0));
        assertEquals("LOADOUT:false", r.buttons.get(1));
        c.down(2, 1450, 70, 2000); c.cancel();
        assertEquals(2, r.buttons.size());
        assertFalse(c.longPressTool(2, 2000));
    }
    @Test public void sharedToolPointersProduceOneTapOrHold() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 1450, 70, 1000); c.down(2, 1460, 70, 1010);
        c.up(1, 1050); assertTrue(r.buttons.isEmpty()); c.up(2, 1060);
        assertEquals("NEXT_TOOL:true", r.buttons.get(0));
        assertEquals("NEXT_TOOL:false", r.buttons.get(1));
        c.down(3, 1450, 70, 2000); c.down(4, 1460, 70, 2010);
        assertTrue(c.longPressTool(3, 2000));
        c.up(3, 2700); c.up(4, 2700);
        assertEquals(4, r.buttons.size());
        assertEquals("LOADOUT:true", r.buttons.get(2));
        assertEquals("LOADOUT:false", r.buttons.get(3));
    }
    @Test public void toolPanelIsClickableAndRemovedBottomButtonIsNot() {
        Recorder r = new Recorder(); TouchController c = new TouchController(new HudLayout(1568), r);
        c.down(1, 1450, 70); c.up(1);
        assertEquals("NEXT_TOOL:true", r.buttons.get(0));
        assertEquals("NEXT_TOOL:false", r.buttons.get(1));
        c.down(2, 1079, 765); c.up(2);
        assertEquals(2, r.buttons.size());
    }
}
