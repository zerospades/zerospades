package io.github.zerospades.mobile.hud;

import android.graphics.Bitmap;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** Immutable snapshot supplied on the UI thread by the future engine bridge. */
public final class HudState {
    public enum Tool { WEAPON, BLOCK, SPADE, GRENADE }
    public enum Weapon { RIFLE, SMG, SHOTGUN }
    public final int teamOneScore, teamTwoScore, scoreLimit, health, maxHealth;
    public final int ammo, reserveAmmo, blocks, grenades, fps;
    public final Tool tool;
    public final Weapon weapon;
    /** SystemClock.elapsedRealtime at the actual join; -1 means not connected. */
    public final long joinedAtMillis;
    /** Optional pre-rendered map, player coordinates normalized to 0..1. */
    public final Bitmap map;
    public final float playerX, playerY;
    /** Newest last. Entries should be complete, formatted kill messages. */
    public final List<String> killFeed;

    /** Both teams use the same game-mode maximum, not the leading team's score. */
    public float scoreFraction(int score) {
        if (scoreLimit <= 0 || score <= 0) return 0f;
        return Math.min(1f, (float) score / (float) scoreLimit);
    }

    public HudState(int teamOneScore, int teamTwoScore, int scoreLimit, int health, int maxHealth,
                    Tool tool, int ammo, int reserveAmmo, int blocks, int grenades, int fps,
                    long joinedAtMillis, Bitmap map, float playerX, float playerY,
                    List<String> killFeed) {
        this(teamOneScore, teamTwoScore, scoreLimit, health, maxHealth, tool, Weapon.RIFLE,
                ammo, reserveAmmo, blocks, grenades, fps, joinedAtMillis, map, playerX, playerY, killFeed);
    }

    public HudState(int teamOneScore, int teamTwoScore, int scoreLimit, int health, int maxHealth,
                    Tool tool, Weapon weapon, int ammo, int reserveAmmo, int blocks, int grenades, int fps,
                    long joinedAtMillis, Bitmap map, float playerX, float playerY, List<String> killFeed) {
        if (tool == null || weapon == null || killFeed == null)
            throw new IllegalArgumentException("tool, weapon and killFeed required");
        this.teamOneScore = teamOneScore;
        this.teamTwoScore = teamTwoScore;
        this.scoreLimit = scoreLimit;
        this.health = health;
        this.maxHealth = maxHealth;
        this.tool = tool;
        this.weapon = weapon;
        this.ammo = ammo;
        this.reserveAmmo = reserveAmmo;
        this.blocks = blocks;
        this.grenades = grenades;
        this.fps = fps;
        this.joinedAtMillis = joinedAtMillis;
        this.map = map;
        this.playerX = playerX;
        this.playerY = playerY;
        this.killFeed = Collections.unmodifiableList(
                new ArrayList<>(killFeed.subList(Math.max(0, killFeed.size() - 5), killFeed.size())));
    }
}
