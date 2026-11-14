#!/usr/bin/env python3
#
# Copyright (c) 2026 ZeroSpades developers.
#
# This file is part of ZeroSpades, a fork of OpenSpades.
#
# ZeroSpades is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# ZeroSpades is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

"""
Server-side sentry turret for piqueserver.

Builds a small destructible block turret that automatically tracks and
shoots enemy players in range. Damage is applied server-side and kill
credit goes to the player who placed the turret.

The turret is made of real map blocks: visible to every player, solid
(no walking through) and destructible by shooting, digging or nading
any of its blocks. When the ZeroSpades client from this repository is
used, it renders the animated kv6 turret models over these blocks and
hides them from the renderer (they stay solid and destructible),
telling the turret family and level apart by block color (green =
machine gun family, see SENTRY_LEVELS below; other color families
are reserved for future turret types).
Vanilla clients keep seeing and shooting the plain blocks.

Commands
^^^^^^^^

* ``/sentry`` (alias ``/turret``) - place a level 1 sentry on the
  block you are aiming at
* ``/sentry <1-3>`` - place a sentry of the given level (also
  accepted: ``/sentry1`` .. ``/sentry3`` and ``/turret1`` .. ``/turret3``)
* ``/sentryremove`` (alias ``/turretremove``) - remove all your sentries

Note: ZeroSpades client builds older than the sentry update intercept
``/sentry`` chat-side for their cosmetic local turret and never forward
it to the server. Use the ``/turret`` aliases to place a real
server-side sentry from such a client. With the sentry update,
``/sentry`` is forwarded to the server and the cosmetic local mode
lives behind ``/sentrylocal``.

Coordinates note: the map z axis points DOWN (z=0 is the sky), so
"above a block" means a SMALLER z.
"""

from math import atan2, degrees, sqrt

from twisted.internet import reactor
from piqueserver.commands import command
from pyspades.common import make_color
from pyspades.contained import BlockAction, SetColor
from pyspades.constants import BUILD_BLOCK, DESTROY_BLOCK, WEAPON_KILL

# ----------------------------------------------------------------------
# Tuning constants (edit here, then restart the server)
# ----------------------------------------------------------------------
SENTRY_SCRIPT_VERSION = "1.8"  # 2026-11-14

# per-level tuning: damage per shot, seconds between shots, block
# color. The colors double as the turret family and level markers the
# ZeroSpades client reads to pick the matching animated model. Color
# families are reserved per turret type; this script is the machine
# gun family (greens). Future types get their own family: red =
# flamethrower, yellow = grenade launcher, blue = electric. Encoding
# rules (keep in sync with kServerSentryColors in SentryEntity.cpp):
#   * a tier color is a pure family hue whose brightness encodes
#     the level (bright to dark: 190, 140, 80);
#   * no channel ever reaches 255 (the future red family especially:
#     r == 255 marks aim data, see make_aim_color) and no tier color
#     may equal the heartbeat colors (0, 255, 0) or (255, 0, 0).
SENTRY_LEVELS = {
    1: {"damage": 20, "interval": 0.5, "color": (0, 190, 0)},   # green, bright
    2: {"damage": 25, "interval": 0.4, "color": (0, 140, 0)},  # green, mid
    3: {"damage": 10, "interval": 0.15, "color": (0, 80, 0)},  # green, dark
}

SENTRY_RANGE = 48.0           # targeting range, in blocks
SENTRY_SCAN_INTERVAL = 0.25    # targeting loop period, seconds
SENTRY_PLACE_RANGE = 8.0      # max distance to place a sentry, in blocks
MAX_SENTRIES_PER_PLAYER = 3

# heartbeat: the TOP pillar block alternates between the tier color
# and an indicator color, so every connected client can read the
# turret state at a glance — green while scanning (nobody in sight),
# red while firing. The tier color stays every other toggle, so the
# level remains readable and the fork client can still detect the
# turret. These periodic recolor packets also let fork clients that
# joined AFTER placement discover existing turrets without rescanning
# the map. Cost: two small packets per toggle per turret, piggybacked
# on the existing scan loop (no extra scheduling)
BLINK_IDLE_INTERVAL = 2.0    # seconds between idle toggles
BLINK_ALERT_INTERVAL = 0.5   # seconds between firing toggles
BLINK_IDLE_COLOR = (0, 255, 0)    # green = scanning
BLINK_ALERT_COLOR = (255, 0, 0)   # red = target locked (old script; see
                                  # make_aim_color: the locked beat now
                                  # CARRIES the aim direction instead)

