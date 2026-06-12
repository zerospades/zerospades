/*
 Copyright (c) 2019 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "../UIFramework/UIFramework.as"
#include "ConsoleCommandField.as"

namespace spades {
	class ConsoleWindow : spades::ui::UIElement {
		private ConsoleHelper@ helper;
		private array<spades::ui::CommandHistoryItem@> history = {};
		private FieldWithHistory@ field;
		private spades::ui::TextViewer@ viewer;

		private ConfigItem cl_consoleScrollbackLines("cl_consoleScrollbackLines", "1000");

		ConsoleWindow(ConsoleHelper@ helper, spades::ui::UIManager@ manager) {
			super(manager);
			@this.helper = helper;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			float height = floor(sh * 0.4F);

			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				label.Bounds = AABB2(0.0F, 0.0F, sw, height);
				AddChild(label);
			}

			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(1.0F, 1.0F, 1.0F, 0.1F);
				label.Bounds = AABB2(0.0F, height, sw, 1.0F);
				AddChild(label);
			}

			{
				@field = ConsoleCommandField(Manager, this.history, helper);
				field.Bounds = AABB2(10.0F, (height - 30.0F) - 8.0F, sw - 20.0F, 30.0F);
				field.Placeholder = _Tr("Console", "Command");
				field.MaxLength = 128;
				@field.Changed = spades::ui::EventHandler(this.OnFieldChanged);
				AddChild(field);
			}
			{
				spades::ui::TextViewer viewer(Manager);
				AddChild(viewer);
				viewer.Bounds = AABB2(10.0F, 5.0F, sw - 20.0F, height - 45.0F);
				viewer.MaxNumLines = uint(cl_consoleScrollbackLines.IntValue);
				@this.viewer = viewer;
			}
			{
				spades::ui::Label label(Manager);
				label.TextColor = Vector4(1.0F, 1.0F, 1.0F, 0.25F);
				label.Text = helper.GetVersionString();
				label.Bounds = AABB2(5.0F, (height - 30.0F) - 8.0F, sw - 20.0F, 30.0F);
				label.Alignment = Vector2(1.0F, 0.5F);
				AddChild(label);
			}
		}

		private void OnFieldChanged(spades::ui::UIElement@ sender) {}

		void FocusField() { @Manager.ActiveElement = field; }

		void AddLine(string line) { viewer.AddLine(line, true); }

		void HotKey(string key) {
			if (key == "Enter") {
				if (field.Text.length == 0)
					return;
				field.CommandSent();
				helper.ExecCommand(field.Text);
				field.Clear();
			}
		}
	}
} // namespace spades
