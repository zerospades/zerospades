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

#include <Gui/UI/Widgets/Field.h>

namespace spades {
	namespace gui {
		namespace ui {
			/** One saved entry (text + selection) in a `FieldWithHistory`. */
			struct CommandHistoryItem {
				std::string text;
				int selStart = 0;
				int selEnd = 0;

				CommandHistoryItem() {}
				CommandHistoryItem(const std::string& text, int selStart, int selEnd)
				    : text(text), selStart(selStart), selEnd(selEnd) {}
			};

			/**
			 * A `Field` with Up/Down command history navigation. With an empty
			 * field, Up/Down cycle through history bash-style. Once text has been
			 * typed, Up/Down instead perform a zsh-like `history-search-backward`/
			 * `-forward`: they jump to the nearest older/newer entry that starts
			 * with the text already typed before the cursor, so typing "/ki" then
			 * pressing Up repeatedly cycles only through past commands beginning
			 * with "/ki".
			 */
			class FieldWithHistory : public Field {
				// Weak: the history list is owned by the enclosing window and is shared
				// across field instances so it persists between uses.
				std::vector<CommandHistoryItem>* cmdhistory;
				CommandHistoryItem temporalLastHistory;
				size_t currentHistoryIndex;

				// Set while a prefix search is in progress, so repeated Up/Down
				// presses keep reusing the same search prefix instead of
				// recapturing it from the (already-navigated-to) current text.
				bool historySearchActive = false;
				std::string historySearchPrefix;
				// Guards `OnChanged` while history navigation itself is updating the
				// text, so that update isn't mistaken for the user typing (which
				// would otherwise cancel the in-progress prefix search).
				bool navigatingHistory = false;

				CommandHistoryItem GetCommandHistoryItemRep() const;
				void SetCommandHistoryItemRep(const CommandHistoryItem& value);
				void OverwriteItem();
				void LoadItem();

				void SearchHistory(bool backward);
				bool MatchesHistorySearch(const std::string& text) const;
				void LoadHistoryEntry();

			public:
				FieldWithHistory(UIManager* manager, std::vector<CommandHistoryItem>* history);

				void KeyDown(const std::string& key) override;
				void OnChanged() override;

				void CommandSent();
				virtual void Clear();
				void Cancelled();
			};
		} // namespace ui
	} // namespace gui
} // namespace spades
