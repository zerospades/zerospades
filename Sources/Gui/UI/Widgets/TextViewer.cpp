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
#include "ScrollBar.h"
#include "TextViewer.h"
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
				std::vector<std::string> SplitLines(const std::string& s) {
					std::vector<std::string> out;
					size_t start = 0;
					while (true) {
						size_t nl = s.find('\n', start);
						if (nl == std::string::npos) {
							out.push_back(s.substr(start));
							break;
						}
						out.push_back(s.substr(start, nl - start));
						start = nl + 1;
					}
					return out;
				}
			} // namespace

			// -- TextViewerItemUI --

			TextViewerItemUI::TextViewerItemUI(UIManager* manager, const TextViewerItem& item,
			                                   TextViewerSelectionState* selection)
			    : UIElement(manager),
			      text(item.text),
			      textColor(item.color),
			      index(item.index),
			      selection(selection) {}

			void TextViewerItemUI::DrawHighlight(client::IRenderer& r, float x, float y, float w,
			                                     float h) {
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				r.DrawImage(nullptr, AABB2(x, y, w, h));
			}

			void TextViewerItemUI::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;
				float textScale = 1.0F;
				client::IFont* font = GetFont();
				if (!font)
					return;

				// Draw selection
				if (selection->focusElement && selection->focusElement->IsFocused()) {
					int start = selection->GetSelectionStart() - index;
					int end = selection->GetSelectionEnd() - index;
					if (start < 0)
						start = 0;
					if (end > static_cast<int>(text.size()) + 1)
						end = static_cast<int>(text.size()) + 1;
					if (end > start) {
						float x1 = font->Measure(text.substr(0, start)).x;
						float x2 = font->Measure(text.substr(0, end)).x;
						if (end == static_cast<int>(text.size()) + 1)
							x2 = sz.x;

						DrawHighlight(r, pos.x + x1, pos.y, x2 - x1, sz.y);
					}
				}

				if (text.size() > 0) {
					Vector2 txtSize = font->Measure(text) * textScale;
					Vector2 txtPos = pos + (sz - txtSize) * MakeVector2(0.0F, 0.0F);

					font->Draw(text, txtPos, textScale, textColor);
				}
			}

			// -- TextViewerModel --

			TextViewerModel::TextViewerModel(UIManager* manager, const std::string& text,
			                                 client::IFont* font, float width,
			                                 TextViewerSelectionState* selection)
			    : manager(manager), font(font), width(width), selection(selection) {
				std::vector<std::string> textLines = SplitLines(text);
				for (const std::string& line : textLines)
					AddLine(line, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			}

			void TextViewerModel::AddLine(const std::string& text, Vector4 color) {
				int startPos = 0;
				if (font->Measure(text).x <= width) {
					lines.push_back(TextViewerItem(text, color, contentEnd));
					contentEnd += static_cast<int>(text.size()) + 1;
					return;
				}

				int pos = 0;
				int len = static_cast<int>(text.size());
				bool charMode = false;
				while (startPos < len) {
					int nextPos = pos + 1;
					if (charMode) {
						// skip to the next UTF-8 character boundary
						while (nextPos < len &&
						       (static_cast<unsigned char>(text[nextPos]) & 0x80) != 0 &&
						       (static_cast<unsigned char>(text[nextPos]) & 0xc0) != 0xc0)
							nextPos++;
					} else {
						while (nextPos < len && text[nextPos] != 0x20)
							nextPos++;
					}
					if (font->Measure(text.substr(startPos, nextPos - startPos)).x > width) {
						if (pos == startPos) {
							if (charMode) {
								pos = nextPos;
							} else {
								charMode = true;
							}
							continue;
						} else {
							lines.push_back(
							    TextViewerItem(text.substr(startPos, pos - startPos), color, contentEnd));
							contentEnd += pos - startPos;
							startPos = pos;
							while (startPos < len && text[startPos] == 0x20)
								startPos++;
							pos = startPos;
							charMode = false;
							continue;
						}
					} else {
						pos = nextPos;
						if (nextPos >= len) {
							lines.push_back(TextViewerItem(text.substr(startPos, nextPos - startPos),
							                               color, contentEnd));
							contentEnd += nextPos - startPos + 1;
							break;
						}
					}
				}
			}

			void TextViewerModel::RemoveFirstLines(unsigned int numLines) {
				int removedLength;
				if (lines.size() > numLines)
					removedLength = lines[numLines].index - contentStart;
				else
					removedLength = contentEnd - contentStart;

				lines.erase(lines.begin(),
				            lines.begin() + std::min<size_t>(numLines, lines.size()));
				contentStart += removedLength;

				selection->markPosition = std::max(selection->markPosition, contentStart);
				selection->cursorPosition = std::max(selection->cursorPosition, contentStart);
			}

			Handle<UIElement> TextViewerModel::CreateElement(int row) {
				return Handle<TextViewerItemUI>::New(manager, lines[row],
				                                     selection.GetPointerOrNull())
				    .Cast<UIElement>();
			}

			// -- TextViewer --

			TextViewer::TextViewer(UIManager* manager) : ListViewBase(manager) {
				selection = Handle<TextViewerSelectionState>::New();
				selection->focusElement = this;
				acceptsFocus = true;
				isMouseInteractive = true;
				image = GetManager().GetRenderer().RegisterImage("Gfx/UI/IBeam.png");
			}

			void TextViewer::SetText(const std::string& value) {
				text = value;
				textmodel = Handle<TextViewerModel>::New(&GetManager(), text, GetFont(),
				                                         GetItemWidth(), selection.GetPointerOrNull());
				SetModel(textmodel.GetPointerOrNull());
				selection->markPosition = 0;
				selection->cursorPosition = 0;
			}

			int TextViewer::PointToCharIndex(Vector2 clientPosition) const {
				if (!textmodel)
					return 0;

				int line = static_cast<int>(clientPosition.y / rowHeight) + GetTopRowIndex();
				if (line < 0)
					return textmodel->contentStart;
				if (line >= static_cast<int>(textmodel->lines.size()))
					return textmodel->contentEnd;

				float x = clientPosition.x;
				const std::string& lineText = textmodel->lines[line].text;
				int lineStartIndex = textmodel->lines[line].index;
				if (x < 0.0F)
					return lineStartIndex;
				int len = static_cast<int>(lineText.size());
				float lastWidth = 0.0F;
				client::IFont* font = GetFont();
				if (!font)
					return lineStartIndex;
				int idx = 0;
				for (int i = 1; i <= len; i++) {
					int lastIdx = idx;
					idx = GetByteIndexForString(lineText, 1, idx);
					float width = font->Measure(lineText.substr(0, idx)).x;
					if (width > x) {
						if (x < (lastWidth + width) * 0.5F)
							return lastIdx + lineStartIndex;
						else
							return idx + lineStartIndex;
					}
					lastWidth = width;
					if (idx >= len)
						return len + lineStartIndex;
				}
				return len + lineStartIndex;
			}

			void TextViewer::MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left)
					return;
				dragging = true;
				if (GetManager().isShiftPressed) {
					MouseMove(clientPosition);
				} else {
					selection->markPosition = selection->cursorPosition =
					    PointToCharIndex(clientPosition);
				}
			}

			void TextViewer::MouseMove(Vector2 clientPosition) {
				if (dragging)
					selection->cursorPosition = PointToCharIndex(clientPosition);
			}

			void TextViewer::MouseUp(MouseButton button, Vector2 clientPosition) {
				(void)clientPosition;
				if (button != MouseButton::Left)
					return;
				dragging = false;
			}

			void TextViewer::MouseEnter() {
				if (textmodel) {
					Handle<Cursor> ibeam =
					  Handle<Cursor>::New(GetManager().GetRenderer(), image.GetPointerOrNull(),
					                      MakeVector2(16.0F, 16.0F));
					SetCursor(ibeam.GetPointerOrNull());
				}
			}

			void TextViewer::MouseLeave() { SetCursor(nullptr); }

			void TextViewer::KeyDown(const std::string& key) {
				UIManager& manager = GetManager();
				if (manager.isControlPressed || manager.isMetaPressed /* for OSX; Cmd + [a-z] */) {
					if (key == "C" &&
					    selection->GetSelectionEnd() > selection->GetSelectionStart()) {
						manager.Copy(GetSelectedText());
						return;
					} else if (key == "A") {
						if (!textmodel)
							return;
						selection->markPosition = textmodel->contentStart;
						selection->cursorPosition = textmodel->contentEnd;
						return;
					}
				}

				manager.ProcessHotKey(key);
			}

			std::string TextViewer::GetSelectedText() const {
				if (!textmodel)
					return "";

				std::string result;
				int start = selection->GetSelectionStart();
				int end = selection->GetSelectionEnd();

				const std::vector<TextViewerItem>& lines = textmodel->lines;

				for (size_t i = 0, count = lines.size(); i < count; ++i) {
					const std::string& line = lines[i].text;
					int lineStart = lines[i].index;
					int lineEnd = lineStart + static_cast<int>(line.size());

					if (end >= lineStart && start <= lineEnd) {
						int substrStart = std::max(start - lineStart, 0);
						int substrEnd = std::min(end - lineStart, static_cast<int>(line.size()));
						result += line.substr(substrStart, substrEnd - substrStart);
					}

					if (i < lines.size() - 1 && lineEnd < lines[i + 1].index) {
						// Implicit new line
						if (lineEnd >= start && lineEnd < end)
							result += "\n";
					}
				}

				return result;
			}

			void TextViewer::AddLine(const std::string& line, bool autoscroll, Vector4 color) {
				if (!textmodel)
					SetText("");
				if (autoscroll) {
					Layout();
					if (scrollBar->value < scrollBar->maxValue)
						autoscroll = false;
				}
				textmodel->AddLine(line, color);
				if (maxNumLines > 0 && textmodel->GetNumRows() > maxNumLines) {
					textmodel->RemoveFirstLines(textmodel->GetNumRows() - maxNumLines);
					SetModel(textmodel.GetPointerOrNull());
				}
				if (autoscroll) {
					Layout();
					ScrollToEnd();
				}
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
