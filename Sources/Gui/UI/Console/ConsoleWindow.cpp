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

#include <cmath>

#include "ConsoleWindow.h"
#include "ConsoleCommandField.h"
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/ConsoleHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/TextViewer.h>

DEFINE_SPADES_SETTING(cl_consoleScrollbackLines, "1000");

namespace spades {
	namespace gui {
		ConsoleWindow::ConsoleWindow(ConsoleHelper* helper, ui::UIManager* manager)
		    : ui::UIElement(manager), helper(helper) {
			float sw = GetManager().screenWidth;
			float sh = GetManager().screenHeight;

			float height = std::floor(sh * 0.4F);

			{
				Handle<ui::Label> label = Handle<ui::Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(0.0F, 0.0F, sw, height));
				AddChild(label.GetPointerOrNull());
			}

			{
				Handle<ui::Label> label = Handle<ui::Label>::New(manager);
				label->backgroundColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.1F);
				label->SetBounds(AABB2(0.0F, height, sw, 1.0F));
				AddChild(label.GetPointerOrNull());
			}

			{
				Handle<ConsoleCommandField> f =
				    Handle<ConsoleCommandField>::New(manager, &history, helper);
				field = f.GetPointerOrNull();
				field->SetBounds(AABB2(10.0F, (height - 30.0F) - 8.0F, sw - 20.0F, 30.0F));
				field->placeholder = _Tr("Console", "Command");
				field->maxLength = 128;
				AddChild(field);
			}
			{
				Handle<ui::TextViewer> v = Handle<ui::TextViewer>::New(manager);
				viewer = v.GetPointerOrNull();
				AddChild(viewer);
				viewer->SetBounds(AABB2(10.0F, 5.0F, sw - 20.0F, height - 45.0F));
				viewer->maxNumLines = static_cast<int>(cl_consoleScrollbackLines);
			}
			{
				Handle<ui::Label> label = Handle<ui::Label>::New(manager);
				label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.25F);
				label->text = helper->GetVersionString();
				label->SetBounds(AABB2(5.0F, (height - 30.0F) - 8.0F, sw - 20.0F, 30.0F));
				label->alignment = MakeVector2(1.0F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
		}

		void ConsoleWindow::FocusField() { GetManager().SetActiveElement(field); }

		void ConsoleWindow::AddLine(const std::string& line) { viewer->AddLine(line, true); }

		void ConsoleWindow::HotKey(const std::string& key) {
			if (key == "Enter") {
				if (field->GetText().empty())
					return;
				field->CommandSent();
				helper->ExecCommand(field->GetText());
				field->Clear();
			}
		}
	} // namespace gui
} // namespace spades
