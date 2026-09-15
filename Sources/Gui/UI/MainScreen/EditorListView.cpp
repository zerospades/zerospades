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

#include "EditorListView.h"

#include <cctype>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::SetColorNP;
		using ui::UIElement;
		using ui::UIManager;

		namespace {
			std::string ToLowerExt(const std::string& name, size_t len) {
				if (name.size() < len)
					return std::string();
				std::string ext = name.substr(name.size() - len);
				for (char& c : ext)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				return ext;
			}
		} // namespace

		bool EditorIsEditable(const std::string& name) { return ToLowerExt(name, 4) == ".kv6"; }

		bool EditorIsModelFile(const std::string& name) {
			return ToLowerExt(name, 4) == ".kv6" || ToLowerExt(name, 5) == ".2kv6" ||
			       ToLowerExt(name, 4) == ".vxl";
		}

		// -- EditorListItem --

		EditorListItem::EditorListItem(UIManager* manager, EditorEntry entry)
		    : ui::ButtonBase(manager), entry(std::move(entry)) {}

		void EditorListItem::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font)
				return;

			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			Vector4 bgcolor = MakeVector4(1.0F, 1.0F, 1.0F, 0.0F);
			if (pressed && hover)
				bgcolor.w = 0.3F;
			else if (hover)
				bgcolor.w = 0.15F;
			SetColorNP(r, bgcolor);
			r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));

			Vector4 fg;
			std::string label;
			if (entry.isFolder) {
				fg = MakeVector4(0.55F, 0.78F, 1.0F, 1.0F);
				label = entry.name + "/";
			} else if (EditorIsEditable(entry.name)) {
				fg = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
				label = entry.name;
			} else {
				fg = MakeVector4(0.55F, 0.55F, 0.55F, 1.0F);
				label = entry.name + "   (not implemented)";
			}
			font->Draw(label, pos + MakeVector2(6.0F, 2.0F), 1.0F, fg);
		}

		// -- EditorListModel --

		EditorListModel::EditorListModel(UIManager* manager, std::vector<EditorEntry> entries)
		    : manager(manager), entries(std::move(entries)) {
			itemElements.resize(this->entries.size());
		}

		void EditorListModel::OnItemClicked(UIElement& sender) {
			EditorListItem* item = dynamic_cast<EditorListItem*>(&sender);
			if (item && itemActivated)
				itemActivated(item->GetEntry().name, item->GetEntry().isFolder);
		}

		void EditorListModel::OnItemDoubleClicked(UIElement& sender) {
			EditorListItem* item = dynamic_cast<EditorListItem*>(&sender);
			if (item && itemDoubleClicked)
				itemDoubleClicked(item->GetEntry().name, item->GetEntry().isFolder);
		}

		Handle<UIElement> EditorListModel::CreateElement(int row) {
			if (!itemElements[row]) {
				Handle<EditorListItem> i = Handle<EditorListItem>::New(manager, entries[row]);
				i->activated = [this](UIElement& s) { OnItemClicked(s); };
				i->doubleClicked = [this](UIElement& s) { OnItemDoubleClicked(s); };
				itemElements[row] = i;
			}
			return itemElements[row].Cast<UIElement>();
		}
	} // namespace gui
} // namespace spades
