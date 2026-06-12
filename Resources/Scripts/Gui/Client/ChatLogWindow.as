/*
 Copyright (c) 2013 yvt

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

#include "ChatSayWindow.as"

namespace spades {

	class ChatLogSayWindow : ClientChatWindow {
		ChatLogWindow@ owner;

		ChatLogSayWindow(ChatLogWindow@ own, bool isTeamChat) {
			super(own.ui, isTeamChat);
			@owner = own;
		}

		void Close() {
			owner.SayWindowClosed();
			@this.Parent = null;
		}
	}

	class ChatLogWindow : spades::ui::UIElement {
		float ContentsTop, ContentsHeight;

		ClientUI@ ui;
		private ClientUIHelper@ helper;

		private spades::ui::TextViewer@ viewer;
		ChatLogSayWindow@ sayWindow;

		private spades::ui::UIElement@ sayButton1;
		private spades::ui::UIElement@ sayButton2;

		private ConfigItem cg_keyGlobalChat("cg_keyGlobalChat");
		private ConfigItem cg_keyTeamChat("cg_keyTeamChat");
		private ConfigItem cg_keyChatLog("cg_keyChatLog");

		ChatLogWindow(ClientUI@ ui) {
			super(ui.manager);
			@this.ui = ui;
			@this.helper = ui.helper;

			@Font = Manager.RootElement.Font;
			this.Bounds = Manager.RootElement.Bounds;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			float contentsWidth = sw - 16.0F;
			float maxContentsWidth = 700.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float contentsLeft = (sw - contentsWidth) * 0.5F;
			ContentsHeight = sh - 200.0F;
			ContentsTop = (sh - ContentsHeight - 106.0F) * 0.5F;

			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				label.Bounds = AABB2(0.0F, ContentsTop - 13.0F, Size.x, ContentsHeight + 27.0F);
				AddChild(label);
			}

			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("Client", "Close");
				button.HotKeyText = _Tr("Client", "[Esc]");
				button.Bounds = AABB2(contentsLeft + contentsWidth - 150.0F,
					ContentsTop + ContentsHeight - 30.0F, 150.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnOkPressed);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("Client", "Say Global");
				button.Bounds = AABB2(contentsLeft,
					ContentsTop + ContentsHeight - 30.0F, 150.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnGlobalChat);
				AddChild(button);
				@this.sayButton1 = button;
			}
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("Client", "Say Team");
				button.Bounds = AABB2(contentsLeft + 155.0F,
					ContentsTop + ContentsHeight - 30.0F, 150.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnTeamChat);
				AddChild(button);
				@this.sayButton2 = button;
			}
			{
				spades::ui::TextViewer viewer(Manager);
				AddChild(viewer);
				viewer.Bounds = AABB2(contentsLeft, ContentsTop,
					contentsWidth, ContentsHeight - 40.0F);
				@this.viewer = viewer;
			}
		}

		void ScrollToEnd() {
			viewer.Layout();
			viewer.ScrollToEnd();
		}

		void Close() { @ui.ActiveUI = null; }

		void UpdateState(bool chatEnabled) {
			sayButton1.Enable = chatEnabled;
			sayButton2.Enable = chatEnabled;
		}

		void SayWindowClosed() {
			@sayWindow = null;
			UpdateState(ui.IsChatEnabled());
		}

		private void OnOkPressed(spades::ui::UIElement@ sender) { Close(); }

		private void OnChat(bool isTeam) {
			if (sayWindow !is null) {
				sayWindow.IsTeamChat = true;
				return;
			}

			sayButton1.Enable = false;
			sayButton2.Enable = false;
			ChatLogSayWindow wnd(this, isTeam);
			AddChild(wnd);
			wnd.Bounds = this.Bounds;
			@this.sayWindow = wnd;
			@Manager.ActiveElement = wnd.field;
		}

		private void OnTeamChat(spades::ui::UIElement@ sender) { OnChat(true); }
		private void OnGlobalChat(spades::ui::UIElement@ sender) { OnChat(false); }

		void HotKey(string key) {
			if (sayWindow !is null) {
				UIElement::HotKey(key);
				return;
			}

			if (IsEnabled and (EqualsIgnoringCase(key, cg_keyChatLog.StringValue) or key == "Escape"))
				Close();
			else if (IsEnabled and ui.IsChatEnabled() and EqualsIgnoringCase(key, cg_keyTeamChat.StringValue))
				OnTeamChat(this);
			else if (IsEnabled and ui.IsChatEnabled() and EqualsIgnoringCase(key, cg_keyGlobalChat.StringValue))
				OnGlobalChat(this);
			else
				UIElement::HotKey(key);
		}

		void Record(string text, Vector4 color) {
			color.x = Mix(color.x, 1.0F, 0.5F);
			color.y = Mix(color.y, 1.0F, 0.5F);
			color.z = Mix(color.z, 1.0F, 0.5F);
			color.w = 1.0F;
			viewer.AddLine(text, this.IsVisible, color);
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F);
			r.DrawImage(null, AABB2(pos.x, pos.y + ContentsTop - 14.0F, size.x, 1.0F));
			r.DrawImage(null, AABB2(pos.x, pos.y + ContentsTop + ContentsHeight + 14.0F, size.x, 1.0F));

			UIElement::Render();
		}
	}
}
