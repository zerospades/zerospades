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

#include <string>
#include <vector>

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace client {
		class IRenderer;
	}
	namespace gui {
		namespace ui {
			/** A single reversible edit applied to a `FieldBase`. */
			struct FieldCommand {
				int index = 0;
				std::string oldString;
				std::string newString;
			};

			/**
			 * A single-line editable text field with selection, clipboard, IME and
			 * undo/redo support. `Field` adds the default framed background.
			 */
			class FieldBase : public UIElement {
				std::string text;
				std::vector<FieldCommand> history;
				int historyPos = 0; // index at which to insert the next command

				bool CheckCharType(const std::string& s) const;
				void RunFieldCommand(const FieldCommand& cmd, bool autoSelect, bool addHistory);
				void UndoFieldCommand(const FieldCommand& cmd, bool autoSelect);
				void SetHistoryPos(int index);
				void EraseUndoHistory();

				int PointToCharIndex(float x) const;
				int ClampCursorPosition(int pos) const;
				bool FitsInBox(const std::string& text) const;
				std::string TruncateToFit(const std::string& text) const;

				void DrawHighlight(client::IRenderer& r, float x, float y, float w, float h);
				void DrawBeam(client::IRenderer& r, float x, float y, float h);
				void DrawEditingLine(client::IRenderer& r, float x, float y, float w, float h);

			public:
				bool dragging = false;
				EventHandler changed;
				std::string placeholder;
				int markPosition = 0;
				int cursorPosition = 0;
				int maxLength = 255;
				bool denyNonAscii = false;
				bool removeNewlines = false;

				Vector4 textColor = MakeVector4(1, 1, 1, 1);
				Vector4 disabledTextColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.3F);
				Vector4 placeholderColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
				Vector4 highlightColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.3F);

				Vector2 textOrigin = MakeVector2(0, 0);
				float textScale = 1.0F;

				FieldBase(UIManager* manager);

				const std::string& GetText() const { return text; }
				void SetText(const std::string& value);

				virtual void OnChanged();

				int GetSelectionStart() const;
				void SetSelectionStart(int value);
				int GetSelectionEnd() const;
				void SetSelectionEnd(int value);
				int GetSelectionLength() const;
				void SetSelectionLength(int value);

				std::string GetSelectedText() const;
				void SetSelectedText(const std::string& value);

				bool Undo();
				bool Redo();

				AABB2 GetTextInputRect() const override;

				int PointToCharIndex(Vector2 pt) const { return PointToCharIndex(pt.x); }

				void Select(int start, int length = 0);
				void SelectAll();
				void BackSpace();
				void Delete();
				void Insert(const std::string& text);

				void KeyDown(const std::string& key) override;
				void KeyUp(const std::string& key) override;
				void KeyPress(const std::string& text) override;
				void MouseDown(MouseButton button, Vector2 clientPosition) override;
				void MouseMove(Vector2 clientPosition) override;
				void MouseUp(MouseButton button, Vector2 clientPosition) override;

				void Render() override;
			};

			/** A `FieldBase` with the standard framed, hover-highlighted background. */
			class Field : public FieldBase {
				bool hover = false;

			public:
				Field(UIManager* manager);

				void MouseEnter() override;
				void MouseLeave() override;
				void Render() override;
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
