/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

 ZeroSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 ZeroSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with ZeroSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include "FileBrowserRow.h"
#include "FileBrowserModel.h"

#include <algorithm>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::SetColorNP;
		using ui::UIManager;

		namespace {
			const float kTextInset = 6.0F; // matches the other main-menu lists
			const Vector4 kFolderColor = MakeVector4(0.55F, 0.78F, 1.0F, 1.0F);
			const Vector4 kFileColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			const Vector4 kRefusedColor = MakeVector4(0.55F, 0.55F, 0.55F, 1.0F);
		} // namespace

		FileBrowserRow::FileBrowserRow(UIManager* manager, FileBrowserModel* model, int row)
		    : ui::ButtonBase(manager), model(model), row(row) {}

		void FileBrowserRow::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			client::IFont* font = GetFont();
			if (!font || !model)
				return;

			const FileBrowserEntry& entry = model->GetEntry(row);
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			// Selection stays visible while hovering or pressing another row.
			float alpha = model->IsSelected(row) ? 0.22F : 0.0F;
			if (pressed && hover)
				alpha = 0.3F;
			else if (hover)
				alpha = std::max(alpha, 0.15F);
			if (alpha > 0.0F) {
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, alpha));
				r.DrawImage(nullptr, AABB2(pos.x + 1.0F, pos.y + 1.0F, sz.x, sz.y));
			}

			Vector4 color = entry.accepted ? (entry.isFolder ? kFolderColor : kFileColor)
			                              : kRefusedColor;
			std::string name = entry.isFolder ? entry.name + "/" : entry.name;
			float available = sz.x - kTextInset * 2.0F;

			// The hint keeps its space; the name gives way first.
			if (!entry.hint.empty()) {
				std::string hint = "   " + entry.hint;
				float hintWidth = font->Measure(hint).x;
				name = ui::ElideText(*font, name, available - hintWidth);
				name += hint;
			} else {
				name = ui::ElideText(*font, name, available);
			}

			font->Draw(name, pos + MakeVector2(kTextInset, 2.0F), 1.0F, color);
		}
	} // namespace gui
} // namespace spades
