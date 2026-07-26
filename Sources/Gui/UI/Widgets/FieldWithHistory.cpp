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

#include "FieldWithHistory.h"
#include <Gui/UI/Framework/TextUtils.h>

namespace spades {
	namespace gui {
		namespace ui {
			FieldWithHistory::FieldWithHistory(UIManager* manager,
			                                   std::vector<CommandHistoryItem>* history)
			    : Field(manager), cmdhistory(history) {
				currentHistoryIndex = cmdhistory->size();
				temporalLastHistory = GetCommandHistoryItemRep();
			}

			CommandHistoryItem FieldWithHistory::GetCommandHistoryItemRep() const {
				return CommandHistoryItem(GetText(), GetSelectionStart(), GetSelectionEnd());
			}

			void FieldWithHistory::SetCommandHistoryItemRep(const CommandHistoryItem& value) {
				SetText(value.text);
				Select(value.selStart, value.selEnd - value.selStart);
			}

			void FieldWithHistory::OverwriteItem() {
				if (currentHistoryIndex < cmdhistory->size()) {
					(*cmdhistory)[currentHistoryIndex] = GetCommandHistoryItemRep();
				} else if (currentHistoryIndex == cmdhistory->size()) {
					temporalLastHistory = GetCommandHistoryItemRep();
				}
			}

			void FieldWithHistory::LoadItem() {
				if (currentHistoryIndex < cmdhistory->size()) {
					SetCommandHistoryItemRep((*cmdhistory)[currentHistoryIndex]);
				} else if (currentHistoryIndex == cmdhistory->size()) {
					SetCommandHistoryItemRep(temporalLastHistory);
				}
			}

			void FieldWithHistory::KeyDown(const std::string& key) {
				if (key == "Up") {
					SearchHistory(true);
				} else if (key == "Down") {
					SearchHistory(false);
				} else {
					Field::KeyDown(key);
				}
			}

			void FieldWithHistory::OnChanged() {
				// A real edit invalidates any prefix search in progress. Changes made
				// by history navigation itself are exempted via the guard.
				if (!navigatingHistory)
					historySearchActive = false;
				Field::OnChanged();
			}

			bool FieldWithHistory::MatchesHistorySearch(const std::string& text) const {
				return text.size() >= historySearchPrefix.size() &&
				       StringCommonPrefixLength(historySearchPrefix, text) ==
				           historySearchPrefix.size();
			}

			void FieldWithHistory::LoadHistoryEntry() {
				navigatingHistory = true;
				LoadItem();
				navigatingHistory = false;
				if (!historySearchPrefix.empty())
					Select(static_cast<int>(historySearchPrefix.size()), 0);
				OnChanged();
			}

			void FieldWithHistory::SearchHistory(bool backward) {
				if (!historySearchActive) {
					// Start of a new search: the prefix is whatever the user typed
					// before the cursor. An empty prefix means "no filter", which
					// falls back to plain bash-style linear cycling below.
					historySearchPrefix = GetText().substr(0, GetSelectionStart());
					historySearchActive = true;
				}

				if (historySearchPrefix.empty()) {
					if (backward) {
						if (currentHistoryIndex == 0)
							return;
						OverwriteItem();
						currentHistoryIndex--;
					} else {
						if (currentHistoryIndex >= cmdhistory->size())
							return;
						OverwriteItem();
						currentHistoryIndex++;
					}
					LoadHistoryEntry();
					return;
				}

				if (backward) {
					for (size_t i = currentHistoryIndex; i-- > 0;) {
						if (MatchesHistorySearch((*cmdhistory)[i].text)) {
							OverwriteItem();
							currentHistoryIndex = i;
							LoadHistoryEntry();
							return;
						}
					}
					// No older match: stay put (mirrors zsh's history-search-backward).
				} else {
					for (size_t i = currentHistoryIndex + 1; i < cmdhistory->size(); i++) {
						if (MatchesHistorySearch((*cmdhistory)[i].text)) {
							OverwriteItem();
							currentHistoryIndex = i;
							LoadHistoryEntry();
							return;
						}
					}
					if (currentHistoryIndex < cmdhistory->size()) {
						// No newer match: land back on the in-progress line, which
						// always matches its own prefix.
						OverwriteItem();
						currentHistoryIndex = cmdhistory->size();
						LoadHistoryEntry();
					}
				}
			}

			void FieldWithHistory::CommandSent() {
				cmdhistory->push_back(GetCommandHistoryItemRep());
				currentHistoryIndex = cmdhistory->size() - 1;
				historySearchActive = false;
			}

			void FieldWithHistory::Clear() {
				currentHistoryIndex = cmdhistory->size();
				SetText("");
				OverwriteItem();
				historySearchActive = false;
			}

			void FieldWithHistory::Cancelled() {
				OverwriteItem();
				historySearchActive = false;
			}
		} // namespace ui
	} // namespace gui
} // namespace spades
