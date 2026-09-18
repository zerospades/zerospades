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

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

#include <Core/Math.h>
#include <Core/TMPUtils.h>

namespace spades {
	namespace client {

		/**
		 * Client-side state of the *Teamplay* protocol extension
		 * (extension id 48, packet id 112).
		 *
		 * The extension lets a server permit a set of optional teamplay features, relays
		 * in-world team pings, and lets the server reveal a chosen player through walls.
		 * This class owns everything the extension makes the client remember: the Config
		 * the server announced (feature bitmask and north), the pings currently on
		 * screen, and the ESP marks currently in force. It holds no rendering or
		 * networking concerns — `NetClient` feeds it, `Client` ticks it, and the draw
		 * code reads it.
		 *
		 * Both the ping lifetime and the mark lifetime are expired here rather than by the
		 * server: the protocol makes expiry the client's job and gives the server a
		 * Duration of `0` to take something away before its time.
		 */
		class Teamplay {
		public:
			/**
			 * Feature bits carried by the Config sub-packet. Bits 3-7 are reserved and
			 * must be ignored, so an unknown bit never enables anything.
			 *
			 * Every one of them is a permission for something the client would otherwise
			 * do on its own initiative. None of them governs what the server sends: a
			 * ping or a mark is drawn where the packet says, not where a bit allows.
			 */
			enum Feature : uint8_t {
				/** The client may render its own teammates through walls. */
				FeatureTeamESP = 1 << 0,
				/** The client may send Ping packets. */
				FeaturePing = 1 << 1,
				/** The client may draw a compass HUD at all. */
				FeatureCompassHud = 1 << 2,
			};

			/**
			 * Surface bits carried by both the Ping and the ESP Mark, saying where the
			 * client is to show them. Bits 3-7 are reserved.
			 *
			 * The packet decides: the client draws it on exactly the surfaces named and
			 * on no others. The compass is the one surface it may withhold, when the
			 * server has not permitted a compass or this client has none to draw on.
			 */
			enum Surface : uint8_t {
				/** In the world: a 3D marker, or the body outline of a mark. */
				SurfaceWorld = 1 << 0,
				/** On the minimap, at the position. */
				SurfaceMinimap = 1 << 1,
				/** On the compass HUD, as a bearing — the direction, not the place. */
				SurfaceCompass = 1 << 2,
			};

			/** What a Surfaces of `0` asks for: this client's own default placement.
			 * The compass is left out of it because a compass exists only where the
			 * server grants one, so a server with no opinion gets the marker and the
			 * minimap dot. */
			static constexpr uint8_t kDefaultSurfaces = SurfaceWorld | SurfaceMinimap;

			/** Flag bits carried by the ESP Mark sub-packet. Bits 2-7 are reserved. */
			enum MarkFlag : uint8_t {
				/** The mark ends the next time the marked player spawns. */
				MarkFlagClearOnRespawn = 1 << 0,
				/** The client shows the marked player's name. Clear, it must not. */
				MarkFlagShowName = 1 << 1,
			};

			/** Player ID a Ping carries when the server originated it itself. */
			static constexpr int kServerPlayerId = 255;

			/** The only Message ID version 1 of the extension defines. The byte is
			 * reserved for a later version that names a label instead of spelling it;
			 * a receiver that gets any other value renders the packet and ignores it. */
			static constexpr uint8_t kReservedMessageId = 0;

			/** The reason strings are free-form UTF-8 and the protocol assigns no fixed
			 * values, so a server may send anything. The cap only bounds what this
			 * client is willing to keep; longer strings are truncated on a codepoint
			 * boundary rather than rejected. */
			static constexpr size_t kMaxReasonBytes = 64;

			/**
			 * A Duration field, which both the Ping and the ESP Mark carry as an
			 * `LE float32` number of seconds: `0` removes what the packet refers to, a
			 * positive finite value is a lifetime the client counts down itself, and
			 * `+inf` lasts until the server takes it away. Negative and NaN are invalid
			 * and the packet carrying one is dropped.
			 */
			static bool IsValidDuration(float duration);

			/** Whether a valid Duration means "until the server removes it". */
			static bool IsEndlessDuration(float duration);

			/** Whether a ping's Position can be drawn: finite and inside the map volume.
			 * The server is authoritative on placement, but a NaN or a far out-of-bounds
			 * position would corrupt the projection maths, so the packet is dropped. */
			static bool IsValidPingPosition(const Vector3& position);

			struct Ping {
				/** The player who pinged, or `kServerPlayerId` for a server-origin ping. */
				int playerId;
				Vector3 position;
				std::string reason;
				/** Where the packet asked for it, as `Surface` bits. */
				uint8_t surfaces;
				/** The colour the server chose, as 0-255 per channel. Drawn as sent on
				 * every surface the packet named; working out what colour a ping should
				 * be is the server's job, not this client's. */
				IntVector3 color;
				/** The lifetime the server gave it. Meaningless when `endless`. */
				float duration;
				/** Seconds until the ping disappears. Meaningless when `endless`. */
				float timeLeft;
				/** The ping stays until the server removes it (Duration `+inf`). */
				bool endless;
				/** Seconds since the ping was last placed, endless or not; drives the
				 * marker's animation, so placing it again replays the intro. */
				float age;

				/** Opacity for a marker that fades over its last `fadeTime` seconds
				 * rather than blinking out. An endless ping never fades. */
				float GetFadeAlpha(float fadeTime) const;
			};

