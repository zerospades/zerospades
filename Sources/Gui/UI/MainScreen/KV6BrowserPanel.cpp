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

			// Restore the last-used folder; fall back to the home (data) folder if
			// unset or gone.
			dir = static_cast<std::string>(cl_kv6EditorFolder);
			if (dir.empty() || !fs->IsFolder(dir))
				dir = fs->DefaultDir();

			{
				Handle<Field> field = Handle<Field>::New(manager);
				pathField = field.GetPointerOrNull();
				pathField->SetBounds(AABB2(contentsLeft, 200.0F, contentsWidth - 480.0F, 30.0F));
				pathField->placeholder = _Tr("MainScreen", "Type a path and press [Enter]");
				// The end of a path (current folder / file name) matters most.
				pathField->elision = ui::FieldElision::Start;
				AddChild(pathField);
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Home");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 470.0F, 200.0F, 55.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnHome(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Up");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 410.0F, 200.0F, 55.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnUp(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "New Folder");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 350.0F, 200.0F, 100.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnNewFolder(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "New");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 245.0F, 200.0F, 105.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnNewModel(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("MainScreen", "Delete");
				button->SetBounds(
				    AABB2(contentsLeft + contentsWidth - 135.0F, 200.0F, 135.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnDelete(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Label> header = Handle<Label>::New(manager);
				header->text = _Tr("MainScreen", "Name");
				header->SetBounds(AABB2(contentsLeft, headerPos, contentsWidth, headerHeight));
				header->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(header.GetPointerOrNull());
			}
			{
				Handle<ListView> listView = Handle<ListView>::New(manager);
				list = listView.GetPointerOrNull();
				list->SetBounds(
				    AABB2(contentsLeft, listPos, contentsWidth, footerPos - listPos - 44.0F));
				AddChild(list);
			}

			Reload();
		}

		std::string KV6BrowserPanel::Child(const std::string& name) const {
			if (dir.empty())
				return name;
			char last = dir[dir.size() - 1];
			if (last == '/' || last == '\\')
				return dir + name;
			return dir + "/" + name;
		}

		void KV6BrowserPanel::Reload() {
			// Folders first, then files.
			std::vector<EditorEntry> entries;
			for (const std::string& name : fs->GetFolders(dir))
				entries.push_back(EditorEntry{name, true});
			for (const std::string& name : fs->GetFiles(dir))
				entries.push_back(EditorEntry{name, false});

			Handle<EditorListModel> model =
			    Handle<EditorListModel>::New(&GetManager(), std::move(entries));
			model->itemActivated = [this](const std::string& n, bool f) { OnItemActivated(n, f); };
			model->itemDoubleClicked = [this](const std::string& n, bool f) {
				OnItemDoubleClicked(n, f);
			};
			list->SetModel(model.GetPointerOrNull());
			currentModel = model;

			pathField->SetText(dir);
			cl_kv6EditorFolder = dir; // remember for next time
			selected = "";
			selectedIsFolder = false;
		}

		void KV6BrowserPanel::OpenModel(const std::string& absPath, bool isNew) {
			std::string msg = helper->OpenKV6Editor(absPath, isNew);
			if (msg.size() > 0) {
				Handle<AlertScreen> al = Handle<AlertScreen>::New(modalOwner, msg);
				al->Run();
			}
		}

		void KV6BrowserPanel::NotImplemented() {
			Handle<AlertScreen> al = Handle<AlertScreen>::New(
			    modalOwner,
			    _Tr("MainScreen",
			        "This file type is not supported yet. Only .kv6 files can be edited."),
			    120.0F);
			al->Run();
		}

		void KV6BrowserPanel::OnItemActivated(const std::string& name, bool isFolder) {
			selected = name;
			selectedIsFolder = isFolder;
			// Show the full path so the user can read or edit it directly.
			pathField->SetText(Child(name) + (isFolder ? "/" : ""));
		}

		void KV6BrowserPanel::OnItemDoubleClicked(const std::string& name, bool isFolder) {
			if (isFolder) {
				dir = Child(name);
				Reload();
			} else if (!EditorIsEditable(name)) {
				NotImplemented();
			} else {
				OpenModel(Child(name), false);
			}
		}

		void KV6BrowserPanel::SubmitPath() {
			std::string p = pathField->GetText();
			// Trim trailing separators (but keep a lone root "/").
			while (p.size() > 1 && (p[p.size() - 1] == '/' || p[p.size() - 1] == '\\'))
				p.erase(p.size() - 1);

			if (p.empty())
				return;
			if (fs->IsFolder(p)) {
				dir = p;
				Reload();
				return;
			}

			if (EditorIsModelFile(p) && !EditorIsEditable(p)) {
				NotImplemented();
				return;
			}
			// Treat as a .kv6 file (open existing, or create if it does not exist).
			if (!EditorIsEditable(p))
				p += ".kv6";
			OpenModel(p, !fs->Exists(p));
		}

		void KV6BrowserPanel::OnHome(UIElement&) {
			dir = fs->DefaultDir();
			Reload();
		}

		void KV6BrowserPanel::OnUp(UIElement&) {
			dir = fs->ParentDir(dir);
			Reload();
		}

		std::string KV6BrowserPanel::ValidateNewName(const std::string& name) const {
			std::string reason;
			if (!LocalFileSystem::IsValidFileName(name, &reason))
				return reason;
			if (LocalFileSystem::Exists(Child(name)))
				return _Tr("MainScreen", "An item named '{0}' already exists.", name);
			return std::string();
		}

		void KV6BrowserPanel::OnNewFolder(UIElement&) {
			TextPromptScreen::Options options;
			options.title = _Tr("MainScreen", "New Folder");
			options.validate = [this](const std::string& name) { return ValidateNewName(name); };
			Handle<TextPromptScreen> prompt =
			    Handle<TextPromptScreen>::New(modalOwner, std::move(options));
			prompt->closed = [this](UIElement& s) { OnNewFolderClosed(s); };
			prompt->Run();
		}

		void KV6BrowserPanel::OnNewFolderClosed(UIElement& sender) {
			TextPromptScreen* p = dynamic_cast<TextPromptScreen*>(&sender);
			if (!p || !p->GetResult())
				return;
			std::string error;
			if (!LocalFileSystem::CreateFolder(Child(p->GetText()), &error)) {
				Handle<AlertScreen> al = Handle<AlertScreen>::New(
				    modalOwner, _Tr("MainScreen", "Could not create the folder: {0}", error));
				al->Run();
			}
			Reload();
		}

		void KV6BrowserPanel::OnNewModel(UIElement&) {
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
			OpenModel(Child(ModelFileName(p->GetText())), true);
		}

		std::string KV6BrowserPanel::ModelFileName(const std::string& name) {
			return EditorIsEditable(name) ? name : name + ".kv6";
		}

		void KV6BrowserPanel::OnDelete(UIElement&) {
			if (selected.empty())
				return;
			Handle<ConfirmScreen> prompt = Handle<ConfirmScreen>::New(
			    modalOwner, _Tr("MainScreen", "Delete '{0}'?", selected));
			prompt->closed = [this](UIElement& s) { OnDeleteClosed(s); };
			prompt->Run();
		}

		void KV6BrowserPanel::OnDeleteClosed(UIElement& sender) {
			ConfirmScreen* p = dynamic_cast<ConfirmScreen*>(&sender);
			if (!p || !p->GetResult() || selected.empty())
				return;
			fs->Delete(Child(selected));
			Reload();
		}
	} // namespace gui
} // namespace spades
