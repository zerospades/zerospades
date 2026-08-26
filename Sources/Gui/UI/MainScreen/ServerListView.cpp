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

#include "ServerListView.h"
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/Strings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		// -- ServerListItem --

		ServerListItem::ServerListItem(UIManager* manager, MainScreenHelper* helper,
		                               Handle<MainScreenServerItem> item)
		    : ui::ButtonBase(manager), helper(helper), item(std::move(item)) {
			flagIconRenderer = Handle<FlagIconRenderer>::New(&manager->GetRenderer());
		}

		void ServerListItem::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			// adjust based on screen width
			float maxContentsWidth = 750.0F;
			float scaleF = std::min(GetManager().screenWidth / maxContentsWidth, 1.0F);

			float itemOffsetX = 2.0F;
			float itemOffsetY = 2.0F;
			float flagIconOffsetX = (itemOffsetX + 12.0F) * scaleF;
			float nameOffsetX = (itemOffsetX + 24.0F) * scaleF;
			float slotsOffsetX = 335.0F * scaleF;
			float mapNameOffsetX = 372.0F * scaleF;
			float gameModeOffsetX = 580.0F * scaleF;
			float protocolOffsetX = 658.0F * scaleF;
			float pingOffsetX = sz.x - itemOffsetX;

			Vector4 bgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 0.0F);
			Vector4 fgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);

			if (item->IsFavorite()) {
				bgcolor = MakeVector4(0.3F, 0.3F, 1.0F, 0.1F);
				fgcolor = MakeVector4(220, 220, 0, 255) / 255.0F;
			}

			if (pressed && hover) {
				bgcolor.w = 0.3F;
			} else if (hover) {
				bgcolor.w = 0.15F;
			}

			SetColorNP(r, bgcolor);
			r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));

			Vector4 col = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (!hover && !item->IsFavorite() && item->GetNumPlayers() == 0) {
				col.w *= 0.5F;
				fgcolor.w *= 0.5F;
			}

			// Draw server country flag icon
			SetColorNP(r, col);
			flagIconRenderer->DrawIcon(
			    item->GetCountry(), pos + MakeVector2(flagIconOffsetX, itemOffsetY + (sz.y * 0.5F)));

			// Draw server name
			font->Draw(item->GetName(), pos + MakeVector2(nameOffsetX, itemOffsetY), 1.0F, fgcolor);

			// Draw server slots
			std::string playersStr =
			    std::to_string(item->GetNumPlayers()) + "/" + std::to_string(item->GetMaxPlayers());
			Vector4 playersCol = col;
			if (item->GetNumPlayers() >= item->GetMaxPlayers())
				playersCol = MakeVector4(1.0F, 0.7F, 0.7F, col.w);
			else if (item->GetNumPlayers() >= item->GetMaxPlayers() * 3 / 4)
				playersCol = MakeVector4(1.0F, 1.0F, 0.7F, col.w);
			else if (item->GetNumPlayers() == 0)
				playersCol = MakeVector4(0.7F, 0.7F, 1.0F, col.w);
			font->Draw(playersStr,
			           pos + MakeVector2(slotsOffsetX - font->Measure(playersStr).x * 0.5F,
			                             itemOffsetY),
			           1.0F, playersCol);

			// Draw map name
			font->Draw(item->GetMapName(), pos + MakeVector2(mapNameOffsetX, itemOffsetY), 1.0F,
			           col);

			// Draw server gamemode
			font->Draw(item->GetGameMode(),
			           pos + MakeVector2(gameModeOffsetX -
			                                 font->Measure(item->GetGameMode()).x * 0.5F,
			                             itemOffsetY),
			           1.0F, col);

			// Draw server protocol
			font->Draw(item->GetProtocol(),
			           pos + MakeVector2(protocolOffsetX -
			                                 font->Measure(item->GetProtocol()).x * 0.5F,
			                             itemOffsetY),
			           1.0F, col);

			// Draw server ping
			int ping = helper->GetServerPing(item->GetAddress());
			std::string pingStr = (ping == -1) ? "?" : std::to_string(ping);

			Vector4 pingCol = MakeVector4(1.0F, 1.0F, 1.0F, col.w);
			if (ping != -1) {
				int maxPing = 300;
				float ratio = static_cast<float>(std::min(ping, maxPing)) /
				              static_cast<float>(maxPing);
				float hue = (1.0F - ratio) * 240.0F / 360.0F;
				Vector3 gradient = HSV2RGB(hue, 0.9F, 1.0F);
				pingCol.x = gradient.x;
				pingCol.y = gradient.y;
				pingCol.z = gradient.z;
			}

			font->Draw(pingStr,
			           pos + MakeVector2(pingOffsetX - font->Measure(pingStr).x, itemOffsetY), 1.0F,
			           pingCol);
		}

		// -- ServerListModel --

		ServerListModel::ServerListModel(UIManager* manager, MainScreenHelper* helper,
		                                 std::vector<Handle<MainScreenServerItem>> list)
		    : manager(manager), helper(helper), list(std::move(list)) {
			itemElements.resize(this->list.size());
		}

		void ServerListModel::ReplaceList(std::vector<Handle<MainScreenServerItem>> newList) {
			SPAssert(newList.size() == list.size());
			list = std::move(newList);

			// Kinda dirty hack
			for (size_t i = 0, count = list.size(); i < count; ++i) {
				if (itemElements[i])
					itemElements[i]->item = list[i];
			}
		}

		void ServerListModel::OnItemClicked(UIElement& sender) {
			ServerListItem* item = dynamic_cast<ServerListItem*>(&sender);
			if (item && itemActivated)
				itemActivated(*item->item);
		}

		void ServerListModel::OnItemDoubleClicked(UIElement& sender) {
			ServerListItem* item = dynamic_cast<ServerListItem*>(&sender);
			if (item && itemDoubleClicked)
				itemDoubleClicked(*item->item);
		}

		void ServerListModel::OnItemRightClicked(UIElement& sender) {
			ServerListItem* item = dynamic_cast<ServerListItem*>(&sender);
			if (item && itemRightClicked)
				itemRightClicked(*item->item);
		}

		Handle<UIElement> ServerListModel::CreateElement(int row) {
			if (!itemElements[row]) {
				Handle<ServerListItem> i =
				    Handle<ServerListItem>::New(manager, helper, list[row]);
				i->activated = [this](UIElement& s) { OnItemClicked(s); };
				i->doubleClicked = [this](UIElement& s) { OnItemDoubleClicked(s); };
				i->rightClicked = [this](UIElement& s) { OnItemRightClicked(s); };
				itemElements[row] = i;
			}
			return itemElements[row].Cast<UIElement>();
		}

		// -- ServerListHeader --

		void ServerListHeader::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * alignment;

			if (alignment.x == 0.0F)
				txtPos.x += 2.0F;

			if (pressed && hover)
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.3F));
			else if (hover)
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.15F));
			else
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.0F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
		}

		// -- placeholders --

		void MainScreenServerListLoadingView::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			std::string text = _Tr("MainScreen", "Fetching server list...");

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;

			font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.8F));
		}

		void MainScreenServerListErrorView::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			std::string text = _Tr("MainScreen", "Failed to fetch the server list.");

			Vector2 txtSize = font->Measure(text);
			Vector2 txtPos = pos + (sz - txtSize) * 0.5F;

			font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.8F));
		}
	} // namespace gui
} // namespace spades