			struct Mark {
				std::string reason;
				/** Where the packet asked for it, as `Surface` bits. */
				uint8_t surfaces;
				/** The Surfaces byte exactly as the server sent it, before this client
				 * resolved it. A mark outlives the packet that placed it and is sent on
				 * into a demo, which has to carry what arrived rather than what this
				 * version made of it. */
				uint8_t sentSurfaces;
				/** The colour the server chose for the outline, as 0-255 per channel,
				 * drawn as sent on every surface the packet named. */
				IntVector3 color;
				/** Seconds until the mark expires. Meaningless when `endless`. */
				float timeLeft;
				/** The mark lasts until the server clears it (Duration `+inf`). */
				bool endless;
				/** The mark ends the next time the marked player spawns. */
				bool clearOnRespawn;
				/** The server permits the marked player's name to be shown. */
				bool showName;
			};

			/** Pings currently on screen, keyed by the player who originated them: the
			 * specification allows one live ping per player, a newer one replacing it and
			 * restarting its timer. */
			using PingMap = std::unordered_map<int, Ping>;

			/** Marks in force, keyed by the player they reveal, one apiece. */
			using MarkMap = std::unordered_map<int, Mark>;

			Teamplay() = default;

			/** The north a client uses until a Config says otherwise, and in place of a
			 * malformed one. */
			static Vector2 DefaultNorth() { return MakeVector2(0.0F, -1.0F); }

			/**
			 * Normalises the North a Config carries. Only the direction is used, so any
			 * length is accepted; `(0, 0)`, a NaN or an infinity in either component is
			 * malformed and yields `DefaultNorth()`.
			 */
			static Vector2 ResolveNorth(float x, float y);

			/**
			 * Applies a Config sub-packet, immediately and in full: the bitmask and the
			 * north replace what the previous Config said. Reserved bits are dropped here
			 * so no other code has to know which bits are defined.
			 */
			void ApplyConfig(uint8_t features, float northX, float northY);
			uint8_t GetFeatures() const { return features; }

			/** The unit map-plane vector pointing north, in world coordinates. */
			const Vector2& GetNorth() const { return north; }

			/** The compass bearing of a map-plane direction against `GetNorth()`, in
			 * degrees clockwise from north within `[0, 360)`. */
			float GetBearing(const Vector2& direction) const;

			bool IsTeamESPEnabled() const { return (features & FeatureTeamESP) != 0; }

			/** Whether the client may send a Ping at all. With the bit clear it must not,
			 * and the server would drop what it sent anyway. */
			bool CanSendPing() const { return (features & FeaturePing) != 0; }

			/** Whether the client may draw a compass HUD. The compass exists only where
			 * the server allows it, so a client never turns one on by itself. */
			bool IsCompassAllowed() const { return (features & FeatureCompassHud) != 0; }

			/** Applies a relayed Ping. A Duration of `0` removes the player's ping; any
			 * other value replaces it and restarts its lifetime. The caller is expected
			 * to have rejected an invalid Duration with the rest of the packet. */
			void SetPing(int playerId, const Vector3& position, float duration,
						 uint8_t surfaces, const IntVector3& color, std::string reason);

			/** Applies an ESP Mark sub-packet. A Duration of `0` clears the player's mark;
			 * any other value replaces the previous mark and restarts its timer. */
			void SetMark(int playerId, float duration, uint8_t surfaces, uint8_t flags,
						 const IntVector3& color, std::string reason);

			/** The mark in force for `playerId`, or empty when the player is not marked. */
			stmp::optional<const Mark&> GetMark(int playerId) const;

			const PingMap& GetPings() const { return pings; }
			const MarkMap& GetMarks() const { return marks; }
			bool HasPings() const { return !pings.empty(); }
			bool HasMarks() const { return !marks.empty(); }

			/** A 0-255 colour triple as the renderer wants it. The packet's colour is
			 * drawn as sent: the client is told which colour to use, never what it
			 * stands for, so there is nothing here to interpret. */
			static Vector3 ToRenderColor(const IntVector3& color) {
				return MakeVector3(color.x / 255.0F, color.y / 255.0F, color.z / 255.0F);
			}

			/** Normalises a Surfaces byte: reserved bits are dropped, and a byte that
			 * named nothing at all asks for this client's default placement. A byte that
			 * named only surfaces this version does not know keeps naming nothing, since
			 * it did make a choice and that choice was not the default. */
			static uint8_t ResolveSurfaces(uint8_t surfaces);

			/** Drops the mark of a player who just spawned and had `CLEAR_ON_RESPAWN`
			 * set. A mark without that flag survives death and respawn. */
			void PlayerSpawned(int playerId);

			/** Drops everything belonging to a player who left the server. */
			void PlayerLeft(int playerId);

			/** Expires pings and marks. `dt` is real time, so a paused demo does not
			 * expire anything. */
			void Update(float dt);

			/** Clears every ping and mark without touching the Config. Used on a map
			 * change: the Config belongs to the connection, so its bitmask and north
			 * carry into the new world until the server sends another. */
			void ClearTransientState();

			/**
			 * Makes a reason string safe to draw and to send.
			 *
			 * A reason is free-form UTF-8 chosen by whoever sent it, so it arrives
			 * unvetted: ill-formed UTF-8 would reach the font as bytes it cannot read,
			 * newlines would break out of the one line the marker gives it, and padding
			 * would push the label off centre. Drops the first, strips the rest, then
			 * caps the length on a UTF-8 codepoint boundary.
			 *
			 * A reason that was nothing but whitespace comes back **empty**, which is a
			 * value the protocol allows in its own right — a neutral marker with no
			 * wording — so every caller has to cope with it either way.
			 */
			static std::string SanitizeReason(std::string reason);

		private:
			uint8_t features = 0;
			Vector2 north = DefaultNorth();
			PingMap pings;
			MarkMap marks;
		};

	} // namespace client
} // namespace spades
