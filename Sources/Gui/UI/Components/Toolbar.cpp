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

		void Toolbar::SetModeButtons(const std::vector<std::string>& labels) {
			modeButtons = labels;
		}

		void Toolbar::SetActiveModeButton(int index) {
			activeModeButton = index;
		}

		void Toolbar::SetToolButtons(const std::vector<ToolbarButton>& buttons) {
			toolButtons = buttons;
		}

		void Toolbar::SetUndoButton(bool enabled) { undoEnabled = enabled; }

		void Toolbar::SetRedoButton(bool enabled) { redoEnabled = enabled; }

		float Toolbar::ToolbarX(int slot) const {
			float x = kTbX0 + float(slot) * (kTbBtn + kTbGap);
			int toolCount = int(toolButtons.size());
			int toolStartSlot = int(modeButtons.size());
			if (slot >= toolStartSlot && toolCount > 0)
				x += kTbSep;
			return x;
		}

		float Toolbar::UndoButtonX(float sw, bool redo) const {
			float undoX = sw - 12.0F - 2.0F * kUndoBtnW - kTbGap;
			return redo ? undoX + kUndoBtnW + kTbGap : undoX;
		}

		bool Toolbar::InRect(const Vector2& p, float x, float y, float w, float h) const {
			return OverlayInRect(p, x, y, w, h);
		}

		Toolbar::ClickResult Toolbar::HitTest(const Vector2& p, float screenWidth) {
			for (int i = 0; i < int(modeButtons.size()); i++) {
				if (InRect(p, ToolbarX(i), kTbY, kTbBtn, kTbH))
					return {ClickType::Mode, i};
			}
			int toolStartSlot = int(modeButtons.size());
			for (int i = 0; i < int(toolButtons.size()); i++) {
				if (InRect(p, ToolbarX(toolStartSlot + i), kTbY, kTbBtn, kTbH))
					return {ClickType::Tool, i};
			}
			// Undo / Redo buttons on the right edge
			float undoX = UndoButtonX(screenWidth, false);
			float redoX = UndoButtonX(screenWidth, true);
			if (InRect(p, undoX, kTbY, kUndoBtnW, kTbH))
				return {ClickType::Undo, -1};
			if (InRect(p, redoX, kTbY, kUndoBtnW, kTbH))
				return {ClickType::Redo, -1};
			return {};
		}

		void Toolbar::Draw(client::IRenderer& renderer, client::FontManager& fontManager,
		                    const Vector2& cursorPos, bool menuActive, float screenWidth) {
			client::IFont& font = fontManager.GetSmallGuiFont();
			float s = 1.0F;
			int toolCount = int(toolButtons.size());
			int totalButtons = int(modeButtons.size()) + toolCount;

			// Ensure previousHoverState is sized correctly
			if (previousHoverState.size() != (size_t)totalButtons)
				previousHoverState.resize(totalButtons, false);

			// Full-width toolbar band background
			OverlayColorNP(renderer, MakeVector4(0.10F, 0.10F, 0.12F, 1.0F));
			OverlayFillRect(renderer, 0.0F, kRibbonH, screenWidth, kToolbarH);

			// Helper to paint a button and track hover state for audio
			auto button = [&](float x, const char* label, bool active, bool enabled, int index) {
				bool hover = !menuActive && InRect(cursorPos, x, kTbY, kTbBtn, kTbH);
				// Play sound on hover transition (false → true)
				if (hover && index >= 0 && index < (int)previousHoverState.size() && !previousHoverState[index])
					PlayHoverSound();
				if (index >= 0 && index < (int)previousHoverState.size())
					previousHoverState[index] = hover;
				widgets::PaintButton(renderer, font, MakeVector2(x, kTbY),
				                     MakeVector2(kTbBtn, kTbH), label, MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), enabled, hover, false, active, s);
			};

			// Draw mode buttons (only Edit mode is available for now)
			for (int i = 0; i < int(modeButtons.size()); i++) {
				bool active = (i == activeModeButton);
				bool enabled = (i == 1); // Only Edit mode (index 1) is available
				button(ToolbarX(i), modeButtons[i].c_str(), active, enabled, i);
			}

			// Draw separator and tool buttons
			if (toolCount > 0) {
				int toolStartSlot = int(modeButtons.size());
				float sx = ToolbarX(toolStartSlot) - kTbSep * 0.5F - kTbGap;
				OverlayColorNP(renderer, MakeVector4(0.5F, 0.5F, 0.5F, 0.5F));
				OverlayFillRect(renderer, sx, kTbY + 3.0F, 1.0F, kTbH - 6.0F);
				for (int i = 0; i < toolCount; i++) {
					const ToolbarButton& btn = toolButtons[i];
					button(ToolbarX(toolStartSlot + i), btn.label.c_str(), btn.active, btn.enabled,
					       toolStartSlot + i);
				}
			}

			// Undo / Redo buttons on the right edge
			auto urButton = [&](float x, const char* label, bool enabled, bool& prevHover) {
				bool hover = !menuActive && InRect(cursorPos, x, kTbY, kUndoBtnW, kTbH);
				if (hover && !prevHover)
					PlayHoverSound();
				prevHover = hover;
				widgets::PaintButton(renderer, font, MakeVector2(x, kTbY),
				                     MakeVector2(kUndoBtnW, kTbH), label, MakeVector2(0.5F, 0.5F), "",
				                     MakeVector2(1.0F, 0.5F), enabled, hover, false, false, s);
			};
			urButton(UndoButtonX(screenWidth, false), "Undo", undoEnabled, previousUndoHover);
			urButton(UndoButtonX(screenWidth, true), "Redo", redoEnabled, previousRedoHover);
		}
	} // namespace gui
} // namespace spades
