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
			namespace {
				bool IsContinuationByte(char c) {
					return (static_cast<unsigned char>(c) & 0xC0) == 0x80;
				}

				int NextChar(const std::string& s, int i) {
					int len = static_cast<int>(s.size());
					return std::min(len, GetByteIndexForString(s, 1, i));
				}

				int PrevChar(const std::string& s, int i) {
					i = std::min(i, static_cast<int>(s.size()));
					if (i <= 0)
						return 0;
					i--;
					while (i > 0 && IsContinuationByte(s[i]))
						i--;
					return i;
				}
			} // namespace

			FieldBase::FieldBase(UIManager* manager) : UIElement(manager) {
				isMouseInteractive = true;
				acceptsFocus = true;

				Handle<client::IImage> img = GetManager().GetRenderer().RegisterImage("Gfx/UI/IBeam.png");
				Handle<Cursor> ibeam =
				  Handle<Cursor>::New(GetManager().GetRenderer(), img.GetPointerOrNull(),
				                      MakeVector2(16, 16));
				SetCursor(ibeam.GetPointerOrNull());
			}

			void FieldBase::EraseUndoHistory() {
				history.clear();
				historyPos = 0;
			}

			void FieldBase::SetText(const std::string& value) {
				text = value;
				scrollIndex = 0;
				// Clamp the cursor/mark to the new text's length. Without this, a
				// shorter (or empty) text leaves a stale position that later causes
				// std::string::substr() to throw std::out_of_range on the next edit.
				markPosition = ClampCursorPosition(markPosition);
				cursorPosition = ClampCursorPosition(cursorPosition);
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
				int scroll = std::min(scrollIndex, static_cast<int>(text.size()));
				float width = leadingOffset + MeasureRange(text, scroll, cursorPosition);
				float fontHeight = font->Measure("A").y * textScale;
				return AABB2(textPos.x + width, textPos.y, siz.x - textPos.x - width, fontHeight);
			}

			int FieldBase::PointToCharIndex(float x) const {
				client::IFont* font = GetFont();
				if (!font)
					return 0;
				int len = static_cast<int>(text.size());
				int scroll = std::min(scrollIndex, len);

				// Positions are measured from the first visible character.
				x -= textOrigin.x + leadingOffset;
				if (x < 0.0F) // left of the visible text: step back so drags scroll
					return PrevChar(text, scroll);

				float lastWidth = 0.0F;
				int idx = scroll;
				while (idx < len) {
					int lastIdx = idx;
					idx = NextChar(text, idx);
					float width = MeasureRange(text, scroll, idx);
					if (width > x)
						return x < (lastWidth + width) * 0.5F ? lastIdx : idx;
					lastWidth = width;
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

				// Text wider than the box scrolls; only the length is limited.
				if (static_cast<int>(text.size()) > maxLength) {
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
						std::string clipboard = manager.Paste();
						if (!clipboard.empty())
							Insert(clipboard);
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

			float FieldBase::MeasureRange(const std::string& s, int from, int to) const {
				client::IFont* font = GetFont();
				if (!font || to <= from)
					return 0.0F;
				return font->Measure(s.substr(from, to - from)).x * textScale;
			}

			float FieldBase::GetVisibleTextWidth() const {
				return std::max(0.0F, size.x - textOrigin.x * 2.0F);
			}

			int FieldBase::UpdateScroll(const std::string& s, int caret) {
				int len = static_cast<int>(s.size());
				caret = Clamp(caret, 0, len);
				float avail = GetVisibleTextWidth();

				int scroll = Clamp(scrollIndex, 0, len);
				while (scroll > 0 && scroll < len && IsContinuationByte(s[scroll]))
					scroll--;

				// Bring the caret into view...
				if (caret < scroll)
					scroll = caret;
				while (scroll < caret && MeasureRange(s, scroll, caret) > avail)
					scroll = NextChar(s, scroll);
				// ...and don't leave room unused at the end (e.g. after deleting).
				while (scroll > 0) {
					int prev = PrevChar(s, scroll);
					if (MeasureRange(s, prev, len) > avail)
						break;
					scroll = prev;
				}
				scrollIndex = scroll;

				int end = scroll;
				while (end < len) {
					int next = NextChar(s, end);
					if (MeasureRange(s, scroll, next) > avail)
						break;
					end = next;
				}
				return end;
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
				client::IFont* font = GetFont();
				if (!font)
					return t;
				return ElideText(*font, t, size.x - textOrigin.x, textScale, TextElision::End);
			}

			void FieldBase::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				client::IFont* font = GetFont();
				if (!font)
					return;

				Vector2 pos = GetScreenPosition();
				std::string displayText = text;
				Vector2 textPos = textOrigin + pos;
				Vector4 color = IsEnabled() ? textColor : disabledTextColor;

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
					leadingOffset = 0.0F;
					int caret = composition.empty() ? cursorPosition : markEnd;
					int visibleEnd = UpdateScroll(displayText, caret);
					float fontHeight = font->Measure("A").y * textScale;

					// Screen x of byte index `i`, clamped to the visible span.
					auto xAt = [&](int i) {
						i = Clamp(i, scrollIndex, visibleEnd);
						return textPos.x + MeasureRange(displayText, scrollIndex, i);
					};

					// draw selection
					if (markStart == markEnd) {
						if (markStart >= scrollIndex && markStart <= visibleEnd)
							DrawBeam(r, xAt(markStart), textPos.y, fontHeight);
					} else {
						float x1 = xAt(markStart);
						float x2 = xAt(markEnd);
						if (x2 > x1)
							DrawHighlight(r, x1, textPos.y, x2 - x1, fontHeight);
					}

					// draw composition underline
					if (composition.size() > 0) {
						int start = GetSelectionStart();
						int end = start + static_cast<int>(composition.size());
						float x1 = xAt(start);
						float x2 = xAt(end);
						if (x2 > x1)
							DrawEditingLine(r, x1, textPos.y, x2 - x1, fontHeight);
					}

					if (displayText.empty()) {
						if (IsEnabled())
							font->Draw(placeholder, textPos, textScale, placeholderColor);
					} else {
						font->Draw(displayText.substr(scrollIndex, visibleEnd - scrollIndex),
						           textPos, textScale, color);
					}
				} else if (displayText.empty()) {
					scrollIndex = 0;
					leadingOffset = 0.0F;
					if (IsEnabled())
						font->Draw(placeholder, textPos, textScale, placeholderColor);
				} else if (elision == FieldElision::Start && !FitsInBox(displayText)) {
					// Keep the end visible: "..<tail>". `scrollIndex` marks where the
					// tail starts so a click maps to the character under the cursor.
					std::string prefix = "..";
					int len = static_cast<int>(displayText.size());
					int start = NextChar(displayText, 0);
					while (start < len && !FitsInBox(prefix + displayText.substr(start)))
						start = NextChar(displayText, start);
					scrollIndex = start;
					leadingOffset = font->Measure(prefix).x * textScale;
					font->Draw(prefix + displayText.substr(start), textPos, textScale, color);
				} else {
					scrollIndex = 0;
					leadingOffset = 0.0F;
					font->Draw(TruncateToFit(displayText), textPos, textScale, color);
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
