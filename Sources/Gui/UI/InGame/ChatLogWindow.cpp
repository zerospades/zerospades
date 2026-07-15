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

#include "ChatLogWindow.h"
#include <Client/ClientUI.h>
#include <Client/ClientUIHelper.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/TextViewer.h>

SPADES_SETTING(cg_keyGlobalChat);
SPADES_SETTING(cg_keyTeamChat);
SPADES_SETTING(cg_keyChatLog);

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::Label;
		using gui::ui::SetColorNP;
		using gui::ui::TextViewer;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		// -- ChatLogSayWindow --

		ChatLogSayWindow::ChatLogSayWindow(ChatLogWindow* owner, bool isTeamChat)
		    : ClientChatWindow(owner->ui, isTeamChat), owner(owner) {}

		void ChatLogSayWindow::Close() {
			owner->SayWindowClosed();
			SetParent(nullptr);
		}

		// -- ChatLogWindow --

		ChatLogWindow::ChatLogWindow(ClientUI* ui)
		    : UIElement(&ui->GetUIManager()), ui(ui), helper(&ui->GetHelper()) {
			UIManager* manager = &GetManager();
			SetFont(manager->GetRootElement().GetFont());
			SetBounds(manager->GetRootElement().GetBounds());

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			float contentsWidth = sw - 16.0F;
			float maxContentsWidth = 700.0F;
			if (contentsWidth > maxContentsWidth)
				contentsWidth = maxContentsWidth;

			float contentsLeft = (sw - contentsWidth) * 0.5F;
			contentsHeight = sh - 200.0F;
			contentsTop = (sh - contentsHeight - 106.0F) * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				label->SetBounds(AABB2(0.0F, contentsTop - 13.0F, size.x, contentsHeight + 27.0F));
				AddChild(label.GetPointerOrNull());
			}

			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Close");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(contentsLeft + contentsWidth - 150.0F,
				                        contentsTop + contentsHeight - 30.0F, 150.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnOkPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Say Global");
				button->SetBounds(
				    AABB2(contentsLeft, contentsTop + contentsHeight - 30.0F, 150.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnGlobalChat(s); };
				AddChild(button.GetPointerOrNull());
				sayButton1 = button.GetPointerOrNull();
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Say Team");
				button->SetBounds(AABB2(contentsLeft + 155.0F, contentsTop + contentsHeight - 30.0F,
				                        150.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnTeamChat(s); };
				AddChild(button.GetPointerOrNull());
				sayButton2 = button.GetPointerOrNull();
			}
			{
				Handle<TextViewer> v = Handle<TextViewer>::New(manager);
				AddChild(v.GetPointerOrNull());
				v->SetBounds(AABB2(contentsLeft, contentsTop, contentsWidth, contentsHeight - 40.0F));
				viewer = v.GetPointerOrNull();
			}
		}

		void ChatLogWindow::ScrollToEnd() {
			viewer->Layout();
			viewer->ScrollToEnd();
		}

		void ChatLogWindow::Close() { ui->SetActiveUI(nullptr); }

		void ChatLogWindow::UpdateState(bool chatEnabled) {
			sayButton1->enable = chatEnabled;
			sayButton2->enable = chatEnabled;
		}

		void ChatLogWindow::SayWindowClosed() {
			sayWindow = nullptr;
			UpdateState(ui->IsChatEnabled());
		}

		void ChatLogWindow::OnOkPressed(UIElement&) { Close(); }

		void ChatLogWindow::OnChat(bool isTeam) {
			if (sayWindow != nullptr) {
				sayWindow->SetIsTeamChat(true);
				return;
			}

			sayButton1->enable = false;
			sayButton2->enable = false;
			Handle<ChatLogSayWindow> wnd = Handle<ChatLogSayWindow>::New(this, isTeam);
			AddChild(wnd.GetPointerOrNull());
			wnd->SetBounds(GetBounds());
			sayWindow = wnd.GetPointerOrNull();
			GetManager().SetActiveElement(sayWindow->field);
		}

		void ChatLogWindow::OnTeamChat(UIElement&) { OnChat(true); }
		void ChatLogWindow::OnGlobalChat(UIElement&) { OnChat(false); }

		void ChatLogWindow::HotKey(const std::string& key) {
			if (sayWindow != nullptr) {
				UIElement::HotKey(key);
				return;
			}

			if (IsEnabled() &&
			    (EqualsIgnoringCase(key, cg_keyChatLog) || key == "Escape"))
				Close();
			else if (IsEnabled() && ui->IsChatEnabled() &&
			         EqualsIgnoringCase(key, cg_keyTeamChat))
				OnTeamChat(*this);
			else if (IsEnabled() && ui->IsChatEnabled() &&
			         EqualsIgnoringCase(key, cg_keyGlobalChat))
				OnGlobalChat(*this);
			else
				UIElement::HotKey(key);
		}

		void ChatLogWindow::Record(const std::string& text, Vector4 color) {
			color.x = Mix(color.x, 1.0F, 0.5F);
			color.y = Mix(color.y, 1.0F, 0.5F);
			color.z = Mix(color.z, 1.0F, 0.5F);
			color.w = 1.0F;
			viewer->AddLine(text, IsVisible(), color);
		}

		void ChatLogWindow::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contentsTop - 14.0F, sz.x, 1.0F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contentsTop + contentsHeight + 14.0F, sz.x, 1.0F));

			UIElement::Render();
		}
	} // namespace client
} // namespace spades
