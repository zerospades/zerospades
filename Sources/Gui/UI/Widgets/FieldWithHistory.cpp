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
					if (currentHistoryIndex > 0) {
						OverwriteItem();
						currentHistoryIndex--;
						LoadItem();
						OnChanged();
					}
				} else if (key == "Down") {
					if (currentHistoryIndex < cmdhistory->size()) {
						OverwriteItem();
						currentHistoryIndex++;
						LoadItem();
						OnChanged();
					}
				} else {
					Field::KeyDown(key);
				}
			}

			void FieldWithHistory::CommandSent() {
				cmdhistory->push_back(GetCommandHistoryItemRep());
				currentHistoryIndex = cmdhistory->size() - 1;
			}

			void FieldWithHistory::Clear() {
				currentHistoryIndex = cmdhistory->size();
				SetText("");
				OverwriteItem();
			}

			void FieldWithHistory::Cancelled() { OverwriteItem(); }
		} // namespace ui
	} // namespace gui
} // namespace spades
