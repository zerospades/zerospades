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

#include "ConsoleCommandField.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/ConsoleHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		namespace {
			size_t StringCommonPrefixLength(const std::string& a, const std::string& b) {
				size_t ln = std::min(a.size(), b.size());
				for (size_t i = 0; i < ln; i++) {
					if (std::tolower(static_cast<unsigned char>(a[i])) !=
					    std::tolower(static_cast<unsigned char>(b[i])))
						return i;
				}
				return ln;
			}
		} // namespace

		// -- ConsoleCommandFieldCandidateView --

		ConsoleCommandFieldCandidateView::ConsoleCommandFieldCandidateView(
		    ui::UIManager* manager, std::vector<ConsoleCommandCandidate> candidates)
		    : ui::UIElement(manager), candidates(std::move(candidates)) {}

		void ConsoleCommandFieldCandidateView::Render() {
			float maxNameLen = 0.0F;
			float maxDescLen = 0.0F;
			client::IFont* font = GetFont();
			if (!font)
				return;
			client::IRenderer& r = GetManager().GetRenderer();
			float rowHeight = 24.0F;

			for (const ConsoleCommandCandidate& c : candidates) {
				maxNameLen = std::max(maxNameLen, font->Measure(c.name).x);
				maxDescLen = std::max(maxDescLen, font->Measure(c.description).x);
			}

			float w = maxNameLen + maxDescLen + 20.0F;
			float h = static_cast<float>(candidates.size()) * rowHeight;

			Vector2 pos = GetScreenPosition();
			pos.y += 2.0F;

			// draw background
			ui::SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.75F));
			r.DrawFilledRect(pos.x + 1, pos.y - 10.0F + 1, pos.x + w - 1, pos.y - 10.0F + h - 1);

			ui::SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
			r.DrawOutlinedRect(pos.x, pos.y - 10.0F, pos.x + w, pos.y - 10.0F + h);

			// draw cvar list
			Vector4 nameColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.7F);
			Vector4 nameShadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.3F);
			Vector4 descColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			Vector4 descShadow = MakeVector4(0.0F, 0.0F, 0.0F, 0.4F);

			for (size_t i = 0, len = candidates.size(); i < len; i++) {
				float y = static_cast<float>(i) * rowHeight - 10.0F;
				Vector2 namePos = pos + MakeVector2(5.0F, y);
				Vector2 descPos = pos + MakeVector2(15.0F + maxNameLen, y);

				font->DrawShadow(candidates[i].name, namePos, 1.0F, nameColor, nameShadow);
				font->DrawShadow(candidates[i].description, descPos, 1.0F, descColor, descShadow);
			}
		}

		// -- ConsoleCommandField --

		ConsoleCommandField::ConsoleCommandField(ui::UIManager* manager,
		                                         std::vector<ui::CommandHistoryItem>* history,
		                                         ConsoleHelper* helper)
		    : ui::FieldWithHistory(manager, history), helper(helper) {}

		void ConsoleCommandField::OnChanged() {
			ui::FieldWithHistory::OnChanged();

			if (valueView)
				valueView->SetParent(nullptr);

			// Find the command name part
			size_t ws = GetText().find(' ');
			int whitespace = (ws == std::string::npos) ? static_cast<int>(GetText().size())
			                                           : static_cast<int>(ws);

			if (whitespace > 0) {
				std::string input = GetText().substr(0, whitespace);
				Handle<ConsoleCommandCandidateIterator> it(helper->AutocompleteCommandName(input),
				                                           false);
				std::vector<ConsoleCommandCandidate> candidates;

				for (int i = 0; i < 16; ++i) {
					if (!it->MoveNext())
						break;
					candidates.push_back(it->GetCurrent());
				}

				if (candidates.size() > 0) {
					valueView = Handle<ConsoleCommandFieldCandidateView>::New(&GetManager(),
					                                                          std::move(candidates));
					valueView->SetBounds(AABB2(0.0F, size.y + 10.0F, 0.0F, 0.0F));
					valueView->SetParent(this);
				}
			}
		}

		void ConsoleCommandField::Clear() {
			ui::FieldWithHistory::Clear();
			OnChanged();
		}

		void ConsoleCommandField::KeyDown(const std::string& key) {
			if (key == "Tab") {
				// Find the command name part
				size_t ws = GetText().find(' ');
				int whitespace = (ws == std::string::npos) ? static_cast<int>(GetText().size())
				                                           : static_cast<int>(ws);

				if (GetSelectionLength() == 0 && GetSelectionStart() <= whitespace) {
					// Command name auto completion
					std::string input = GetText().substr(0, whitespace);
					Handle<ConsoleCommandCandidateIterator> it(
					    helper->AutocompleteCommandName(input), false);
					std::string commonPart;
					bool foundOne = false;
					while (it->MoveNext()) {
						std::string name = it->GetCurrent().name;
						if (StringCommonPrefixLength(input, name) == input.size()) {
							if (!foundOne) {
								commonPart = name;
								foundOne = true;
							}

							size_t commonLen = StringCommonPrefixLength(commonPart, name);
							commonPart = commonPart.substr(0, commonLen);
						}
					}

					if (commonPart.size() > input.size()) {
						SetText(commonPart + GetText().substr(whitespace));
						Select(static_cast<int>(commonPart.size()), 0);
					}
				}
			} else {
				ui::FieldWithHistory::KeyDown(key);
			}
		}
	} // namespace gui
} // namespace spades
