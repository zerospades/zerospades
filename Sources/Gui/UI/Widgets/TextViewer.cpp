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
#include <cctype>
#include <cmath>
#include <utility>

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

				/**
				 * Removes paired backticks from `src` and records the enclosed byte ranges. The
				 * offsets in `src` of the removed backticks are appended to `removed`, in order.
				 */
				std::string StripInlineCode(const std::string& src,
				                            std::vector<std::pair<int, int>>& ranges,
				                            std::vector<int>* removed = nullptr) {
					std::string out;
					size_t i = 0;
					while (i < src.size()) {
						size_t a = src.find('`', i);
						if (a == std::string::npos) {
							out += src.substr(i);
							break;
						}
						size_t b = src.find('`', a + 1);
						if (b == std::string::npos) {
							// unmatched backtick: keep it as a literal character
							out += src.substr(i);
							break;
						}
						out += src.substr(i, a - i);
						int begin = static_cast<int>(out.size());
						out += src.substr(a + 1, b - a - 1);
						int end = static_cast<int>(out.size());
						if (end > begin)
							ranges.push_back(std::make_pair(begin, end));
						if (removed) {
							removed->push_back(static_cast<int>(a));
							removed->push_back(static_cast<int>(b));
						}
						i = b + 1;
					}
					return out;
				}

				/** Clips `ranges` to [begin, end) and rebases them to start at `begin`. */
				std::vector<std::pair<int, int>>
				SliceRanges(const std::vector<std::pair<int, int>>& ranges, int begin, int end) {
					std::vector<std::pair<int, int>> out;
					for (const std::pair<int, int>& r : ranges) {
						int b = std::max(r.first, begin);
						int e = std::min(r.second, end);
						if (e > b)
							out.push_back(std::make_pair(b - begin, e - begin));
					}
					return out;
				}

				/** Finds `http://` and `https://` URLs in `text` and records their byte ranges. */
				void FindLinks(const std::string& text, std::vector<TextViewerLink>& links) {
					const std::string trailingPunctuation = ".,;:!?'\"";
					size_t i = 0;
					while (i < text.size()) {
						size_t a = text.find("http", i);
						if (a == std::string::npos)
							break;

						size_t schemeLen = 0;
						if (text.compare(a, 7, "http://") == 0)
							schemeLen = 7;
						else if (text.compare(a, 8, "https://") == 0)
							schemeLen = 8;

						// skip words that merely end in "http", such as "xhttp://"
						bool glued = a > 0 && std::isalnum(static_cast<unsigned char>(text[a - 1])) != 0;
						if (schemeLen == 0 || glued) {
							i = a + 4;
							continue;
						}

						size_t e = a + schemeLen;
						while (e < text.size() && std::isspace(static_cast<unsigned char>(text[e])) == 0)
							e++;

						// do not swallow punctuation that follows the URL in a sentence
						while (e > a + schemeLen) {
							char last = text[e - 1];
							bool trim = trailingPunctuation.find(last) != std::string::npos;
							// a closing parenthesis belongs to the URL only if it has an opening one
							if (last == ')' || last == ']' || last == '}') {
								char open = last == ')' ? '(' : (last == ']' ? '[' : '{');
								size_t o = text.find(open, a);
								trim = o == std::string::npos || o >= e - 1;
							}
							if (!trim)
								break;
							e--;
						}

						if (e > a + schemeLen)
							links.push_back(TextViewerLink(static_cast<int>(a), static_cast<int>(e),
							                               text.substr(a, e - a)));
						i = std::max(e, a + schemeLen);
					}
				}

				/** Drops links overlapping an inline code range: code is shown literally. */
				void RemoveLinksInCode(std::vector<TextViewerLink>& links,
				                       const std::vector<std::pair<int, int>>& codeRanges) {
					links.erase(std::remove_if(links.begin(), links.end(),
					                           [&codeRanges](const TextViewerLink& link) {
						                           for (const std::pair<int, int>& range : codeRanges) {
							                           if (link.begin < range.second &&
							                               link.end > range.first)
								                           return true;
						                           }
						                           return false;
					                           }),
					            links.end());
				}

				/** Clips `links` to [begin, end) and rebases them to start at `begin`. */
				std::vector<TextViewerLink> SliceLinks(const std::vector<TextViewerLink>& links,
				                                       int begin, int end) {
					std::vector<TextViewerLink> out;
					for (const TextViewerLink& l : links) {
						int b = std::max(l.begin, begin);
						int e = std::min(l.end, end);
						if (e > b)
							out.push_back(TextViewerLink(b - begin, e - begin, l.url, l.id));
					}
					return out;
				}
			} // namespace

			// -- TextViewerItem --

			void TextViewerItem::BuildSegments(client::IFont* font) {
				segments.clear();
				textHeight = 0.0F;
				if (!font || text.empty())
					return;

				textHeight = font->Measure(text).y;
				if (codeRanges.empty() && links.empty())
					return;

				// split the row at every code and link boundary
				std::vector<int> cuts;
				cuts.push_back(0);
				cuts.push_back(static_cast<int>(text.size()));
				for (const std::pair<int, int>& range : codeRanges) {
					cuts.push_back(range.first);
					cuts.push_back(range.second);
				}
				for (const TextViewerLink& link : links) {
					cuts.push_back(link.begin);
					cuts.push_back(link.end);
				}
				std::sort(cuts.begin(), cuts.end());
				cuts.erase(std::unique(cuts.begin(), cuts.end()), cuts.end());

				// every boundary is measured exactly once
				std::vector<float> offsets;
				offsets.reserve(cuts.size());
				for (int cut : cuts)
					offsets.push_back(font->Measure(text.substr(0, cut)).x);

				for (size_t i = 0; i + 1 < cuts.size(); i++) {
					TextViewerSegment segment;
					segment.begin = cuts[i];
					segment.end = cuts[i + 1];
					segment.x1 = offsets[i];
					segment.x2 = offsets[i + 1];

					for (size_t l = 0; l < links.size(); l++) {
						if (segment.begin >= links[l].begin && segment.end <= links[l].end) {
							segment.kind = TextViewerSegment::Kind::Link;
							segment.linkIndex = static_cast<int>(l);
							break;
						}
					}

					if (segment.kind == TextViewerSegment::Kind::Plain) {
						for (const std::pair<int, int>& range : codeRanges) {
							if (segment.begin >= range.first && segment.end <= range.second) {
								segment.kind = TextViewerSegment::Kind::Code;
								break;
							}
						}
					}

					segments.push_back(segment);
				}
			}

			// -- TextViewerItemUI --

			TextViewerItemUI::TextViewerItemUI(UIManager* manager, const TextViewerItem& item,
			                                   TextViewerSelectionState* selection)
			    : UIElement(manager), item(item), selection(selection) {}

			void TextViewerItemUI::DrawHighlight(client::IRenderer& r, float x, float y, float w, float h) {
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.2F));
				r.DrawImage(nullptr, AABB2(x, y, w, h));
			}

			void TextViewerItemUI::Render() {
				client::IRenderer& r = GetManager().GetRenderer();
				Vector2 pos = GetScreenPosition();
				Vector2 sz = size;
				client::IFont* font = GetFont();
				if (!font)
					return;

				const float textScale = 1.0F;
				const std::string& text = item.text;

				const Vector4 linkColor = MakeVector4(0.4F, 0.65F, 0.9F, 1.0F);
				const Vector4 linkHoverColor = MakeVector4(0.5F, 0.75F, 1.0F, 1.0F);
				const Vector4 inlineCodeColor = MakeVector4(1.0F, 0.9F, 0.6F, 1.0F);

				// draw the inline code backgrounds first so selection and text stay on top
				for (const TextViewerSegment& seg : item.segments) {
					if (seg.kind != TextViewerSegment::Kind::Code)
						continue;
					float x1 = seg.x1 * textScale;
					float x2 = seg.x2 * textScale;
					SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.15F));
					r.DrawImage(nullptr, AABB2(pos.x + x1 - 2.0F, pos.y - 1.0F, (x2 - x1) + 3.0F, sz.y));
				}

				// draw selection
				if (selection->focusElement &&
				    (selection->showUnfocused || selection->focusElement->IsFocused())) {
					int start = selection->GetSelectionStart() - item.index;
					int end = selection->GetSelectionEnd() - item.index;
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

				if (text.empty())
					return;

				if (item.segments.empty()) {
					font->Draw(text, pos, textScale, item.color);
					return;
				}

				// Code and links change the color, so the row is drawn run by run
				float alpha = item.color.w;
				for (const TextViewerSegment& seg : item.segments) {
					Vector4 color = item.color;
					bool underline = false;

					if (seg.kind == TextViewerSegment::Kind::Link) {
						bool hovered = item.links[seg.linkIndex].id == selection->hoverLinkId;
						color = hovered ? linkHoverColor : linkColor;
						underline = true;
					} else if (seg.kind == TextViewerSegment::Kind::Code) {
						color = inlineCodeColor;
					}

					float x1 = seg.x1 * textScale;
					float x2 = seg.x2 * textScale;
					font->Draw(text.substr(seg.begin, seg.end - seg.begin),
					           MakeVector2(pos.x + x1, pos.y), textScale, color);

					if (underline) {
						SetColorNP(r, color);
						r.DrawImage(nullptr, AABB2(pos.x + x1,
						                           pos.y + item.textHeight * textScale - 1.0F,
						                           x2 - x1, 1.0F));
					}
				}
			}

			// -- TextViewerModel --

			TextViewerModel::TextViewerModel(UIManager* manager, const std::string& text,
			                                 client::IFont* font, float width,
			                                 TextViewerSelectionState* selection, bool parseCode,
			                                 bool parseLinks)
			    : parseCode(parseCode),
			      parseLinks(parseLinks),
			      manager(manager),
			      font(font),
			      width(width),
			      selection(selection) {
				std::vector<std::string> textLines = SplitLines(text);
				for (const std::string& line : textLines)
					AddLine(line, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			}

			void TextViewerModel::AddLine(const std::string& text, Vector4 color) {
				std::vector<std::pair<int, int>> ranges;
				std::vector<TextViewerLink> links;

				// Control bytes and backticks are removed first so the link offsets match the
				// displayed text.
				std::string plain = text;
				if (parseCode) {
					std::vector<int> removed;
					plain = StripInlineCode(plain, ranges, &removed);
				}
				if (parseLinks) {
					FindLinks(plain, links);
					RemoveLinksInCode(links, ranges);
					for (TextViewerLink& link : links)
						link.id = nextLinkId++;
				}
				AddLineInternal(plain, color, ranges, links);
			}

			void TextViewerModel::AddLineInternal(const std::string& text, Vector4 color,
			                                      const std::vector<std::pair<int, int>>& codeRanges,
			                                      const std::vector<TextViewerLink>& links) {
				// Appends the row [begin, end) of `text` and advances the content index.
				auto pushRow = [&](int begin, int end, int advance) {
					TextViewerItem row(text.substr(begin, end - begin), color, contentEnd,
					                   SliceRanges(codeRanges, begin, end),
					                   SliceLinks(links, begin, end));
					row.BuildSegments(font);
					lines.push_back(std::move(row));
					contentEnd += advance;
				};

				int startPos = 0;
				if (font->Measure(text).x <= width) {
					int len = static_cast<int>(text.size());
					pushRow(0, len, len + 1);
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
							pushRow(startPos, pos, pos - startPos);
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
							pushRow(startPos, nextPos, nextPos - startPos + 1);
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
				                                         GetItemWidth(), selection.GetPointerOrNull(),
				                                         parseInlineCode, parseLinks);
				SetModel(textmodel.GetPointerOrNull());
				selection->markPosition = 0;
				selection->cursorPosition = 0;

				// link IDs restart with every model, so a stale hover must not carry over
				if (selection->hoverLinkId >= 0) {
					selection->hoverLinkId = -1;
					ApplyTextCursor();
				}
				RefreshHover();
			}

			void TextViewer::SetScrollBarVisible(bool visible) {
				scrollBar->visible = visible;
				// 16 pixels is the default width of `ListViewBase`
				scrollBarWidth = visible ? 16.0F : 0.0F;
				Layout();
			}

			int TextViewer::PointToCharIndex(Vector2 clientPosition) const {
				if (!textmodel)
					return 0;

				int line = static_cast<int>(std::floor((clientPosition.y - GetRowsOffsetY()) / rowHeight)) + GetTopRowIndex();
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

			void TextViewer::ApplyTextCursor() {
				Handle<Cursor> ibeam =
				    Handle<Cursor>::New(GetManager().GetRenderer(), image.GetPointerOrNull(),
				                        MakeVector2(16.0F, 16.0F));
				SetCursor(ibeam.GetPointerOrNull());
			}

			void TextViewer::UpdateHover(Vector2 clientPosition) {
				// links are only interactive when someone listens for the click
				const TextViewerLink* link = linkActivated ? FindLinkAt(clientPosition) : nullptr;
				int id = link != nullptr ? link->id : -1;
				if (id == selection->hoverLinkId)
					return;

				selection->hoverLinkId = id;
				if (id < 0) {
					if (textmodel)
						ApplyTextCursor();
				} else {
					// the I-beam is for text, a link gets the regular pointer
					SetCursor(nullptr);
				}
			}

			void TextViewer::RefreshHover() {
				// hover is not tracked while a selection is being dragged
				if (dragging || !IsVisible())
					return;

				Vector2 p = ScreenToClient(GetManager().mouseCursorPosition);
				if (p.x < 0.0F || p.y < 0.0F || p.x >= GetItemWidth() || p.y >= size.y)
					return;

				UpdateHover(p);
			}

			void TextViewer::MouseWheel(float delta) {
				ListViewBase::MouseWheel(delta);

				// the rows moved under a mouse that did not
				RefreshHover();
			}

			void TextViewer::MouseDown(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left)
					return;
				dragging = true;
				const TextViewerLink* pressed = FindLinkAt(clientPosition);
				pressedLinkId = pressed != nullptr ? pressed->id : -1;
				pressedLinkUrl = pressed != nullptr ? pressed->url : std::string();
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
				else
					UpdateHover(clientPosition);
			}

			void TextViewer::MouseUp(MouseButton button, Vector2 clientPosition) {
				if (button != MouseButton::Left)
					return;
				dragging = false;

				int pressedId = pressedLinkId;
				std::string url = pressedLinkUrl;
				pressedLinkId = -1;
				pressedLinkUrl.clear();

				// a click is a press and a release on the same link without dragging a selection
				const TextViewerLink* released = FindLinkAt(clientPosition);
				if (pressedId >= 0 && linkActivated &&
				    selection->markPosition == selection->cursorPosition && released != nullptr &&
				    released->id == pressedId) {
					// copy the handler: it may rebuild the UI and destroy this viewer
					std::function<void(const std::string&)> handler = linkActivated;
					handler(url);
				}
			}

			void TextViewer::MouseEnter() {
				selection->hoverLinkId = -1;
				if (textmodel)
					ApplyTextCursor();
			}

			void TextViewer::MouseLeave() {
				selection->hoverLinkId = -1;
				SetCursor(nullptr);
			}

			void TextViewer::MouseCaptureLost() {
				dragging = false;
				pressedLinkId = -1;
				pressedLinkUrl.clear();
			}

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

			const TextViewerLink* TextViewer::FindLinkAt(Vector2 clientPosition) const {
				if (!textmodel || clientPosition.x < 0.0F || clientPosition.y < 0.0F)
					return nullptr;

				int line = static_cast<int>(std::floor((clientPosition.y - GetRowsOffsetY()) / rowHeight)) + GetTopRowIndex();
				if (line < 0 || line >= static_cast<int>(textmodel->lines.size()))
					return nullptr;

				// the segment extents were measured when the row was built
				const TextViewerItem& item = textmodel->lines[line];
				for (const TextViewerSegment& segment : item.segments) {
					if (segment.kind != TextViewerSegment::Kind::Link)
						continue;
					if (clientPosition.x >= segment.x1 && clientPosition.x < segment.x2)
						return &item.links[segment.linkIndex];
				}
				return nullptr;
			}

			std::string TextViewer::GetLinkAt(Vector2 clientPosition) const {
				const TextViewerLink* link = FindLinkAt(clientPosition);
				return link != nullptr ? link->url : std::string();
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
				if (!textmodel) {
					SetText("");
					// SetText("") builds a model with one empty row (SplitLines("") returns
					// a single empty string). Drop it so the first real line is the first row.
					textmodel->RemoveFirstLines(1);
					SetModel(textmodel.GetPointerOrNull());
				}
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

				// new rows may have shifted the content under a motionless mouse
				RefreshHover();
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
