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

#include "KV6BrowserPanel.h"

#include <algorithm>

#include <Core/LocalFileSystem.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Widgets/TextPromptScreen.h>

DEFINE_SPADES_SETTING(cl_kv6EditorFolder, ""); // remembered folder (absolute)

namespace {
	// The tab's own layout: the action row sits at y=200 and the list header at
	// `headerPos`, with the browser's error line fitting under the list.
	const float kActionRowY = 200.0F;
	const float kActionRowH = 30.0F;
	const float kErrorRowH = 25.0F;

	// A voxel terrain map, which opens in the map editor rather than the model
	// editor.
	bool IsMapFile(const std::string& name) {
		return spades::LocalFileSystem::HasExtension(name, ".vxl");
	}
} // namespace

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Field;
		using ui::Label;
		using ui::ListView;
		using ui::UIElement;
		using ui::UIManager;

		// -- KV6ModelTypePrompt --

		KV6ModelTypePrompt::KV6ModelTypePrompt(UIElement* owner)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetFont(GetManager().GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			float w = std::min(sw - 16.0F, 500.0F);
			float h = 160.0F;
			float x = (sw - w) * 0.5F;
			float y = (sh - h) * 0.5F;

			{
				Handle<Label> bg = Handle<Label>::New(manager);
				bg->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.9F);
				bg->SetBounds(AABB2(0.0F, y - 13.0F, size.x, h + 27.0F));
				AddChild(bg.GetPointerOrNull());
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->text = _Tr("MainScreen", "Which resource to create?");
				label->SetBounds(AABB2(x, y + 20.0F, w, 30.0F));
				label->alignment = MakeVector2(0.5F, 0.5F);
				AddChild(label.GetPointerOrNull());
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = _Tr("MainScreen", "KV6");
				btn->SetBounds(AABB2(x + (w - 150.0F) * 0.5F, y + 70.0F, 150.0F, 30.0F));
				btn->activated = [this](UIElement& s) { OnKV6(s); };
				AddChild(btn.GetPointerOrNull());
			}
			{
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = _Tr("MainScreen", "Map VXL");
				vxlButton = btn.GetPointerOrNull();
				btn->SetBounds(AABB2(x + (w - 150.0F) * 0.5F, y + 110.0F, 150.0F, 30.0F));
				btn->activated = [this](UIElement& s) { OnVXL(s); };
				btn->enable = false; // Disabled until supported
				AddChild(btn.GetPointerOrNull());
			}
		}

		void KV6ModelTypePrompt::OnKV6(UIElement&) {
			result = 0;
			Close();
		}

		void KV6ModelTypePrompt::OnVXL(UIElement&) {
			result = 1;
			Close();
		}

		void KV6ModelTypePrompt::OnCancel(UIElement&) {
			result = -1;
			Close();
		}

		void KV6ModelTypePrompt::Close() {
			Handle<KV6ModelTypePrompt> keepAlive(this);
			owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void KV6ModelTypePrompt::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void KV6ModelTypePrompt::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape") {
				OnCancel(*this);
			} else {
				UIElement::HotKey(key);
			}
		}

		// -- KV6BrowserPanel --

		KV6BrowserPanel::KV6BrowserPanel(UIManager* manager, MainScreenHelper* helper,
		                                 UIElement* modalOwner, float contentsLeft,
		                                 float contentsWidth, float headerPos, float headerHeight,
		                                 float listPos, float footerPos)
		    : UIElement(manager), helper(helper), modalOwner(modalOwner) {
			fs = Handle<KV6ScreenHelper>::New();

			SetBounds(AABB2(0.0F, 0.0F, manager->screenWidth, manager->screenHeight));

			FileBrowserOptions options;
			options.purpose = FileBrowserPurpose::Open;
			options.target = FileBrowserTarget::Files;
			// Restore the last-used folder; the browser falls back to Home if it is
			// gone.
			options.initialDir = static_cast<std::string>(cl_kv6EditorFolder);
			options.homeDir = fs->DefaultDir();
			options.filters.push_back(
			  FileFilter{_Tr("MainScreen", "Voxel models"), KV6ModelExtensions()});
			options.allowCreateFolder = true;
			options.allowRename = true;
			options.allowDelete = true;
			options.showFooter = false;    // the tab itself has no OK/Cancel row
			options.showListHeader = true; // "Name", like the other tabs
			// Line the rows up with the tab's own layout (action row, header, list).
			options.listTopGap = headerPos - kActionRowY - kActionRowH;
			// .kv6 opens in the model editor and .vxl in the map editor; the rest
			// are listed but greyed out.
			options.describeEntry = [](const LocalFileSystem::DirEntry& entry) {
				FileEntryInfo info;
				if (!entry.isFolder && !KV6IsEditable(entry.name) && !IsMapFile(entry.name)) {
					info.accepted = false;
					info.hint = _Tr("MainScreen", "(not implemented)");
				}
				return info;
			};
			{
				FileBrowserAction action;
				action.caption = _Tr("MainScreen", "New");
				action.width = 105.0F;
				action.run = [this] { OnNewModel(); };
				options.extraActions.push_back(std::move(action));
			}

			Handle<FileBrowserView> view =
			  Handle<FileBrowserView>::New(manager, std::move(options), modalOwner);
			browser = view.GetPointerOrNull();
			browser->SetBounds(AABB2(contentsLeft, kActionRowY, contentsWidth,
			                         (footerPos - 44.0F + kErrorRowH) - kActionRowY));
			browser->accepted = [this](const FileBrowserResult& result) {
				if (!result.paths.empty())
					OpenModel(result.paths.front(), false);
			};
			browser->directoryChanged = [](const std::string& dir) {
				cl_kv6EditorFolder = dir; // remember for next time
			};
			browser->entryRejected = [this](const FileBrowserEntry& entry) {
				OnEntryRejected(entry);
			};
			// Typing a path that does not exist yet creates that model, as the old
			// explorer did.
			browser->unknownPathSubmitted = [this](const std::string& path) {
				OpenModel(ModelFileName(path), true);
			};
			AddChild(browser);

			// The browser may have fallen back to another folder (the remembered one
			// could be gone), and it settles that before anyone can subscribe.
			cl_kv6EditorFolder = browser->GetDirectory();

			(void)headerHeight;
			(void)listPos;
		}

		void KV6BrowserPanel::SubmitDefault() { browser->SubmitDefault(); }
		void KV6BrowserPanel::Refresh() { browser->Refresh(); }

		void KV6BrowserPanel::OpenModel(const std::string& absPath, bool isNew) {
			std::string msg = IsMapFile(absPath) ? helper->OpenMapEditor(absPath)
			                                     : helper->OpenKV6Editor(absPath, isNew);
			if (msg.size() > 0) {
				Handle<AlertScreen> al = Handle<AlertScreen>::New(modalOwner, msg);
				al->Run();
			}
		}

		void KV6BrowserPanel::OnEntryRejected(const FileBrowserEntry&) {
			Handle<AlertScreen> al = Handle<AlertScreen>::New(
			  modalOwner,
			  _Tr("MainScreen",
			      "This file type is not supported yet. Only .kv6 and .vxl files can be edited."),
			  120.0F);
			al->Run();
		}

		std::string KV6BrowserPanel::ValidateNewName(const std::string& name) const {
			std::string reason;
			if (!LocalFileSystem::IsValidFileName(name, &reason))
				return reason;
			if (LocalFileSystem::Exists(LocalFileSystem::Join(browser->GetDirectory(), name)))
				return _Tr("MainScreen", "An item named '{0}' already exists.", name);
			return std::string();
		}

		std::string KV6BrowserPanel::ModelFileName(const std::string& name) {
			return KV6IsEditable(name) ? name : name + ".kv6";
		}

		void KV6BrowserPanel::OnNewModel() {
			Handle<KV6ModelTypePrompt> prompt = Handle<KV6ModelTypePrompt>::New(modalOwner);
			prompt->closed = [this](UIElement& s) { OnNewModelTypeClosed(s); };
			prompt->Run();
		}

		void KV6BrowserPanel::OnNewModelTypeClosed(UIElement& sender) {
			KV6ModelTypePrompt* p = dynamic_cast<KV6ModelTypePrompt*>(&sender);
			if (!p)
				return;

			if (p->result == 0) { // KV6 selected
				TextPromptScreen::Options options;
				options.title = _Tr("MainScreen", "New KV6 Model");
				options.initialText = "untitled";
				options.validate = [this](const std::string& name) {
					// ".kv6" alone would silently become a hidden, nameless file.
					std::string file = ModelFileName(name);
					if (file.size() <= 4)
						return _Tr("MainScreen", "The name is empty.");
					return ValidateNewName(file);
				};
				Handle<TextPromptScreen> namePrompt =
				    Handle<TextPromptScreen>::New(modalOwner, std::move(options));
				namePrompt->closed = [this](UIElement& s) { OnNewModelNameClosed(s); };
				namePrompt->Run();
			} else if (p->result == 1) { // VXL selected
				Handle<AlertScreen> al = Handle<AlertScreen>::New(
				    modalOwner,
				    _Tr("MainScreen",
				        "Map VXL support is not yet implemented. Please use KV6 models."),
				    120.0F);
				al->Run();
			}
		}

		void KV6BrowserPanel::OnNewModelNameClosed(UIElement& sender) {
			TextPromptScreen* p = dynamic_cast<TextPromptScreen*>(&sender);
			if (!p || !p->GetResult())
				return;
			OpenModel(LocalFileSystem::Join(browser->GetDirectory(),
			                                ModelFileName(p->GetText())),
			          true);
		}
	} // namespace gui
} // namespace spades
