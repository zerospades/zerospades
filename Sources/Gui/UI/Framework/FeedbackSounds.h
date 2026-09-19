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

namespace spades {
	namespace client {
		class IAudioDevice;
	} // namespace client
	namespace gui {
		namespace ui {
			// The sounds every control makes, widgets and hand-drawn bars alike.
			extern const char* const kHoverSound;    // the pointer arrives on a control
			extern const char* const kActivateSound; // a control is clicked or chosen

			/**
			 * Plays the controls' feedback sounds for UI drawn without the widget
			 * framework (the editor's toolbars and menu), which has no UIManager
			 * to play them. The device, owned by the UI that holds this, must
			 * outlive it; without one it stays silent.
			 */
			class FeedbackSounds {
			public:
				explicit FeedbackSounds(client::IAudioDevice* device);

				void Hover() const;
				void Activate() const;
				/**
				 * Hover, as the pointer arrives on a control: when `hovered` but not
				 * `wasHovered`. Returns `hovered`, to keep for the next call.
				 */
				bool HoverEdge(bool hovered, bool wasHovered) const;

			private:
				client::IAudioDevice* device;
				void Play(const char* sound) const;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
