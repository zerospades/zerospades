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

#include "Toolbar.h"

#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
#include <Client/IAudioChunk.h>
#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		// Layout constants
		static const float kRibbonH = 24.0F;
		static const float kToolbarH = 32.0F;
		static const float kTbBtn = 84.0F;
		static const float kTbH = 24.0F;
		static const float kTbGap = 2.0F;
		static const float kTbSep = 14.0F;
		static const float kUndoBtnW = 50.0F;
		static const float kTbX0 = 12.0F;
		static const float kTbY = kRibbonH + (kToolbarH - kTbH) * 0.5F;

		Toolbar::Toolbar(client::IAudioDevice* audioDevice) : audioDevice(audioDevice) {}

		void Toolbar::PlayHoverSound() const {
			if (!audioDevice)
				return;
			Handle<client::IAudioChunk> chunk(audioDevice->RegisterSound("Sounds/Feedback/Limbo/Hover.opus"));
			audioDevice->PlayLocal(chunk.GetPointerOrNull(), client::AudioParam());
		}

		void Toolbar::PlayClickSound() const {
			if (!audioDevice)
				return;
			Handle<client::IAudioChunk> chunk(audioDevice->RegisterSound("Sounds/Feedback/Limbo/Select.opus"));
			audioDevice->PlayLocal(chunk.GetPointerOrNull(), client::AudioParam());
		}

		void Toolbar::SetModeButtons(const std::vector<ToolbarButton>& buttons) {
			modeButtons = buttons;
		}

		void Toolbar::SetToolButtons(const std::vector<ToolbarButton>& buttons) {
			toolButtons = buttons;
		}

		void Toolbar::SetUndoButton(bool enabled) { undoEnabled = enabled; }

		void Toolbar::SetRedoButton(bool enabled) { redoEnabled = enabled; }

		std::vector<Toolbar::Slot> Toolbar::Layout(float screenWidth) const {
			std::vector<Slot> slots;
			float x = kTbX0;
			for (int i = 0; i < int(modeButtons.size()); i++) {
				slots.push_back({Kind::Mode, i, x, kTbBtn, false});
				x += kTbBtn + kTbGap;
			}
			for (int i = 0; i < int(toolButtons.size()); i++) {
				// The first tool is set apart from the modes, and every later one
				// from its predecessor when it starts another group.
				bool separator = (i == 0) ? !modeButtons.empty()
				                          : toolButtons[i].group != toolButtons[i - 1].group;
				if (separator)
					x += kTbSep;
				slots.push_back({Kind::Tool, i, x, kTbBtn, separator});
				x += kTbBtn + kTbGap;
			}
			// Undo / Redo on the right edge
			float undoX = screenWidth - 12.0F - 2.0F * kUndoBtnW - kTbGap;
			slots.push_back({Kind::Undo, -1, undoX, kUndoBtnW, false});
			slots.push_back({Kind::Redo, -1, undoX + kUndoBtnW + kTbGap, kUndoBtnW, false});
			return slots;
		}

		Toolbar::ToolbarButton Toolbar::ButtonOf(const Slot& slot) const {
			if (slot.kind == Kind::Mode)
				return modeButtons[slot.index];
			if (slot.kind == Kind::Tool)
				return toolButtons[slot.index];
			const bool undo = slot.kind == Kind::Undo;
			ToolbarButton history;
			history.label = undo ? "Undo" : "Redo";
			history.enabled = undo ? undoEnabled : redoEnabled;
			return history;
		}

		bool Toolbar::Click(const Vector2& p, float screenWidth) {
			for (const Slot& slot : Layout(screenWidth)) {
				if (!OverlayInRect(p, slot.x, kTbY, slot.width, kTbH))
					continue;
				if (!ButtonOf(slot).enabled)
					return false; // greyed out: the click does nothing
				switch (slot.kind) {
					case Kind::Mode:
						if (OnModeClicked)
							OnModeClicked(slot.index);
						break;
					case Kind::Tool:
						if (OnToolClicked)
							OnToolClicked(slot.index);
						break;
					case Kind::Undo:
						if (OnUndoClicked)
							OnUndoClicked();
						break;
					case Kind::Redo:
						if (OnRedoClicked)
							OnRedoClicked();
						break;
				}
				PlayClickSound();
				return true;
			}
			return false;
		}

		void Toolbar::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                    const Vector2& cursorPos, bool menuActive, float screenWidth) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			const std::vector<Slot> slots = Layout(screenWidth);
			previousHoverState.resize(slots.size(), false);

			// Full-width toolbar band background
			OverlayColorNP(renderer, MakeVector4(0.10F, 0.10F, 0.12F, 1.0F));
			OverlayFillRect(renderer, 0.0F, kRibbonH, screenWidth, kToolbarH);

			for (size_t i = 0; i < slots.size(); i++) {
				const Slot& slot = slots[i];
				const ToolbarButton button = ButtonOf(slot);
				if (slot.separatorBefore) {
					float sx = slot.x - (kTbSep + kTbGap) * 0.5F;
					OverlayColorNP(renderer, MakeVector4(0.5F, 0.5F, 0.5F, 0.5F));
					OverlayFillRect(renderer, sx, kTbY + 3.0F, 1.0F, kTbH - 6.0F);
				}

				bool hover = !menuActive && button.enabled &&
				             OverlayInRect(cursorPos, slot.x, kTbY, slot.width, kTbH);
				if (hover && !previousHoverState[i])
					PlayHoverSound();
				previousHoverState[i] = hover;

				// A hot key takes the right edge, so the label moves left to make room.
				Vector2 labelAlign =
				  button.hotKey.empty() ? MakeVector2(0.5F, 0.5F) : MakeVector2(0.0F, 0.5F);
				widgets::PaintButton(renderer, font, MakeVector2(slot.x, kTbY),
				                     MakeVector2(slot.width, kTbH), button.label, labelAlign,
				                     button.hotKey, MakeVector2(1.0F, 0.5F), button.enabled, hover,
				                     false, button.active, 1.0F);
			}
		}
	} // namespace gui
} // namespace spades
