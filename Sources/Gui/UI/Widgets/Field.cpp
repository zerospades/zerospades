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

#include <algorithm>

#include "DrawUtils.h"
#include "Field.h"
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/Cursor.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		namespace ui {
			FieldBase::FieldBase(UIManager* manager) : UIElement(manager) {
				isMouseInteractive = true;
				acceptsFocus = true;

				Handle<client::IImage> img = GetManager().GetRenderer().RegisterImage("Gfx/UI/IBeam.png");
				Handle<Cursor> ibeam =
				    Handle<Cursor>::New(&GetManager(), img.GetPointerOrNull(), MakeVector2(16, 16));
				SetCursor(ibeam.GetPointerOrNull());
			}

			void FieldBase::EraseUndoHistory() {
				history.clear();
				historyPos = 0;
			}

			void FieldBase::SetText(const std::string& value) {
				text = value;
				EraseUndoHistory();
			}

			bool FieldBase::CheckCharType(const std::string& s) const {
				if (denyNonAscii) {
					for (char ch : s) {
						if ((static_cast<unsigned char>(ch) & 0x80) != 0)
							return false;
					}
				}
				return true;
			}

			void FieldBase::OnChanged() {
				if (changed)
					changed(*this);
			}

			int FieldBase::GetSelectionStart() const { return std::min(markPosition, cursorPosition); }
			void FieldBase::SetSelectionStart(int value) { Select(value, GetSelectionEnd() - value); }

			int FieldBase::GetSelectionEnd() const { return std::max(markPosition, cursorPosition); }
			void FieldBase::SetSelectionEnd(int value) {
				Select(GetSelectionStart(), value - GetSelectionStart());
			}

			int FieldBase::GetSelectionLength() const { return GetSelectionEnd() - GetSelectionStart(); }
			void FieldBase::SetSelectionLength(int value) { Select(GetSelectionStart(), value); }

			std::string FieldBase::GetSelectedText() const {
				return text.substr(GetSelectionStart(), GetSelectionLength());
			}

			void FieldBase::SetSelectedText(const std::string& value) {
				if (!CheckCharType(value))
					return;
				FieldCommand cmd;
				cmd.oldString = GetSelectedText();
				if (cmd.oldString == value)
					return; // no change
				cmd.newString = value;
				cmd.index = GetSelectionStart();
				RunFieldCommand(cmd, true, true);
			}

			void FieldBase::RunFieldCommand(const FieldCommand& cmd, bool autoSelect, bool addHistory) {
				size_t tail = std::min(cmd.index + cmd.oldString.size(), text.size());
				text = text.substr(0, cmd.index) + cmd.newString + text.substr(tail);

				if (autoSelect)
					Select(cmd.index, static_cast<int>(cmd.newString.size()));
				if (addHistory) {
					history.resize(historyPos);
					history.push_back(cmd);
					historyPos += 1;
				}
			}

			void FieldBase::UndoFieldCommand(const FieldCommand& cmd, bool autoSelect) {
				size_t tail = std::min(cmd.index + cmd.newString.size(), text.size());
				text = text.substr(0, cmd.index) + cmd.oldString + text.substr(tail);
				if (autoSelect)
					Select(cmd.index, static_cast<int>(cmd.oldString.size()));
			}

			void FieldBase::SetHistoryPos(int index) {
				int p = historyPos;
				while (p < index) {
					RunFieldCommand(history[p], true, false);
					p++;
				}
				while (p > index) {
					p--;
					UndoFieldCommand(history[p], true);
				}
				historyPos = p;
			}

			bool FieldBase::Undo() {
				if (historyPos == 0)
					return false;
				SetHistoryPos(historyPos - 1);
				return true;
			}

			bool FieldBase::Redo() {
				if (historyPos >= static_cast<int>(history.size()))
					return false;
				SetHistoryPos(historyPos + 1);
				return true;
			}

			AABB2 FieldBase::GetTextInputRect() const {
				client::IFont* font = GetFont();
				if (!font)
					return AABB2();
				Vector2 textPos = textOrigin;
				Vector2 siz = size;
				float width = font->Measure(text.substr(0, cursorPosition)).x;
				float fontHeight = font->Measure("A").y;
				return AABB2(textPos.x + width, textPos.y, siz.x - textPos.x - width, fontHeight);
			}

			int FieldBase::PointToCharIndex(float x) const {
				x -= textOrigin.x;
				if (x < 0.0F)
					return 0;
				x /= textScale;
				int len = static_cast<int>(text.size());
				float lastWidth = 0.0F;
				client::IFont* font = GetFont();
				if (!font)
					return 0;
				int idx = 0;
				for (int i = 1; i <= len; i++) {
					int lastIdx = idx;
					idx = GetByteIndexForString(text, 1, idx);
					float width = font->Measure(text.substr(0, idx)).x;
					if (width > x) {
						if (x < (lastWidth + width) * 0.5F)
							return lastIdx;
						else
							return idx;
					}
					lastWidth = width;
					if (idx >= len)
						return len;
				}
				return len;
			}

			int FieldBase::ClampCursorPosition(int pos) const {
				return Clamp(pos, 0, static_cast<int>(text.size()));
			}

			void FieldBase::Select(int start, int length) {
				markPosition = ClampCursorPosition(start);
				cursorPosition = ClampCursorPosition(start + length);
			}

			void FieldBase::SelectAll() { Select(0, static_cast<int>(text.size())); }

			void FieldBase::BackSpace() {
				if (GetSelectionLength() > 0) {
					SetSelectedText("");
				} else {
					int pos = cursorPosition;
					int cIdx = GetCharIndexForString(text, cursorPosition);
					int bIdx = GetByteIndexForString(text, cIdx - 1);
					Select(bIdx, pos - bIdx);
					SetSelectedText("");
				}
				OnChanged();
			}

			void FieldBase::Delete() {
				if (GetSelectionLength() > 0) {
					SetSelectedText("");
				} else if (cursorPosition < static_cast<int>(text.size())) {
					int pos = cursorPosition;
					int cIdx = GetCharIndexForString(text, cursorPosition);
					int bIdx = GetByteIndexForString(text, cIdx + 1);
					Select(bIdx, pos - bIdx);
					SetSelectedText("");
				}
				OnChanged();
			}

			void FieldBase::Insert(const std::string& inText) {
				if (!CheckCharType(inText))
					return;

				std::string t = inText;
				if (removeNewlines)
					t = StripNewlines(t);

				std::string oldText = GetSelectedText();
				SetSelectedText(t);

				// if text overflows, deny the insertion
				if (!FitsInBox(text) || static_cast<int>(text.size()) > maxLength) {
					SetSelectedText(oldText);
					return;
				}

				Select(GetSelectionEnd());
				OnChanged();
			}

			void FieldBase::KeyDown(const std::string& key) {
				UIManager& manager = GetManager();
				if (key == "BackSpace") {
					BackSpace();
				} else if (key == "Delete") {
					Delete();
				} else if (key == "Left") {
					if (manager.isShiftPressed) {
						int cIdx = GetCharIndexForString(text, cursorPosition);
						cursorPosition = ClampCursorPosition(GetByteIndexForString(text, cIdx - 1));
					} else {
						if (GetSelectionLength() == 0) {
							int cIdx = GetCharIndexForString(text, cursorPosition);
							Select(GetByteIndexForString(text, cIdx - 1));
						} else {
							Select(GetSelectionStart());
						}
					}
					return;
				} else if (key == "Right") {
					if (manager.isShiftPressed) {
						int cIdx = GetCharIndexForString(text, cursorPosition);
						cursorPosition = ClampCursorPosition(GetByteIndexForString(text, cIdx + 1));
					} else {
						if (GetSelectionLength() == 0) {
							int cIdx = GetCharIndexForString(text, cursorPosition);
							Select(GetByteIndexForString(text, cIdx + 1));
						} else {
							Select(GetSelectionEnd());
						}
					}
					return;
				}

				if (manager.isControlPressed || manager.isMetaPressed /* for OSX; Cmd + [a-z] */) {
					if (key == "A") {
						SelectAll();
						return;
					} else if (key == "V") {
						Insert(manager.Paste());
						OnChanged();
					} else if (key == "C") {
						manager.Copy(GetSelectedText());
					} else if (key == "X") {
						manager.Copy(GetSelectedText());
						SetSelectedText("");
						OnChanged();
					} else if (key == "Z") {
						if (manager.isShiftPressed) {
							if (Redo())
								OnChanged();
						} else {
							if (Undo())
								OnChanged();
						}
					} else if (key == "W") {
						if (Redo())
							OnChanged();
					}
				}

				manager.ProcessHotKey(key);
			}

			void FieldBase::KeyUp(const std::string&) {}

			void FieldBase::KeyPress(const std::string& inText) {
				UIManager& manager = GetManager();
				if (!(manager.isControlPressed || manager.isMetaPressed))
					Insert(inText);
			}

			void FieldBase::MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left)
					return;
				dragging = true;
				if (GetManager().isShiftPressed)
					MouseMove(clientPosition);
				else
					Select(PointToCharIndex(clientPosition));
			}

			void FieldBase::MouseMove(Vector2 clientPosition) {
				if (dragging)
					cursorPosition = PointToCharIndex(clientPosition);
			}

			void FieldBase::MouseUp(MouseButton button, Vector2 clientPosition) {
				(void)clientPosition;
				if (button != MouseButton::Left)
					return;
				dragging = false;
			}

			bool FieldBase::FitsInBox(const std::string& t) const {
				client::IFont* font = GetFont();
				if (!font)
					return true;
				return font->Measure(t).x * textScale < size.x - textOrigin.x;
			}

			void FieldBase::DrawHighlight(client::IRenderer& r, float x, float y, float w, float h) {
				SetColorNP(r, highlightColor);
				r.DrawImage(nullptr, AABB2(x, y, w, h));
			}

			void FieldBase::DrawBeam(client::IRenderer& r, float x, float y, float h) {
				float pulse = static_cast<float>(static_cast<int>(GetManager().time * 2.0F) & 1);
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, pulse));
				r.DrawImage(nullptr, AABB2(x, y, 1.0F, h));
			}

			void FieldBase::DrawEditingLine(client::IRenderer& r, float x, float y, float w, float h) {
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.3F));
				r.DrawImage(nullptr, AABB2(x, y + h, w, 2.0F));
			}

			std::string FieldBase::TruncateToFit(const std::string& t) const {
				if (FitsInBox(t))
					return t;

				std::string suffix = "..";
				int charLen = GetCharIndexForString(t, static_cast<int>(t.size()));

				while (charLen > 0) {
					charLen--;
					int byteIdx = GetByteIndexForString(t, charLen);
					std::string candidate = t.substr(0, byteIdx) + suffix;
					if (FitsInBox(candidate))
						return candidate;
				}

				return suffix;
			}

			void FieldBase::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				client::IFont* font = GetFont();
				if (!font)
					return;

				Vector2 pos = GetScreenPosition();
				std::string displayText = text;
				Vector2 textPos = textOrigin + pos;

				std::string composition = GetEditingText();
				int editStart = GetTextEditingRangeStart();
				int editLen = GetTextEditingRangeLength();

				int markStart = GetSelectionStart();
				int markEnd = GetSelectionEnd();

				if (composition.size() > 0) {
					SetSelectedText("");
					markStart = GetSelectionStart() + editStart;
					markEnd = markStart + editLen;
					displayText = text.substr(0, GetSelectionStart()) + composition +
					              text.substr(GetSelectionStart());
				}

				if (IsFocused()) {
					float fontHeight = font->Measure("A").y;

					// draw selection
					int start = markStart;
					int end = markEnd;
					if (end == start) {
						float x = font->Measure(displayText.substr(0, start)).x;
						DrawBeam(r, x + textPos.x, textPos.y, fontHeight);
					} else {
						float x1 = font->Measure(displayText.substr(0, start)).x;
						float x2 = font->Measure(displayText.substr(0, end)).x;
						DrawHighlight(r, textPos.x + x1, textPos.y, x2 - x1, fontHeight);
					}

					// draw composition underline
					if (composition.size() > 0) {
						start = GetSelectionStart();
						end = start + static_cast<int>(composition.size());
						float x1 = font->Measure(displayText.substr(0, start)).x;
						float x2 = font->Measure(displayText.substr(0, end)).x;
						DrawEditingLine(r, textPos.x + x1, textPos.y, x2 - x1, fontHeight);
					}
				}

				if (displayText.size() == 0) {
					if (IsEnabled())
						font->Draw(placeholder, textPos, textScale, placeholderColor);
				} else {
					font->Draw(IsFocused() ? displayText : TruncateToFit(displayText), textPos,
					           textScale, IsEnabled() ? textColor : disabledTextColor);
				}

				UIElement::Render();
			}

			Field::Field(UIManager* manager) : FieldBase(manager) {
				textOrigin = MakeVector2(4.0F, 4.0F);
			}

			void Field::MouseEnter() {
				hover = true;
				FieldBase::MouseEnter();
			}

			void Field::MouseLeave() {
				hover = false;
				FieldBase::MouseLeave();
			}

			void Field::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;

				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, IsFocused() ? 0.3F : 0.1F));
				r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

				if (IsFocused())
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				else if (hover)
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
				else
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.06F));
				r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

				FieldBase::Render();
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
