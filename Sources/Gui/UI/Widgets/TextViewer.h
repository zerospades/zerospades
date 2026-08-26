/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace client {
		class IFont;
		class IImage;
		class IRenderer;
	}
	namespace gui {
		namespace ui {
			/** Shared text selection anchor/caret for a `TextViewer`. */
			class TextViewerSelectionState : public RefCountedObject {
			public:
				UIElement* focusElement = nullptr; // weak
				int markPosition = 0;
				int cursorPosition = 0;

				int GetSelectionStart() const { return std::min(markPosition, cursorPosition); }
				int GetSelectionEnd() const { return std::max(markPosition, cursorPosition); }
			};

			/** One wrapped line of a `TextViewer`, with its content start index. */
			struct TextViewerItem {
				std::string text;
				Vector4 color;
				int index = 0;

				TextViewerItem(const std::string& text, Vector4 color, int index)
				    : text(text), color(color), index(index) {}
			};

			/** The view element rendering a single `TextViewerItem` row. */
			class TextViewerItemUI : public UIElement {
				std::string text;
				Vector4 textColor;
				int index;
				Handle<TextViewerSelectionState> selection;

				void DrawHighlight(client::IRenderer& r, float x, float y, float w, float h);

			public:
				TextViewerItemUI(UIManager* manager, const TextViewerItem& item,
				                 TextViewerSelectionState* selection);
				void Render() override;
			};

			/** `ListViewModel` that word-wraps text into a scrollable set of rows. */
			class TextViewerModel : public ListViewModel {
			public:
				UIManager* manager; // weak
				std::vector<TextViewerItem> lines;
				client::IFont* font; // weak
				float width;
				Handle<TextViewerSelectionState> selection;
				int contentStart = 0;
				int contentEnd = 0;

				TextViewerModel(UIManager* manager, const std::string& text, client::IFont* font,
				                float width, TextViewerSelectionState* selection);

				void AddLine(const std::string& text, Vector4 color);
				void RemoveFirstLines(unsigned int numLines);

				int GetNumRows() override { return static_cast<int>(lines.size()); }
				Handle<UIElement> CreateElement(int row) override;
			};

			/** A read-only, selectable, word-wrapped, scrollable text view. */
			class TextViewer : public ListViewBase {
				std::string text;
				Handle<TextViewerModel> textmodel;
				Handle<TextViewerSelectionState> selection;
				bool dragging = false;
				Handle<client::IImage> image;

				int PointToCharIndex(Vector2 clientPosition) const;

			public:
				/** Maximum number of lines kept; `0` means unlimited. */
				int maxNumLines = 0;

				TextViewer(UIManager* manager);

				const std::string& GetText() const { return text; }
				void SetText(const std::string& value);

				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void MouseMove(Vector2 clientPosition) override;
				void MouseUp(MouseButton button, Vector2 clientPosition) override;
				void MouseEnter() override;
				void MouseLeave() override;
				void KeyDown(const std::string& key) override;

				std::string GetSelectedText() const;

				void AddLine(const std::string& line, bool autoscroll = false,
				             Vector4 color = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
