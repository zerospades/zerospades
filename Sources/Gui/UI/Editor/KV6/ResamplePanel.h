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
#include <Gui/UI/Components/StepSlider.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * Asks at what resolution to make a KV6 of a node: a slider over the
		 * steps its owner gives (1/10 ... 10/1), and what the chosen one comes
		 * to. The host shows the result in place of the node as it changes.
		 *
		 * While it is open it owns clicks and the keys it knows (Left and
		 * Right step, Home and End go to the ends, Enter creates, Escape
		 * cancels); the host lets the camera keep moving, to look the preview
		 * over.
		 */
		class ResamplePanel {
		public:
			enum class Action { None, Changed, Create, Cancel };

			ResamplePanel(client::IRenderer& renderer, client::IFont& font,
			              client::IAudioDevice* audioDevice);

			/** Opens for the node called `name`, over `steps`, at `index`. */
			void Open(const std::string& name, std::vector<StepSlider::Step> steps, int index);
			void Close() { open = false; }
			bool IsOpen() const { return open; }
			int Index() const { return slider.Index(); }
			/** Chooses step `index`, as the slider would; false when it cannot be. */
			bool Select(int index) { return slider.Select(index) || slider.Index() == index; }

			/** What the node is and what the chosen step makes of it; with
			 *  `refused` set instead, why it makes nothing, and it cannot be
			 *  created. */
			void SetResult(const IntVector3& sourceSize, long sourceVoxels, const IntVector3& size,
			               long voxels, const std::string& refused);

			/** Its top left corner. */
			void Layout(float x, float y);
			bool Contains(const Vector2& at) const;
			/** A left press: on the slider it moves the choice; a button runs
			 *  on its release over it. */
			Action Press(const Vector2& at);
			/** The pointer moving, the slider's handle following it while held. */
			Action Move(const Vector2& at);
			Action Release(const Vector2& at);
			/** A key down: None when the panel has no use for it. */
			Action Key(const std::string& key);

			void Draw(const Vector2& cursor);

		private:
			enum class Button { None, Cancel, Create };

			client::IRenderer& renderer;
			client::IFont& font;
			ui::FeedbackSounds sounds;
			StepSlider slider;
			bool open = false;
			std::string name;
			IntVector3 sourceSize = MakeIntVector3(0, 0, 0), size = MakeIntVector3(0, 0, 0);
			long sourceVoxels = 0, voxels = 0;
			std::string refused;
			float x = 0.0F, y = 0.0F;
			Button pressed = Button::None;
			Button hoverButton = Button::None;

			Button ButtonAt(const Vector2& at) const;
			void ButtonRect(Button button, float& bx, float& by, float& bw, float& bh) const;
			bool CanCreate() const { return refused.empty() && voxels > 0; }
			/** Changed, with a tick, when the slider moved. */
			Action Moved(bool moved);
		};
	} // namespace gui
} // namespace spades
