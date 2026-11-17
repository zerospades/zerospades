/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

namespace spades {

    /**
     * What zombies' player models are made of, as any player skin says it:
     * a .2kv6 scene holding the parts as the player template has them
     * (see player-scene.sh), or "" for the 13 classic .kv6 parts
     * (Torso.kv6, Head.kv6...). A path is in the set's own folder of
     * models unless it starts with "Models/".
     *
     * Every set's script is the same function, built on its own: a skin
     * made for one set goes in any other's place as it is, its script and
     * its models copied there. A classic .kv6 part shipped without a
     * script still takes the place of the scene's, so skins made before
     * scenes keep working.
     */
    string PlayerModels() {
        return "Player.2kv6";
    }

}
