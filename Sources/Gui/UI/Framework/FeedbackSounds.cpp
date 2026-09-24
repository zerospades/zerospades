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

#include "FeedbackSounds.h"

#include <Client/IAudioChunk.h>
#include <Client/IAudioDevice.h>

namespace spades {
	namespace gui {
		namespace ui {
			const char* const kHoverSound = "Sounds/Feedback/Limbo/Hover.opus";
			const char* const kActivateSound = "Sounds/Feedback/Limbo/Select.opus";

			FeedbackSounds::FeedbackSounds(client::IAudioDevice* device) : device(device) {}

			void FeedbackSounds::Hover() const { Play(kHoverSound); }

			void FeedbackSounds::Activate() const { Play(kActivateSound); }

			bool FeedbackSounds::HoverEdge(bool hovered, bool wasHovered) const {
				if (hovered && !wasHovered)
					Hover();
				return hovered;
			}

			void FeedbackSounds::Play(const char* sound) const {
				if (!device)
					return;
				Handle<client::IAudioChunk> chunk(device->RegisterSound(sound));
				device->PlayLocal(chunk.GetPointerOrNull(), client::AudioParam());
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