# covert channel for real shots: one SetColor packet per actual
# server-side damage application, on a reserved player id that no
# block ever follows. Vanilla clients merely store the color (no
# block is built with this id, so nothing renders); the ZeroSpades
# fork decodes the 24-bit value as the turret's (x, y) and plays the
# muzzle flash + tracer at the exact moment the damage lands. Pick
# another id here if some other script claims this one (keep in
# sync with the fork's NetClient.cpp SetColor hook)
SENTRY_SHOT_ID = 34

AIM_Z_OFFSET = 0.5             # aim point below the target's head, in
                               # blocks (matches the client's body-aim)

LOS_STEP = 0.25                # line-of-sight ray sampling step, blocks
LOS_SKIP = 1.2                 # distance ignored at ray start, blocks
                               # (avoids hitting the turret's own blocks)

# server player id used for block actions we emit (same convention as
# the official minefield/strongblock scripts)
SERVER_PLAYER_ID = 32


class Sentry:
    """A placed turret: two stacked blocks plus targeting state."""

    def __init__(self, protocol, owner, x, y, z, level):
        level_config = SENTRY_LEVELS[level]
        self.protocol = protocol
        self.owner = owner
        self.team = owner.team
        self.level = level
        self.damage = level_config["damage"]
        self.fire_interval = level_config["interval"]
        self.color = level_config["color"]
        # pillar built on top of the aimed block (z axis points down,
        # so "one block higher" is z - 1 and z - 2)
        self.blocks = [(x, y, z - 1), (x, y, z - 2)]
        # muzzle: center of the top block, at its top face
        self.muzzle = (x + 0.5, y + 0.5, float(z) - 2.0)
        self.last_shot = 0.0
        # heartbeat state (see BLINK_* constants): which half of
        # the toggle the top block currently shows, and when it
        # last changed. While locked, beat_count cycles the locked
        # beat (aim data every tick, plain tier color every 4th
        # beat for the level + late-joiner detection); locked
        # tracks the idle <-> locked transition to re-beat at once
        self.blink_on = False
        self.last_blink = 0.0
        self.beat_count = 0
        self.locked = False

    def still_alive(self):
        map = self.protocol.map
        return all(map.get_solid(*block) for block in self.blocks)

    def has_los(self, tx, ty, tz):
        """Sampled ray from the muzzle to (tx, ty, tz). True if clear."""
        ox, oy, oz = self.muzzle
        dx, dy, dz = tx - ox, ty - oy, tz - oz
        dist = sqrt(dx * dx + dy * dy + dz * dz)
        if dist < LOS_SKIP + 0.3:
            return True
        steps = max(2, int(dist / LOS_STEP))
        start = int(LOS_SKIP / LOS_STEP)
        sx, sy, sz = dx / steps, dy / steps, dz / steps
        map = self.protocol.map
        # exclude the very last step: the target itself
        for i in range(start, steps):
            x = int(ox + sx * i)
            y = int(oy + sy * i)
            z = int(oz + sz * i)
            if map.get_solid(x, y, z):
                return False
        return True

    def find_target(self):
        """Nearest visible enemy in range, or None."""
        ox, oy, oz = self.muzzle
        r2 = SENTRY_RANGE * SENTRY_RANGE
        candidates = []
        for player in self.protocol.players.values():
            if (player.team is None or player.team is self.team or
                    not player.hp or player.world_object is None):
                continue
            pos = player.world_object.position
            d2 = ((pos.x - ox) ** 2 + (pos.y - oy) ** 2 +
                  (pos.z - oz) ** 2)
            if d2 <= r2:
                candidates.append((d2, player))
        candidates.sort(key=lambda item: item[0])
        for _, player in candidates:
            pos = player.world_object.position
            # head level is the player position itself; the body aim
            # point is slightly below (z axis points down)
            if (self.has_los(pos.x, pos.y, pos.z) or
                    self.has_los(pos.x, pos.y, pos.z + AIM_Z_OFFSET)):
                return player
        return None

    def update(self):
        if not self.still_alive():
            destroy_sentry(self)
            return
        target = self.find_target()
        now = reactor.seconds()
        # heartbeat, two beats:
        # - idle: slow (tier color and green), BLINK_IDLE_INTERVAL
        # - locked: a beat EVERY scan tick, and the red beat CARRIES
        #   the server's aim direction (make_aim_color) so the
        #   ZeroSpades fork model turns where the server actually
        #   aims; every 4th beat the plain tier color instead
        #   (level flash, and lets late joiners discover the turret
        #   while it stays locked). The toggle only recolors the top
        #   block in place — no destroy, so the on_block_removed
        #   hook never sees it
        if target is None:
            if self.locked:
                # just lost the target: beat at once instead of
                # leaving the last red on the block for the whole
                # idle interval
                self.locked = False
                self.last_blink = now - BLINK_IDLE_INTERVAL
            if now - self.last_blink >= BLINK_IDLE_INTERVAL:
                self.last_blink = now
                self.blink_on = not self.blink_on
                color = (BLINK_IDLE_COLOR if self.blink_on
                         else self.color)
                send_block_color(self.protocol, self.blocks[1], color)
        else:
            self.locked = True
            self.last_blink = now
            self.beat_count = (self.beat_count + 1) % 4
            if self.beat_count == 0:
                color = self.color
            else:
                color = make_aim_color(self, target)
            send_block_color(self.protocol, self.blocks[1], color)
        if target is None:
            return
        if now - self.last_shot >= self.fire_interval:
            self.last_shot = now
            # server-side damage; kill credit and the damage direction
            # indicator both point back to this turret
            target.set_hp(target.hp - self.damage, self.owner,
                          kill_type=WEAPON_KILL,
                          hit_indicator=self.muzzle)
            # one tiny invisible packet per REAL shot (see
            # SENTRY_SHOT_ID): the fork plays the muzzle flash +
            # tracer at the exact moment this damage lands
            send_shot_event(self.protocol, self)


