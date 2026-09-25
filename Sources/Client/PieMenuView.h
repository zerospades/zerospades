/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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
 along with ZeroSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <array>
#include <string>
#include <vector>

#include <Core/Math.h>

namespace spades {
	namespace client {
		class Client;
		class IFont;
		class IRenderer;

		class PieMenuView {
		public:
			// What the crosshair is on when the menu opens decides which set of rings
			// is offered. It is fixed from then on: the aim is free to leave, and a
			// message meant for someone must not need them held under the crosshair.
			enum class Variant { World, Teammate };
			enum Slice { None = -1 };

			static constexpr int kSliceCount = 6;
			static constexpr float kSliceSpan = M_PI_F * 2.0F / static_cast<float>(kSliceCount);

		private:
			// A single ring of messages. Rings are cycled in place while the menu is
			// held, so every message stays exactly one gesture away from the centre.
			struct Page {
				std::string name;                            // centre caption
				bool global = false;                         // chat channel
				// Drawn as they are and sent as they are: one string, so what a
				// player picks is character for character what everybody reads.
				// A ring set is content rather than chrome — one day a server
				// will supply its own, in whatever language its players speak —
				// so none of it goes through the translation catalogue.
				std::array<std::string, kSliceCount> labels;
				// Slices that drop a Teamplay ping instead of chatting. A ring
				// carries its own, so a "where" ring can point at places while
				// the ring beside it only talks.
				std::array<bool, kSliceCount> pings{};
			};

			IRenderer& renderer;
			IFont* font;
			IFont* bigFont;

			bool open = false;
			Variant variant = Variant::World;
			int targetPlayerId = -1;
			Vector2 cursor = {0.0F, 0.0F};
			int selection = None;
			int page = 0;

			float openPhase = 0.0F;
			float pagePhase = 1.0F;
			float hintTime = 0.0F;
			// Cleared for good the first time a ring is cycled, so the discovery
			// hint retires itself instead of nagging a player who already knows.
			bool hintNeeded = true;

			std::array<float, kSliceCount> highlight{};

			std::vector<Page> worldPages;
			std::vector<Page> teammatePages;

			// Ring each context was last left on. A player who works out of one ring
			// gets it back on the next open instead of paying for the flip every time.
			static constexpr int kVariantCount = 2;
			// `lastPage` is indexed by Variant, so the two have to agree; adding a
			// context without widening the array would write past the end of it.
			static_assert(kVariantCount == static_cast<int>(Variant::Teammate) + 1,
						  "kVariantCount must cover every Variant");
			std::array<int, kVariantCount> lastPage{};

			// Precomputed per-slice ray params (sin/cos of θ_c ± α).
			// Populated once in the constructor; used by scanline fill.
			struct SliceRay { float s1, c1, s2, c2; };
			std::array<SliceRay, kSliceCount> sliceRays;
			std::array<float, kSliceCount> sliceCenterAngles;

			const std::vector<Page>& CurrentPages() const {
				return (variant == Variant::Teammate) ? teammatePages : worldPages;
			}
			const Page& CurrentPage() const;
			void RestorePage();
			void DrawPageIndicator(Vector2 center, float rOuter, float alpha);

		public:
			PieMenuView(Client*, IFont* font, IFont* bigFont);
			~PieMenuView();

			void Open(Variant v, int targetPlayerId = -1);
			int Close();

			bool IsOpen() const { return open; }
			int GetTargetPlayerId() const { return targetPlayerId; }
			int GetSelection() const { return selection; }
			int GetPage() const { return page; }
			int GetPageCount() const { return static_cast<int>(CurrentPages().size()); }

			// Chat channel the ring currently shown commits to. Only meaningful for
			// the World variant; Player rings always go out as private messages.
			bool IsCurrentPageGlobal() const { return CurrentPage().global; }

			// Text of the highlighted slice, as drawn and as sent.
			const std::string& GetSelectionLabel() const;

			// Move to the next (dir > 0) or previous (dir < 0) ring, wrapping around.
			// The cursor is kept, so the slice under it stays selected across the flip.
			void CyclePage(int dir);

			/**
			 * Whether a slice drops a *Teamplay* ping rather than sending its
			 * message on chat. The ping carries the slice's wire text (the same
			 * string `GetSelectionLabel` returns) as its reason, so what other
			 * players read is the same either way.
			 *
			 * It is a property of the ring on show, not of the menu: a ring aimed
			 * at a place points at one, while a ring aimed at a person has nowhere
			 * to put a marker and only talks.
			 */
			bool SlicePings(int index) const {
				if (index < 0 || index >= kSliceCount)
					return false;
				return CurrentPage().pings[static_cast<size_t>(index)];
			}

			void HandleMouseDelta(float dx, float dy);
			void Update(float dt);
			void Draw();
		};
	} // namespace client
} // namespace spades
