package io.github.zerospades.mobile.hud;

import org.junit.Test;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import static org.junit.Assert.*;

public class HudStateTest {
    @Test public void scoreBarsUseTheSharedModeMaximum() {
        HudState state = new HudState(20, 40, 100, 100, 100, HudState.Tool.WEAPON,
                5, 20, 50, 3, 60, -1, null, .5f, .5f, new ArrayList<String>());
        assertEquals(.2f, state.scoreFraction(state.teamOneScore), .0001f);
        assertEquals(.4f, state.scoreFraction(state.teamTwoScore), .0001f);
        assertEquals(1f, state.scoreFraction(150), .0001f);
        assertEquals(0f, state.scoreFraction(-1), .0001f);
        HudState unknown = new HudState(20, 40, 0, 100, 100, HudState.Tool.WEAPON,
                5, 20, 50, 3, 60, -1, null, .5f, .5f, new ArrayList<String>());
        assertEquals(0f, unknown.scoreFraction(40), .0001f);
    }
    @Test public void killFeedContainsOnlyLastFiveAndIsSnapshot() {
        List<String> input = new ArrayList<>(Arrays.asList("1", "2", "3", "4", "5", "6", "7"));
        HudState state = new HudState(2, 3, 10, 100, 100, HudState.Tool.GRENADE,
                4, 20, 47, 3, 60, -1, null, .5f, .5f, input);
        input.clear();
        assertEquals(Arrays.asList("3", "4", "5", "6", "7"), state.killFeed);
        assertEquals(3, state.grenades);
        assertEquals(47, state.blocks);
    }
}