def destroy_sentry(sentry):
    """Remove a sentry from the map and from the protocol registry."""
    protocol = sentry.protocol
    protocol.sentries.discard(sentry)
    block_action = BlockAction()
    block_action.player_id = SERVER_PLAYER_ID
    block_action.value = DESTROY_BLOCK
    for x, y, z in sentry.blocks:
        if protocol.map.get_solid(x, y, z):
            protocol.map.remove_point(x, y, z)
            block_action.x = x
            block_action.y = y
            block_action.z = z
            protocol.broadcast_contained(block_action, save=True)
    protocol.update_entities()


def send_block_color(protocol, pos, color):
    """Recolor an existing block for every connected client.

    A BlockAction build sent over an existing solid block updates
    the block's color client-side (clients re-set the block with the
    announced color), so no destroy is involved and no block hook
    fires. The server map is updated too, so players joining later
    bake the current color into their downloaded map."""
    x, y, z = pos
    if not protocol.map.get_solid(x, y, z):
        return
    set_color = SetColor()
    set_color.player_id = SERVER_PLAYER_ID
    set_color.value = make_color(*color)
    protocol.broadcast_contained(set_color, save=True)
    block_action = BlockAction()
    block_action.value = BUILD_BLOCK
    block_action.player_id = SERVER_PLAYER_ID
    block_action.x = x
    block_action.y = y
    block_action.z = z
    protocol.broadcast_contained(block_action, save=True)
    protocol.map.set_point(x, y, z, color)


def make_aim_color(sentry, target):
    """Pack the server's CURRENT aim direction into a heartbeat color.

    The ZeroSpades fork decodes it in OnServerBlockTurret: r = 255
    marks AIM DATA, g packs the yaw (128 steps over 360 degrees,
    0 = -180 degrees from +Y), b packs the pitch (128 steps over
    +/- 80 degrees, positive = below the horizon; the map z axis
    points down). Both g and b stay < 128, and no tier color ever has
    r == 255 (see SENTRY_LEVELS), so an aim color can never collide
    with a tier color or the idle green — and vanilla clients
    still read the beat as red-ish = danger.
    """
    position = target.world_object.position
    mx, my, mz = sentry.muzzle
    dx = position.x - mx
    dy = position.y - my
    dz = position.z - mz
    yaw_q = int(round((degrees(atan2(dx, dy)) + 180.0)
                      * 128.0 / 360.0)) & 127
    pitch = degrees(atan2(dz, sqrt(dx * dx + dy * dy)))
    pitch_q = int(round((pitch + 80.0) * 127.0 / 160.0))
    pitch_q = max(0, min(127, pitch_q))
    return (255, yaw_q, pitch_q)


def send_shot_event(protocol, sentry):
    """One invisible SetColor packet per real shot (see SENTRY_SHOT_ID).

    The 24-bit value packs the turret's (x, y) = the fork client's
    anchor block coords. The vanilla SetColor wire format writes
    the value as three bytes (low to high), so `x | (y << 9)`
    survives the round trip untouched (x and y are 0..511).
    """
    set_color = SetColor()
    set_color.player_id = SENTRY_SHOT_ID
    x, y, z = sentry.blocks[0]
    set_color.value = x | (y << 9)
    protocol.broadcast_contained(set_color, save=False)


