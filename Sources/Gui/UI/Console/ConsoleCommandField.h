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

#include <vector>

#include <Gui/Utils/ConsoleCommandCandidate.h>
#include <Gui/UI/Widgets/FieldWithHistory.h>

namespace spades {
	namespace gui {
		class ConsoleHelper;

		/** Displays console command candidates below a `ConsoleCommandField`. */
		class ConsoleCommandFieldCandidateView : public ui::UIElement {
			std::vector<ConsoleCommandCandidate> candidates;

		public:
			ConsoleCommandFieldCandidateView(ui::UIManager* manager,
			                                 std::vector<ConsoleCommandCandidate> candidates);
			void Render() override;
		};

		/** A `Field` with command name autocompletion. */
		class ConsoleCommandField : public ui::FieldWithHistory {
			Handle<ConsoleCommandFieldCandidateView> valueView;
			ConsoleHelper* helper; // weak

		public:
			ConsoleCommandField(ui::UIManager* manager,
			                    std::vector<ui::CommandHistoryItem>* history, ConsoleHelper* helper);

			void OnChanged() override;
			void Clear() override;
			void KeyDown(const std::string& key) override;
		};
	} // namespace gui
} // namespace spades
