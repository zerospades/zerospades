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
#include <functional>
#include <string>
#include <utility>
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
				/** ID of the link under the mouse (-1 if none); shared so rows can highlight it. */
				int hoverLinkId = -1;

				int GetSelectionStart() const { return std::min(markPosition, cursorPosition); }
				int GetSelectionEnd() const { return std::max(markPosition, cursorPosition); }
			};

			/**
			 * A clickable link: byte range [begin, end) of a line's text and its target URL.
			 * `id` tells apart two occurrences of the same URL. The rows of one wrapped link
			 * share an id, so they are highlighted together.
			 */
			struct TextViewerLink {
				int begin = 0;
				int end = 0;
				std::string url;
				int id = 0;

				TextViewerLink(int begin, int end, const std::string& url, int id = 0)
				    : begin(begin), end(end), url(url), id(id) {}
			};

			/**
			 * A run of a row's text drawn in a single style, with its horizontal extent
			 * measured once when the row is built.
			 */
			struct TextViewerSegment {
				enum class Kind { Plain, Code, Link };

				/** Byte range [begin, end) of the row's text. */
				int begin = 0;
				int end = 0;
				/** Horizontal extent of the run, in pixels from the row's left edge. */
				float x1 = 0.0F;
				float x2 = 0.0F;
				Kind kind = Kind::Plain;
				/** Index into `TextViewerItem::links` when `kind` is `Link`, otherwise -1. */
				int linkIndex = -1;
			};

			/** One wrapped line of a `TextViewer`, with its content start index. */
			struct TextViewerItem {
				std::string text;
				Vector4 color;
				int index = 0;
				/** Byte ranges [begin, end) of `text` that are drawn as inline code. */
				std::vector<std::pair<int, int>> codeRanges;
				/** Links found in `text`; a wrapped link keeps its full URL on every row. */
				std::vector<TextViewerLink> links;
				/** Styled runs of `text`; empty when the row has no code and no links. */
				std::vector<TextViewerSegment> segments;
				/** Height of the row's text; set together with `segments`. */
				float textHeight = 0.0F;

				TextViewerItem(const std::string& text, Vector4 color, int index,
				               std::vector<std::pair<int, int>> codeRanges = {},
				               std::vector<TextViewerLink> links = {})
				    : text(text),
				      color(color),
				      index(index),
				      codeRanges(std::move(codeRanges)),
				      links(std::move(links)) {}

				/** Measures `font` once to fill `segments` and `textHeight`. */
				void BuildSegments(client::IFont* font);
			};

			/** The view element rendering a single `TextViewerItem` row. */
			class TextViewerItemUI : public UIElement {
				TextViewerItem item;
				Handle<TextViewerSelectionState> selection;

				void DrawHighlight(client::IRenderer& r, float x, float y, float w, float h);

			public:
				TextViewerItemUI(UIManager* manager, const TextViewerItem& item,
				                 TextViewerSelectionState* selection);
				void Render() override;
			};

			/** `ListViewModel` that word-wraps text into a scrollable set of rows. */
			class TextViewerModel : public ListViewModel {
				bool parseCode;
				bool parseLinks;
				int nextLinkId = 0;

				void AddLineInternal(const std::string& text, Vector4 color,
				                     const std::vector<std::pair<int, int>>& codeRanges,
				                     const std::vector<TextViewerLink>& links);

			public:
				UIManager* manager; // weak
				std::vector<TextViewerItem> lines;
				client::IFont* font; // weak
				float width;
				Handle<TextViewerSelectionState> selection;
				int contentStart = 0;
				int contentEnd = 0;

				TextViewerModel(UIManager* manager, const std::string& text, client::IFont* font,
				                float width, TextViewerSelectionState* selection,
				                bool parseCode = false, bool parseLinks = false);

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

				// link under the mouse at press time, -1 if none
				int pressedLinkId = -1;
				std::string pressedLinkUrl;

				const TextViewerLink* FindLinkAt(Vector2 clientPosition) const;

				int PointToCharIndex(Vector2 clientPosition) const;
				void ApplyTextCursor();
				void UpdateHover(Vector2 clientPosition);

				/** Re-evaluates the hovered link for a mouse that did not move but whose content did. */
				void RefreshHover();

			public:
				/** Maximum number of lines kept; `0` means unlimited. */
				int maxNumLines = 0;

				/** When true, `text` between backticks is drawn highlighted. */
				bool parseInlineCode = false;

				/**
				 * When true, `http://` and `https://` URLs are drawn as links and a click on
				 * one calls `linkActivated`. Without a handler the links are drawn but inert.
				 */
				bool parseLinks = false;

				/** Called with the URL of a clicked link. */
				std::function<void(const std::string&)> linkActivated;

				TextViewer(UIManager* manager);

				const std::string& GetText() const { return text; }
				void SetText(const std::string& value);

				void MouseWheel(float delta) override;
				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void MouseMove(Vector2 clientPosition) override;
				void MouseUp(MouseButton button, Vector2 clientPosition) override;
				void MouseEnter() override;
				void MouseLeave() override;
				void MouseCaptureLost() override;
				void KeyDown(const std::string& key) override;

				std::string GetSelectedText() const;

				/** Returns the URL of the link at `clientPosition`, or an empty string. */
				std::string GetLinkAt(Vector2 clientPosition) const;

				void AddLine(const std::string& line, bool autoscroll = false,
				             Vector4 color = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