def place_sentry(connection, level):
    """Build the block turret at the aimed position, level 1-3."""
    protocol = connection.protocol
    if protocol.sentries is None:
        protocol.sentries = set()
    if connection.world_object is None or connection.team is None:
        return "You must be spawned to place a sentry."
    owned = [s for s in protocol.sentries if s.owner is connection]
    if len(owned) >= MAX_SENTRIES_PER_PLAYER:
        return ("You already have {0} sentries (max {1}). Use "
                "/sentryremove first.").format(len(owned),
                                               MAX_SENTRIES_PER_PLAYER)
    hit = connection.world_object.cast_ray(SENTRY_PLACE_RANGE)
    if hit is None:
        return "Aim at a block to place a sentry."
    x, y, z = hit
    # need two free blocks above (z axis points down: above = z - 1, z - 2)
    if z < 2 or z >= 62:
        return "Not enough room above this block."
    map = protocol.map
    if map.get_solid(x, y, z - 1) or map.get_solid(x, y, z - 2):
        return "Not enough room above this block."
    sentry_obj = Sentry(protocol, connection, x, y, z, level)
    # clients color blocks built by an unknown player id with the last
    # color received in a SetColor packet for that id, so announce our
    # server color once before broadcasting the block actions (same
    # trick as the official strongblock script); otherwise connected
    # clients render the blocks in an arbitrary color
    set_color = SetColor()
    set_color.player_id = SERVER_PLAYER_ID
    set_color.value = make_color(*sentry_obj.color)
    protocol.broadcast_contained(set_color, save=True)
    block_action = BlockAction()
    block_action.value = BUILD_BLOCK
    block_action.player_id = SERVER_PLAYER_ID
    for bx, by, bz in sentry_obj.blocks:
        protocol.map.set_point(bx, by, bz, sentry_obj.color)
        block_action.x = bx
        block_action.y = by
        block_action.z = bz
        protocol.broadcast_contained(block_action, save=True)
    protocol.update_entities()
    protocol.sentries.add(sentry_obj)
    protocol.start_sentry_loop()
    return "Sentry level {0} placed at ({1}, {2}, {3}).".format(
        level, x, y, z - 1)


def parse_level(value):
    if value is None:
        return 1
    try:
        level = int(value)
    except (TypeError, ValueError):
        return None
    if level not in SENTRY_LEVELS:
        return None
    return level


@command('sentry', 'turret')
def sentry(connection, level=None):
    """Place a sentry (level 1-3) on the block you are aiming at."""
    parsed = parse_level(level)
    if parsed is None:
        return "usage: /sentry [1-3]"
    return place_sentry(connection, parsed)


@command('sentry1', 'turret1')
def sentry1(connection):
    """Place a level 1 sentry."""
    return place_sentry(connection, 1)


@command('sentry2', 'turret2')
def sentry2(connection):
    """Place a level 2 sentry."""
    return place_sentry(connection, 2)


@command('sentry3', 'turret3')
def sentry3(connection):
    """Place a level 3 sentry."""
    return place_sentry(connection, 3)


@command('sentryremove', 'turretremove')
def sentryremove(connection):
    """Remove all sentries you own."""
    protocol = connection.protocol
    if protocol.sentries is None:
        protocol.sentries = set()
    owned = [s for s in protocol.sentries if s.owner is connection]
    if not owned:
        return "You have no sentries."
    for sentry_obj in owned:
        destroy_sentry(sentry_obj)
    return "Removed {0} sentries.".format(len(owned))


def apply_script(protocol, connection, config):

    class SentryProtocol(protocol):
        sentries = None
        sentry_loop = None

        def on_map_change(self, map):
            if self.sentry_loop is not None:
                self.sentry_loop.cancel()
                self.sentry_loop = None
            self.sentries = set()
            protocol.on_map_change(self, map)

        def start_sentry_loop(self):
            # self-sustaining loop: runs only while sentries exist
            if self.sentry_loop is None:
                self.sentry_loop = reactor.callLater(
                    SENTRY_SCAN_INTERVAL, self.sentry_tick)

        def sentry_tick(self):
            self.sentry_loop = None
            if self.sentries:
                for sentry_obj in list(self.sentries):
                    sentry_obj.update()
                self.sentry_loop = reactor.callLater(
                    SENTRY_SCAN_INTERVAL, self.sentry_tick)

    class SentryConnection(connection):

        def on_disconnect(self):
            for sentry_obj in list(self.protocol.sentries or ()):
                if sentry_obj.owner is self:
                    destroy_sentry(sentry_obj)
            connection.on_disconnect(self)

        def on_block_removed(self, x, y, z):
            for sentry_obj in list(self.protocol.sentries or ()):
                if (x, y, z) in sentry_obj.blocks:
                    destroy_sentry(sentry_obj)
                    if sentry_obj.owner is not self and \
                            sentry_obj.owner.name is not None:
                        sentry_obj.owner.send_chat("Your sentry was "
                                                  "destroyed!")
            return connection.on_block_removed(self, x, y, z)

    return SentryProtocol, SentryConnection
