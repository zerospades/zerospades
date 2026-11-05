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

#include "FileBrowserDialog.h"

#include <algorithm>

#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Label.h>

namespace spades {
	namespace gui {
		using ui::Label;
		using ui::UIElement;
		using ui::UIManager;

		namespace {
			const float kPadding = 16.0F;
			const float kTitleH = 28.0F;
		} // namespace

		FileBrowserDialog::FileBrowserDialog(UIElement* owner, const std::string& title,
		                                     FileBrowserOptions options)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetFont(GetManager().GetRootElement().GetFont());

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			SetBounds(AABB2(0.0F, 0.0F, sw, sh));

			float w = std::min(sw - 32.0F, 760.0F);
			float h = std::min(sh - 64.0F, 520.0F);
			float x = (sw - w) * 0.5F;
			float y = (sh - h) * 0.5F;

			{ // dim everything behind the dialog
				Handle<Label> shade = Handle<Label>::New(manager);
				shade->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.6F);
				shade->SetBounds(AABB2(0.0F, 0.0F, sw, sh));
				AddChild(shade.GetPointerOrNull());
			}
			{
				Handle<Label> panel = Handle<Label>::New(manager);
				panel->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				panel->SetBounds(AABB2(x - kPadding, y - kPadding, w + kPadding * 2.0F,
				                       h + kPadding * 2.0F));
				AddChild(panel.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = title;
				label->alignment = MakeVector2(0.0F, 0.5F);
				label->SetBounds(AABB2(x, y, w, kTitleH));
				AddChild(label.GetPointerOrNull());
			}
			{
				// Prompts raised by the browser (rename, overwrite, alerts) take this
				// dialog as their owner, so they disable it while they are up.
				Handle<FileBrowserView> view =
				  Handle<FileBrowserView>::New(manager, std::move(options), this);
				browser = view.GetPointerOrNull();
				browser->SetBounds(AABB2(x, y + kTitleH, w, h - kTitleH));
				browser->accepted = [this](const FileBrowserResult& result) { Close(result); };
				browser->cancelled = [this] {
					FileBrowserResult result;
					result.directory = browser->GetDirectory();
					Close(result);
				};
				AddChild(browser);
			}
		}

		void FileBrowserDialog::Run() {
			// The dialog sits next to its owner, which it disables while it is up.
			// An owner with no parent (a manager's root element) instead hosts the
			// dialog directly, and must stay enabled: a disabled root swallows the
			// mouse, leaving the dialog unclickable.
			if (UIElement* parent = owner->GetParent()) {
				owner->enable = false;
				disabledOwner = true;
				parent->AddChild(this);
			} else {
				owner->AddChild(this);
			}
		}

		void FileBrowserDialog::Close(const FileBrowserResult& result) {
			if (closing)
				return;
			closing = true;
			// Detaching drops the last reference in the usual case: stay alive while
			// the `closed` handler runs.
			Handle<FileBrowserDialog> keepAlive(this);
			FileBrowserResult copy = result;
			if (disabledOwner) {
				owner->enable = true;
				disabledOwner = false;
			}
			if (GetParent())
				GetParent()->RemoveChild(this);
			if (closed)
				closed(copy);
		}
	} // namespace gui
} // namespace spades
