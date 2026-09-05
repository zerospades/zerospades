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

#include <cmath>
#include <utility>

#include "ExtendedTeamplay.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		namespace {
			/** Every feature bit this client knows about. The specification reserves the
			 * remaining bits and requires unknown ones to be ignored, so masking here
			 * means a future server can set them without changing behaviour. */
			constexpr uint8_t kKnownFeatures =
			  ExtendedTeamplay::FeatureTeamESP | ExtendedTeamplay::FeaturePing |
			  ExtendedTeamplay::FeatureCompassHud;

			/** Likewise for the Surfaces byte both the Ping and the ESP Mark carry. */
			constexpr uint8_t kKnownSurfaces =
			  ExtendedTeamplay::SurfaceWorld | ExtendedTeamplay::SurfaceMinimap |
			  ExtendedTeamplay::SurfaceCompass;

			/** Likewise for the ESP Mark flags. */
			constexpr uint8_t kKnownMarkFlags =
			  ExtendedTeamplay::MarkFlagClearOnRespawn | ExtendedTeamplay::MarkFlagShowName;

			bool IsUTF8Continuation(char c) { return (static_cast<unsigned char>(c) & 0xC0) == 0x80; }

			/**
			 * Copies `s` without the byte sequences that are not well-formed UTF-8.
			 *
			 * The reason arrives as raw bytes from a stranger, and everything downstream
			 * — measuring the label, laying it out, drawing it — assumes valid UTF-8.
			 * Rejecting the whole string over one bad byte would lose a reason that is
			 * otherwise readable, so the bad bytes are dropped and the rest kept.
			 */
			std::string DropIllFormedUTF8(const std::string& s) {
				std::string out;
				out.reserve(s.size());

				for (size_t i = 0; i < s.size();) {
					auto lead = static_cast<unsigned char>(s[i]);

					size_t len;
					uint32_t cp;
					if (lead < 0x80) {
						len = 1;
						cp = lead;
					} else if ((lead & 0xE0) == 0xC0) {
						len = 2;
						cp = lead & 0x1F;
					} else if ((lead & 0xF0) == 0xE0) {
						len = 3;
						cp = lead & 0x0F;
					} else if ((lead & 0xF8) == 0xF0) {
						len = 4;
						cp = lead & 0x07;
					} else { // a continuation byte on its own, or a 5+ byte lead
						i++;
						continue;
					}

					if (i + len > s.size())
						break; // truncated at the end of the string

					bool ok = true;
					for (size_t j = 1; j < len; j++) {
						if (!IsUTF8Continuation(s[i + j])) {
							ok = false;
							break;
						}
						cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3F);
					}

					// An overlong encoding, a surrogate half or a codepoint past the end
					// of Unicode is ill-formed even when every byte is in the right shape.
					if (ok) {
						static const uint32_t kMinimum[5] = {0, 0, 0x80, 0x800, 0x10000};
						ok = cp >= kMinimum[len] && cp <= 0x10FFFF &&
							 !(cp >= 0xD800 && cp <= 0xDFFF);
					}

					if (ok)
						out.append(s, i, len);

					i += ok ? len : 1;
				}

				return out;
			}
		} // namespace

		bool ExtendedTeamplay::IsValidDuration(float duration) {
			return !std::isnan(duration) && duration >= 0.0F;
		}

		bool ExtendedTeamplay::IsEndlessDuration(float duration) { return std::isinf(duration); }

		uint8_t ExtendedTeamplay::ResolveSurfaces(uint8_t surfaces) {
			if (surfaces == 0) // named nothing: the client places it
				return kDefaultSurfaces;
			return surfaces & kKnownSurfaces;
		}

		float ExtendedTeamplay::Ping::GetAgeFraction() const {
			if (endless || duration <= 0.0F)
				return 0.0F;
			return Clamp(1.0F - timeLeft / duration, 0.0F, 1.0F);
		}

		float ExtendedTeamplay::Ping::GetFadeAlpha(float fadeTime) const {
			if (endless || fadeTime <= 0.0F)
				return 1.0F;
			return Clamp(timeLeft / fadeTime, 0.0F, 1.0F);
		}

		std::string ExtendedTeamplay::SanitizeReason(std::string reason) {
			reason = TrimSpaces(StripNewlines(DropIllFormedUTF8(reason)));

			if (reason.size() > kMaxReasonBytes) {
				// Back off to the start of the codepoint that straddles the cap, so the
				// result is never a half-encoded character. A codepoint is at most 4
				// bytes, so this walks back 3 bytes at the very most.
				size_t end = kMaxReasonBytes;
				while (end > 0 && IsUTF8Continuation(reason[end]))
					end--;

				reason.resize(end);

				// Cutting mid-phrase can leave the space that preceded the dropped word.
				reason = TrimSpaces(reason);
			}

			return reason;
		}

		void ExtendedTeamplay::SetFeatures(uint8_t newFeatures) {
			SPADES_MARK_FUNCTION();

			features = newFeatures & kKnownFeatures;

			// A ping the client sent before this arrived may still be in flight; the
			// server drops it, which is not an error. Pings already on screen are kept:
			// the specification only gates whether new ones may be sent and drawn, and
			// dropping live ones would make a policy change look like a glitch.
		}

		void ExtendedTeamplay::SetPing(int playerId, const Vector3& position, float duration,
		                               uint8_t surfaces, const IntVector3& color,
		                               std::string reason) {
			SPADES_MARK_FUNCTION();

			if (duration == 0.0F) { // removes the player's ping without placing another
				pings.erase(playerId);
				return;
			}

			Ping& ping = pings[playerId];
			ping.playerId = playerId;
			ping.position = position;
			ping.reason = std::move(reason);
			ping.surfaces = ResolveSurfaces(surfaces);
			ping.color = color;
			ping.endless = IsEndlessDuration(duration);
			ping.duration = ping.endless ? 0.0F : duration;
			ping.timeLeft = ping.duration;
		}

		void ExtendedTeamplay::SetMark(int playerId, float duration, uint8_t surfaces,
		                               uint8_t flags, const IntVector3& color,
		                               std::string reason) {
			SPADES_MARK_FUNCTION();

			if (duration == 0.0F) { // clears the mark
				marks.erase(playerId);
				return;
			}

			// Reserved flag bits are masked off here, so no other code has to know which
			// of them are defined.
			flags &= kKnownMarkFlags;

			Mark& mark = marks[playerId];
			mark.reason = std::move(reason);
			mark.surfaces = ResolveSurfaces(surfaces);
			mark.color = color;
			mark.endless = IsEndlessDuration(duration);
			mark.timeLeft = mark.endless ? 0.0F : duration;
			mark.clearOnRespawn = (flags & MarkFlagClearOnRespawn) != 0;
			mark.showName = (flags & MarkFlagShowName) != 0;
		}

		stmp::optional<const ExtendedTeamplay::Mark&>
		ExtendedTeamplay::GetMark(int playerId) const {
			auto it = marks.find(playerId);
			if (it == marks.end())
				return {};
			return it->second;
		}

		void ExtendedTeamplay::PlayerSpawned(int playerId) {
			// Keyed to the spawn rather than to the death, so any Create Player for the
			// id ends the mark — killed, changed team, changed weapon or moved by a
			// script — and this class needs no death bookkeeping of its own.
			auto it = marks.find(playerId);
			if (it != marks.end() && it->second.clearOnRespawn)
				marks.erase(it);
		}

		void ExtendedTeamplay::PlayerLeft(int playerId) {
			marks.erase(playerId);
			pings.erase(playerId);
		}

		void ExtendedTeamplay::Update(float dt) {
			SPADES_MARK_FUNCTION();

			for (auto it = pings.begin(); it != pings.end();) {
				if (it->second.endless) {
					++it;
					continue;
				}

				it->second.timeLeft -= dt;
				if (it->second.timeLeft <= 0.0F)
					it = pings.erase(it);
				else
					++it;
			}

			for (auto it = marks.begin(); it != marks.end();) {
				if (it->second.endless) {
					++it;
					continue;
				}

				it->second.timeLeft -= dt;
				if (it->second.timeLeft <= 0.0F)
					it = marks.erase(it);
				else
					++it;
			}
		}

		void ExtendedTeamplay::ClearTransientState() {
			pings.clear();
			marks.clear();
		}

		void ExtendedTeamplay::Reset() {
			ClearTransientState();
			features = 0;
		}
	} // namespace client
} // namespace spades
