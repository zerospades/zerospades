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

#include "BarLayout.h"

#include <algorithm>

#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>
#include <Client/IRenderer.h>
#include <Client/Fonts.h>
#include <Core/Math.h>

namespace spades {
	namespace gui {
		// Layout constants, beside the bars' shared ones
		using namespace barlayout;
		static const float kTbBtn = 84.0F;
		// In a window too narrow for them the tool buttons narrow, down
		// to this, rather than run under Undo and Redo; one narrower than
		// kTbBtnWithKey gives its hot key's place to its label.
		static const float kTbBtnMin = 52.0F;
		static const float kTbBtnWithKey = 72.0F;
		static const float kUndoBtnW = 50.0F;
		static const float kSwatchW = 40.0F;
		static const float kTbY = kRibbonH + (kToolbarH - kTbH) * 0.5F;

		Toolbar::Toolbar(client::IAudioDevice* audioDevice) : sounds(audioDevice) {}

		void Toolbar::SetToolButtons(const std::vector<ToolbarButton>& buttons) {
			toolButtons = buttons;
		}

		void Toolbar::SetUndoButton(bool enabled) { undoEnabled = enabled; }

		void Toolbar::SetRedoButton(bool enabled) { redoEnabled = enabled; }

		void Toolbar::SetColorSwatch(uint32_t color, bool open) {
			hasSwatch = true;
			swatchColor = color;
			swatchOpen = open;
		}

		std::vector<Toolbar::Slot> Toolbar::Layout(float screenWidth) const {
			// Undo / Redo on the right edge
			const float undoX = screenWidth - 12.0F - 2.0F * kUndoBtnW - kTbGap;

			// What the buttons do not take: the separators and the swatch. The
			// first tool starts the bar, every later one is set apart from its
			// predecessor when it starts another group, and the swatch, which
			// belongs to no one tool, from them all.
			auto separatedTool = [&](int i) {
				return i > 0 && toolButtons[i].group != toolButtons[i - 1].group;
			};
			const bool swatchSeparated = !toolButtons.empty();
			float fixed = 0.0F;
			for (int i = 0; i < int(toolButtons.size()); i++)
				fixed += separatedTool(i) ? kTbSep : 0.0F;
			if (hasSwatch)
				fixed += (swatchSeparated ? kTbSep : 0.0F) + kSwatchW + kTbGap;

			// As wide as they are meant to be where there is room, and sharing
			// what room there is where there is not.
			const int buttons = int(toolButtons.size());
			float width = kTbBtn;
			if (buttons > 0) {
				const float room = undoX - kTbSep - kTbX0 - fixed;
				width = std::max(kTbBtnMin, std::min(kTbBtn, room / float(buttons) - kTbGap));
			}

			std::vector<Slot> slots;
			float x = kTbX0;
			for (int i = 0; i < int(toolButtons.size()); i++) {
				const bool separator = separatedTool(i);
				if (separator)
					x += kTbSep;
				slots.push_back({Kind::Tool, i, x, width, separator});
				x += width + kTbGap;
			}
			if (hasSwatch) {
				if (swatchSeparated)
					x += kTbSep;
				slots.push_back({Kind::ColorSwatch, -1, x, kSwatchW, swatchSeparated});
				x += kSwatchW + kTbGap;
			}
			slots.push_back({Kind::Undo, -1, undoX, kUndoBtnW, false});
			slots.push_back({Kind::Redo, -1, undoX + kUndoBtnW + kTbGap, kUndoBtnW, false});
			return slots;
		}

		Toolbar::ToolbarButton Toolbar::ButtonOf(const Slot& slot) const {
			if (slot.kind == Kind::Tool)
				return toolButtons[slot.index];
			if (slot.kind == Kind::ColorSwatch) {
				ToolbarButton swatch;
				swatch.active = swatchOpen;
				return swatch;
			}
			const bool undo = slot.kind == Kind::Undo;
			ToolbarButton history;
			history.label = undo ? "Undo" : "Redo";
			history.enabled = undo ? undoEnabled : redoEnabled;
			return history;
		}

		void Toolbar::DrawColorSwatch(client::IRenderer& renderer, const Slot& slot,
		                              bool hover) const {
			OverlayColorNP(renderer, ConvertColorRGBA(IntVectorFromColor(swatchColor)));
			OverlayFillRect(renderer, slot.x, kTbY, slot.width, kTbH);
			// A brighter, heavier frame while its picker is open, as a latched
			// button reads; a lighter one on hover.
			const float frame = swatchOpen ? 2.0F : 1.0F;
			const float bright = swatchOpen ? 1.0F : (hover ? 0.9F : 0.7F);
			OverlayStrokeRect(renderer, slot.x, kTbY, slot.width, kTbH, frame,
			                  MakeVector4(bright, bright, bright, 0.9F));
		}

		bool Toolbar::Click(const Vector2& p, float screenWidth) {
			for (const Slot& slot : Layout(screenWidth)) {
				if (!OverlayInRect(p, slot.x, kTbY, slot.width, kTbH))
					continue;
				if (!ButtonOf(slot).enabled)
					return false; // greyed out: the click does nothing
				// Ids are copied: a handler may change the buttons they belong to.
				switch (slot.kind) {
					case Kind::Tool:
						if (OnToolClicked) {
							const std::string id = toolButtons[slot.index].id;
							OnToolClicked(id);
						}
						break;
					case Kind::ColorSwatch:
						if (OnColorSwatchClicked)
							OnColorSwatchClicked();
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
				sounds.Activate();
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
				previousHoverState[i] = sounds.HoverEdge(hover, previousHoverState[i]);

				if (slot.kind == Kind::ColorSwatch) {
					DrawColorSwatch(renderer, slot, hover);
					continue;
				}

				// A hot key takes the right edge, so the label moves left to make
				// room; a button narrowed for a small window shows the label alone.
				const bool isHistory = slot.kind == Kind::Undo || slot.kind == Kind::Redo;
				const std::string hotKey =
				  (isHistory || slot.width >= kTbBtnWithKey) ? button.hotKey : std::string();
				Vector2 labelAlign =
				  hotKey.empty() ? MakeVector2(0.5F, 0.5F) : MakeVector2(0.0F, 0.5F);
				widgets::PaintButton(renderer, font, MakeVector2(slot.x, kTbY),
				                     MakeVector2(slot.width, kTbH), button.label, labelAlign,
				                     hotKey, MakeVector2(1.0F, 0.5F), button.enabled, hover,
				                     false, button.active, 1.0F);
			}
		}
	} // namespace gui
} // namespace spades
