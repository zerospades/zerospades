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

#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * Asks how coarse a low-poly model of a node should be: one voxel of it
		 * for every `factor` voxels each way. The host shows it in place of the
		 * node as the factor changes, and the panel says what it comes to.
		 *
		 * While it is open it owns clicks and the keys it knows (the arrows or
		 * a digit choose, Enter creates, Escape cancels); the host lets the
		 * camera keep moving, to look the preview over.
		 */
		class LowPolyPanel {
		public:
			enum class Action { None, Changed, Create, Cancel };

			LowPolyPanel(client::IRenderer& renderer, client::IFont& font,
			             client::IAudioDevice* audioDevice);

			/** Opens for the node called `name`, at `factor`. */
			void Open(const std::string& name, int factor);
			void Close() { open = false; }
			bool IsOpen() const { return open; }
			int Factor() const { return factor; }

			/** What the node is and what the chosen factor makes of it; a
			 *  result of no voxel cannot be created. */
			void SetStats(const IntVector3& sourceSize, long sourceVoxels, const IntVector3& size,
			              long voxels);

			/** Its top left corner. */
			void Layout(float x, float y);
			bool Contains(const Vector2& at) const;
			/** A left press; a factor is chosen on the press, a button runs on
			 *  its release over it. */
			Action Press(const Vector2& at);
			Action Release(const Vector2& at);
			/** A key down: None when the panel has no use for it. */
			Action Key(const std::string& key);

			void Draw(const Vector2& cursor);

		private:
			enum class Button { None, Cancel, Create };

			client::IRenderer& renderer;
			client::IFont& font;
			ui::FeedbackSounds sounds;
			bool open = false;
			std::string name;
			int factor = 2;
			IntVector3 sourceSize = MakeIntVector3(0, 0, 0), size = MakeIntVector3(0, 0, 0);
			long sourceVoxels = 0, voxels = 0;
			float x = 0.0F, y = 0.0F;
			Button pressed = Button::None;
			int hoverChip = -1;
			Button hoverButton = Button::None;

			int ChipAt(const Vector2& at) const;
			Button ButtonAt(const Vector2& at) const;
			void ChipRect(int index, float& cx, float& cy, float& cw, float& ch) const;
			void ButtonRect(Button button, float& bx, float& by, float& bw, float& bh) const;
			bool CanCreate() const { return voxels > 0; }
			/** Moves to the next factor that way; Changed when it moved. */
			Action Step(int direction);
		};
	} // namespace gui
} // namespace spades
