package io.github.zerospades.mobile.hud;

import android.content.Context;
import android.content.SharedPreferences;
import java.util.HashMap;
import java.util.Map;

/** Private persistent HUD positions shared by the game and launcher editor. */
public final class HudLayoutStore {
    private static final String FILE = "hud_layout";
    private HudLayoutStore() { }
    public static Map<String, HudLayout.Placement> load(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(FILE, Context.MODE_PRIVATE);
        Map<String, HudLayout.Placement> result = new HashMap<>();
        for (String id : HudLayout.EDITABLE_IDS) {
            try {
                if (!prefs.contains(id+".x")) continue;
                HudLayout.Placement p = new HudLayout.Placement(prefs.getFloat(id+".x", -1),
                        prefs.getFloat(id+".y", -1), prefs.getFloat(id+".size", 1));
                if (p.valid()) result.put(id, p);
            } catch (ClassCastException ignored) { /* Ignore corrupt or legacy values. */ }
        }
        return result;
    }
    public static void save(Context context, Map<String, HudLayout.Placement> placements) {
        SharedPreferences.Editor edit=context.getSharedPreferences(FILE, Context.MODE_PRIVATE).edit().clear();
        for (String id : HudLayout.EDITABLE_IDS) {
            HudLayout.Placement p=placements.get(id);
            if (p == null || !p.valid()) continue;
            edit.putFloat(id+".x",p.x).putFloat(id+".y",p.y).putFloat(id+".size",p.size);
        }
        edit.apply();
    }
}
