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
#include <cmath>

#include "ConsoleCommandField.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/Utils/ConsoleHelper.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {

		// -- ConsoleCommandFieldCandidateView --

		ConsoleCommandFieldCandidateView::ConsoleCommandFieldCandidateView(
		    ui::UIManager* manager, std::vector<ConsoleCommandCandidate> candidates,
		    size_t prefixLength, size_t extraCount)
		    : ui::UIElement(manager),
		      candidates(std::move(candidates)),
		      prefixLength(prefixLength),
		      extraCount(extraCount) {}

		void ConsoleCommandFieldCandidateView::BuildRows(client::IFont& font) {
			const float rowHeight = 24.0F;
			// distance kept to the edges of the screen, the same as the console has
			const float margin = 10.0F;

			rows.clear();
			laidOut = true;

			ui::UIManager& manager = GetManager();
			const Vector2 pos = GetScreenPosition();

			float maxNameLen = 0.0F;
			for (const ConsoleCommandCandidate& c : candidates)
				maxNameLen = std::max(maxNameLen, font.Measure(c.name).x);
			nameColumnW = maxNameLen;

			// The box starts at the left edge of the field and must not leave the screen: a
			// description that does not fit continues on the following rows. Nothing is ever cut.
			const float maxBoxW = std::max(manager.screenWidth - pos.x - margin, 0.0F);
			const float descW = std::max(maxBoxW - maxNameLen - 20.0F, 40.0F);

			// The list hangs below the field, so only the rows that fit above the bottom edge are kept.
			const float top = pos.y + 2.0F - 10.0F;
			const float room = manager.screenHeight - top - margin;
			const size_t maxRows = static_cast<size_t>(std::max(1.0F, std::floor(room / rowHeight)));

			auto fill = [&](size_t limit) {
				rows.clear();
				for (size_t i = 0; i < candidates.size(); i++) {
					std::vector<std::string> lines =
					    ui::WrapText(font, candidates[i].description, descW);
					// whole candidates only, except that the first one is always shown
					if (!rows.empty() && rows.size() + lines.size() > limit)
						break;

					for (size_t l = 0; l < lines.size(); l++) {
						Row row;
						row.candidate = i;
						row.text = lines[l];
						row.first = (l == 0);
						if (row.first) {
							// the part of the name that was already typed is highlighted
							size_t split = std::min(prefixLength, candidates[i].name.size());
							row.head = candidates[i].name.substr(0, split);
							row.tail = candidates[i].name.substr(split);
							row.headW = std::ceil(font.Measure(row.head).x);
						}
						rows.push_back(std::move(row));
					}
				}
				if (rows.size() > limit)
					rows.resize(limit);
			};
			// the candidates that are not shown, plus the matches that were never listed
			auto countHidden = [&]() {
				size_t shown = rows.empty() ? 0 : rows.back().candidate + 1;
				return candidates.size() - shown + extraCount;
			};

			fill(maxRows);
			size_t hidden = countHidden();
			moreText.clear();
			if (hidden > 0 && maxRows > 1) {
				// the last row is kept for the "more" line
				fill(maxRows - 1);
				hidden = countHidden();
				moreText = _Tr("Client", "+{0} more", hidden);
			}

			// the box is as wide as the widest row that is shown
			float maxDescLen = 0.0F;
			for (const Row& row : rows)
				maxDescLen = std::max(maxDescLen, font.Measure(row.text).x);
			boxW = maxNameLen + maxDescLen + 20.0F;
			if (!moreText.empty())
				boxW = std::max(boxW, std::ceil(font.Measure(moreText).x) + 10.0F);
		}

		void ConsoleCommandFieldCandidateView::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			if (!laidOut)
				BuildRows(*font);
			if (rows.empty())
				return;

			client::IRenderer& r = GetManager().GetRenderer();
			const float rowHeight = 24.0F;

			const float w = boxW;
			const float h =
			    static_cast<float>(rows.size() + (moreText.empty() ? 0 : 1)) * rowHeight;

			Vector2 pos = GetScreenPosition();
			pos.y += 2.0F;

			// draw background
			ui::SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.9F));
			r.DrawFilledRect(pos.x + 1, pos.y - 10.0F + 1, pos.x + w - 1, pos.y - 10.0F + h - 1);

			ui::SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
			r.DrawOutlinedRect(pos.x, pos.y - 10.0F, pos.x + w, pos.y - 10.0F + h);

			// draw cvar list, with the same colors as the chat
			const Vector4 matchColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			const Vector4 nameColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
			const Vector4 descColor = MakeVector4(0.75F, 0.75F, 1.0F, 1.0F);
			const Vector4 cmdMatchColor = MakeVector4(0.7F, 0.5F, 1.0F, 1.0F);
			const Vector4 cmdNameColor = MakeVector4(0.7F, 0.5F, 1.0F, 0.5F);
			const Vector4 cmdDescColor = MakeVector4(0.75F, 0.6F, 0.95F, 1.0F);

			for (size_t i = 0, len = rows.size(); i < len; i++) {
				const Row& row = rows[i];
				const bool cmd = !candidates[row.candidate].isVariable;
				float y = static_cast<float>(i) * rowHeight - 10.0F;
				Vector2 descPos = pos + MakeVector2(15.0F + nameColumnW, y);

				// only the first row of a candidate shows its name
				if (row.first) {
					Vector2 namePos = pos + MakeVector2(5.0F, y);
					font->Draw(row.head, namePos, 1.0F, cmd ? cmdMatchColor : matchColor);
					font->Draw(row.tail, namePos + MakeVector2(row.headW, 0.0F), 1.0F,
					           cmd ? cmdNameColor : nameColor);
				}
				font->Draw(row.text, descPos, 1.0F, cmd ? cmdDescColor : descColor);
			}

			if (!moreText.empty()) {
				float y = static_cast<float>(rows.size()) * rowHeight - 10.0F;
				font->Draw(moreText, pos + MakeVector2(5.0F, y), 1.0F,
				           MakeVector4(1.0F, 1.0F, 1.0F, 0.4F));
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

				bool exhausted = false;
				for (int i = 0; i < 16; ++i) {
					if (!it->MoveNext()) {
						exhausted = true;
						break;
					}
					candidates.push_back(it->GetCurrent());
				}

				// count the matches beyond the listed ones, for the "more" line
				size_t extra = 0;
				if (!exhausted) {
					while (it->MoveNext())
						extra++;
				}

				if (candidates.size() > 0) {
					valueView = Handle<ConsoleCommandFieldCandidateView>::New(
					    &GetManager(), std::move(candidates), input.size(), extra);
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
						if (ui::StringCommonPrefixLength(input, name) == input.size()) {
							if (!foundOne) {
								commonPart = name;
								foundOne = true;
							}

							size_t commonLen = ui::StringCommonPrefixLength(commonPart, name);
							commonPart = commonPart.substr(0, commonLen);
						}
					}

					if (commonPart.size() > input.size()) {
						SetText(commonPart + GetText().substr(whitespace));
						Select(static_cast<int>(commonPart.size()), 0);
						// SetText does not raise the change event, so refresh the candidate list here
						OnChanged();
					}
				}
			} else {
				ui::FieldWithHistory::KeyDown(key);
			}
		}
	} // namespace gui
} // namespace spades
