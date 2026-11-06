package io.github.zerospades.mobile.hud;

import org.junit.Test;
import static org.junit.Assert.*;
import java.util.HashMap;
import java.util.Map;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.List;

public class MobileControlsTest {
    @Test public void savedLayoutScalesAndTargetsTheMovedButton() {
        Map<String,HudLayout.Placement> values=new HashMap<>();
        values.put("fire_right",new HudLayout.Placement(.5f,.5f,1.5f));
        values.put("stick",new HudLayout.Placement(.1f,.8f,.75f));
        values.put("map",new HudLayout.Placement(.12f,.2f,1.2f));
        HudLayout layout=new HudLayout(1800,values);
        assertEquals(900, layout.find("fire_right").x, .001);
        assertEquals(490, layout.find("fire_right").y, .001);
        assertEquals(1.5, layout.find("fire_right").size, .001);
        assertEquals(180, layout.stickX, .001);
        assertEquals(105*.75, layout.stickRadius, .001);
        assertEquals(216, layout.map.x, .001);
        final StringBuilder calls = new StringBuilder();
        TouchController controller=new TouchController(layout, new TouchController.Sink() {
            public void move(float x,float y) { }
            public void look(float x,float y) { }
            public void button(TouchController.Action a,boolean down) { calls.append(a).append(down); }
        });
        controller.down(1,900,490); controller.up(1);
        assertEquals("FIREtrueFIREfalse", calls.toString());
        assertEquals(1000, new HudLayout(2000,values).find("fire_right").x, .001);
        values.put("jump",new HudLayout.Placement(Float.NaN,.5f,1));
        assertEquals(1800-465, new HudLayout(1800,values).find("jump").x, .001);
    }
    @Test public void piePressDragReleaseAndCancel() {
        HudLayout layout=new HudLayout(1568); HudLayout.Widget pie=layout.find("pie");
        StringBuilder calls=new StringBuilder();
        TouchController c=new TouchController(layout,new TouchController.Sink() {
            public void move(float x,float y) { }
            public void look(float x,float y) { calls.append("look:").append((int)x).append(",").append((int)y).append(";"); }
            public void button(TouchController.Action a, boolean down) { calls.append(a).append(":").append(down).append(";"); }
            public void cancelPie() { calls.append("cancel;"); }
        });
        c.down(1,pie.x,pie.y); c.move(1,pie.x+20,pie.y-30); c.up(1);
        assertEquals("PIE_MENU:true;look:80,-120;PIE_MENU:false;",calls.toString());
        calls.setLength(0); c.down(2,pie.x,pie.y); c.cancel();
        assertEquals("PIE_MENU:true;cancel;PIE_MENU:false;",calls.toString());
    }
    @Test public void gyroUsesTimeAndLandscapeRotationAndResetsAfterLongGap() {
        GyroAim gyro=new GyroAim();
        assertEquals(null,gyro.sample(1_000_000_000L,1,2,0,1));
        float[] delta=gyro.sample(1_010_000_000L,1,2,0,1);
        assertEquals(-3.3333,delta[0],.001); assertEquals(6.6667,delta[1],.001);
        assertEquals(null,gyro.sample(2_000_000_000L,1,2,0,1));
        gyro.reset(); assertEquals(null,gyro.sample(3_000_000_000L,1,2,0,3));
        delta=gyro.sample(3_010_000_000L,1,2,0,3);
        assertEquals(3.3333,delta[0],.001); assertEquals(-6.6667,delta[1],.001);
        assertEquals(null,gyro.sample(0,Float.NaN,0,0,1));
    }
    @Test public void gyroSensitivityScalesOnlyGyroDeltasAndClampsInvalidValues() {
        GyroAim gyro = new GyroAim();
        gyro.sample(1_000_000_000L, 1, 2, 0, 1);
        float[] delta = gyro.sample(1_010_000_000L, 1, 2, 0, 1);
        float x = delta[0], y = delta[1];
        gyro.setSensitivity(2f);
        delta = gyro.sample(1_020_000_000L, 1, 2, 0, 1);
        assertEquals(x * 2, delta[0], .001); assertEquals(y * 2, delta[1], .001);
        gyro.setSensitivity(.5f);
        delta = gyro.sample(1_030_000_000L, 1, 2, 0, 1);
        assertEquals(x * .5, delta[0], .001); assertEquals(y * .5, delta[1], .001);
        assertEquals(.1, GyroAim.clampSensitivity(-2), .0001);
        assertEquals(5, GyroAim.clampSensitivity(100), .0001);
        assertEquals(1, GyroAim.clampSensitivity(Float.NaN), .0001);
    }
    @Test public void chatRecordsKeepUnicodeAndColorWithoutMarkupParsing() {
        byte[] prefix="[Team] ".getBytes(StandardCharsets.UTF_8);
        byte[] name="नाम😀".getBytes(StandardCharsets.UTF_8);
        byte[] suffix=": hi\nthere".getBytes(StandardCharsets.UTF_8);
        ByteBuffer b=ByteBuffer.allocate(16+prefix.length+name.length+suffix.length);
        b.putInt(0x123456).putInt(prefix.length).putInt(name.length).putInt(suffix.length);
        b.put(prefix).put(name).put(suffix);
        List<ChatMessage> messages=ChatMessage.decode(b.array());
        assertEquals(1,messages.size()); assertEquals("नाम😀",messages.get(0).name);
        assertEquals(0xff123456,messages.get(0).color); assertEquals(": hi\nthere",messages.get(0).suffix);
        byte[] invalid=ByteBuffer.allocate(16).putInt(0).putInt(-1).putInt(0).putInt(0).array();
        assertEquals(0,ChatMessage.decode(invalid).size());
    }
}
