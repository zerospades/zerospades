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

#include "FileBrowserView.h"

#include <algorithm>

#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Field.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/ListView.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Widgets/TextPromptScreen.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Field;
		using ui::Label;
		using ui::ListView;
		using ui::UIElement;
		using ui::UIManager;
		namespace fs = LocalFileSystem;

		namespace {
			const float kRowH = 30.0F;
			const float kGap = 5.0F;
			const float kErrorH = 20.0F;
			const float kHeaderH = 24.0F;
			const float kNavButtonW = 55.0F;
			const float kActionButtonW = 100.0F;
			const float kConfirmButtonW = 150.0F;
			const float kFilterButtonW = 160.0F;

			std::string Trim(const std::string& s) {
				size_t from = s.find_first_not_of(" \t");
				if (from == std::string::npos)
					return std::string();
				size_t to = s.find_last_not_of(" \t");
				return s.substr(from, to - from + 1);
			}

			const FileFilter& AllFilesFilter() {
				static const FileFilter filter{"All files", {}};
				return filter;
			}
		} // namespace

		FileBrowserView::FileBrowserView(UIManager* manager, FileBrowserOptions opts,
		                                 UIElement* modalOwner)
		    : UIElement(manager), options(std::move(opts)),
		      modalOwner(modalOwner ? modalOwner : this) {
			// Save and Create name exactly one path.
			if (options.purpose != FileBrowserPurpose::Open)
				options.multiSelect = false;

			dir = options.initialDir;
			BuildWidgets();
			Refresh(); // resolves `dir` to a folder that exists
		}

		const FileFilter& FileBrowserView::ActiveFilter() const {
			if (filterIndex < options.filters.size())
				return options.filters[filterIndex];
			return AllFilesFilter();
		}

		std::string FileBrowserView::DefaultConfirmCaption() const {
			switch (options.purpose) {
				case FileBrowserPurpose::Save: return _Tr("FileBrowser", "Save");
				case FileBrowserPurpose::Create: return _Tr("FileBrowser", "Create");
				default: return _Tr("FileBrowser", "Open");
			}
		}

		std::string FileBrowserView::Child(const std::string& name) const {
			return fs::Join(dir, name);
		}

		void FileBrowserView::BuildWidgets() {
			UIManager* manager = &GetManager();

			{
				Handle<Field> field = Handle<Field>::New(manager);
				pathField = field.GetPointerOrNull();
				pathField->placeholder = _Tr("FileBrowser", "Type a path and press [Enter]");
				// The end of a path (current folder / file name) matters most.
				pathField->elision = ui::FieldElision::Start;
				AddChild(pathField);
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				homeButton = button.GetPointerOrNull();
				homeButton->caption = _Tr("FileBrowser", "Home");
				homeButton->activated = [this](UIElement&) { OnHome(); };
				AddChild(homeButton);
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				upButton = button.GetPointerOrNull();
				upButton->caption = _Tr("FileBrowser", "Up");
				upButton->activated = [this](UIElement&) { OnUp(); };
				AddChild(upButton);
			}
			if (options.allowCreateFolder) {
				Handle<Button> button = Handle<Button>::New(manager);
				newFolderButton = button.GetPointerOrNull();
				newFolderButton->caption = _Tr("FileBrowser", "New Folder");
				newFolderButton->activated = [this](UIElement&) { OnNewFolder(); };
				AddChild(newFolderButton);
			}
			if (options.allowRename) {
				Handle<Button> button = Handle<Button>::New(manager);
				renameButton = button.GetPointerOrNull();
				renameButton->caption = _Tr("FileBrowser", "Rename");
				renameButton->activated = [this](UIElement&) { OnRename(); };
				AddChild(renameButton);
			}
			if (options.allowDelete) {
				Handle<Button> button = Handle<Button>::New(manager);
				deleteButton = button.GetPointerOrNull();
				deleteButton->caption = _Tr("FileBrowser", "Delete");
				deleteButton->activated = [this](UIElement&) { OnDelete(); };
				AddChild(deleteButton);
			}
			for (size_t i = 0; i < options.extraActions.size(); i++) {
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = options.extraActions[i].caption;
				size_t index = i;
				button->activated = [this, index](UIElement&) {
					if (options.extraActions[index].run)
						options.extraActions[index].run();
				};
				actionButtons.push_back(button.GetPointerOrNull());
				AddChild(button.GetPointerOrNull());
			}
			if (options.showListHeader) {
				Handle<Label> label = Handle<Label>::New(manager);
				header = label.GetPointerOrNull();
				header->text = _Tr("FileBrowser", "Name");
				header->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(header);
			}
			{
				Handle<ListView> listView = Handle<ListView>::New(manager);
				list = listView.GetPointerOrNull();
				AddChild(list);
			}
			{
				Handle<Label> label = Handle<Label>::New(manager);
				errorLabel = label.GetPointerOrNull();
				errorLabel->textColor = MakeVector4(1.0F, 0.45F, 0.4F, 1.0F);
				errorLabel->textScale = 0.8F;
				errorLabel->alignment = MakeVector2(0.0F, 0.5F);
				AddChild(errorLabel);
			}
			if (options.showFooter && options.purpose != FileBrowserPurpose::Open) {
				Handle<Field> field = Handle<Field>::New(manager);
				nameField = field.GetPointerOrNull();
				nameField->placeholder = _Tr("FileBrowser", "Name");
				nameField->SetText(options.initialName);
				nameField->SelectAll();
				AddChild(nameField);
			}
			if (options.showFooter && options.filters.size() > 1) {
				Handle<Button> button = Handle<Button>::New(manager);
				filterButton = button.GetPointerOrNull();
				filterButton->caption = ActiveFilter().label;
				filterButton->activated = [this](UIElement&) { OnFilterCycle(); };
				AddChild(filterButton);
			}
			if (options.showFooter) {
				{
					Handle<Button> button = Handle<Button>::New(manager);
					confirmButton = button.GetPointerOrNull();
					confirmButton->caption = options.confirmCaption.empty()
					                           ? DefaultConfirmCaption()
					                           : options.confirmCaption;
					confirmButton->activated = [this](UIElement&) { Confirm(); };
					AddChild(confirmButton);
				}
				{
					Handle<Button> button = Handle<Button>::New(manager);
					cancelButton = button.GetPointerOrNull();
					cancelButton->caption = _Tr("FileBrowser", "Cancel");
					cancelButton->activated = [this](UIElement&) { Cancel(); };
					AddChild(cancelButton);
				}
			}
			LayoutWidgets();
		}

		void FileBrowserView::OnResized() {
			LayoutWidgets();
			UIElement::OnResized();
		}

		void FileBrowserView::LayoutWidgets() {
			if (!pathField)
				return; // still being built

			float width = size.x;

			// --- action row: buttons right-aligned, path field takes the rest ---
			// Right to left: Delete, Rename, the owner's actions, New Folder, Up, Home.
			float x = width;
			if (deleteButton) {
				x -= kActionButtonW;
				deleteButton->SetBounds(AABB2(x, 0.0F, kActionButtonW, kRowH));
				x -= kGap;
			}
			if (renameButton) {
				x -= kActionButtonW;
				renameButton->SetBounds(AABB2(x, 0.0F, kActionButtonW, kRowH));
				x -= kGap;
			}
			for (size_t i = options.extraActions.size(); i > 0; i--) {
				float w = options.extraActions[i - 1].width;
				x -= w;
				actionButtons[i - 1]->SetBounds(AABB2(x, 0.0F, w, kRowH));
				x -= kGap;
			}
			if (newFolderButton) {
				x -= kActionButtonW;
				newFolderButton->SetBounds(AABB2(x, 0.0F, kActionButtonW, kRowH));
				x -= kGap;
			}
			x -= kNavButtonW;
			upButton->SetBounds(AABB2(x, 0.0F, kNavButtonW, kRowH));
			x -= kGap + kNavButtonW;
			homeButton->SetBounds(AABB2(x, 0.0F, kNavButtonW, kRowH));
			x -= kGap;
			pathField->SetBounds(AABB2(0.0F, 0.0F, std::max(60.0F, x), kRowH));

			// --- footer, laid out from the bottom up ---
			float y = size.y;
			if (options.showFooter) {
				float bx = width - kConfirmButtonW;
				cancelButton->SetBounds(AABB2(bx, y - kRowH, kConfirmButtonW, kRowH));
				bx -= kGap + kConfirmButtonW;
				confirmButton->SetBounds(AABB2(bx, y - kRowH, kConfirmButtonW, kRowH));
				y -= kRowH + kGap;
			}
			errorLabel->SetBounds(AABB2(0.0F, y - kErrorH, width, kErrorH));
			y -= kErrorH + kGap;
			if (nameField) {
				float fieldW = width;
				if (filterButton) {
					filterButton->SetBounds(
					  AABB2(width - kFilterButtonW, y - kRowH, kFilterButtonW, kRowH));
					fieldW -= kFilterButtonW + kGap;
				}
				nameField->SetBounds(AABB2(0.0F, y - kRowH, fieldW, kRowH));
				y -= kRowH + kGap;
			} else if (filterButton) {
				filterButton->SetBounds(
				  AABB2(width - kFilterButtonW, y - kRowH, kFilterButtonW, kRowH));
				y -= kRowH + kGap;
			}

			// --- list fills what is left ---
			float listTop = kRowH + options.listTopGap;
			if (header) {
				header->SetBounds(AABB2(0.0F, listTop, width, kHeaderH));
				listTop += kHeaderH;
			}
			list->SetBounds(AABB2(0.0F, listTop, width, std::max(kRowH, y - listTop)));
		}

		void FileBrowserView::Refresh() {
			// Fall back to the nearest folder that still exists, then to home. A
			// relative path is its own parent, so stop as soon as it stops shrinking.
			std::string target = dir;
			while (!target.empty() && !fs::IsFolder(target)) {
				std::string parent = fs::ParentDir(target);
				if (parent == target)
					break;
				target = parent;
			}
			if (target.empty() || !fs::IsFolder(target))
				target = HomeDirectory();
			LoadDirectory(target);
		}

		bool FileBrowserView::LoadDirectory(const std::string& target) {
			std::vector<std::string> selected = GetSelectedNames();

			std::string error;
			Handle<FileBrowserModel> built =
			  FileBrowserModel::Build(&GetManager(), target, options.target, ActiveFilter(),
			                          options.showHidden, options.multiSelect,
			                          options.describeEntry, &error);
			if (!built) {
				// Keep showing the folder that does work: the path bar, the listing
				// and `dir` must never disagree, or actions would act on a wrong path.
				SetError(_Tr("FileBrowser", "Could not open this folder: {0}", error));
				return false;
			}

			built->selectionChanged = [this] { OnSelectionChanged(); };
			built->rowActivated = [this](int row) { OnRowActivated(row); };
			model = built;
			list->SetModel(model.GetPointerOrNull());
			list->ScrollToTop();

			bool sameFolder = target == listedDir;
			dir = target;
			listedDir = target;
			pathField->SetText(dir);
			SetError("");

			// Keep the selection across a re-listing of the same folder.
			if (sameFolder) {
				for (size_t i = 0; i < selected.size(); i++) {
					int row = model->FindRow(selected[i]);
					if (row >= 0)
						model->ClickRow(row, i > 0, false);
				}
			}

			UpdateButtons();
			if (dir != notifiedDir) {
				notifiedDir = dir;
				if (directoryChanged)
					directoryChanged(dir);
			}
			return true;
		}

		std::string FileBrowserView::HomeDirectory() const {
			if (!options.homeDir.empty() && fs::IsFolder(options.homeDir))
				return options.homeDir;
			std::vector<std::string> roots = fs::GetRoots();
			return roots.empty() ? std::string("/") : roots.front();
		}

		void FileBrowserView::Navigate(const std::string& path) {
			std::string target = fs::StripTrailingSeparators(path);
			if (target.empty())
				return;
			if (!fs::IsFolder(target)) {
				SetError(_Tr("FileBrowser", "No such folder."));
				return;
			}
			LoadDirectory(target);
		}

		std::vector<std::string> FileBrowserView::GetSelectedNames() const {
			std::vector<std::string> names;
			if (!model)
				return names;
			for (int row : model->GetSelectedRows())
				names.push_back(model->GetEntry(row).name);
			return names;
		}

		void FileBrowserView::UpdateButtons() {
			bool single = model && model->GetSingleSelectedRow() >= 0;
			if (renameButton)
				renameButton->enable = single;
			if (deleteButton)
				deleteButton->enable = single;
			upButton->enable = !fs::IsRoot(dir);
			for (size_t i = 0; i < actionButtons.size(); i++) {
				const FileBrowserAction& action = options.extraActions[i];
				actionButtons[i]->enable = action.isEnabled ? action.isEnabled() : true;
			}
		}

		void FileBrowserView::SetError(const std::string& message) {
			errorLabel->text = message;
		}

		void FileBrowserView::ShowAlert(const std::string& message) {
			Handle<AlertScreen> alert = Handle<AlertScreen>::New(modalOwner, message, 120.0F);
			alert->Run();
		}

		void FileBrowserView::OnSelectionChanged() {
			SetError("");
			// Save/Create: clicking an entry offers its name for editing.
			int row = model->GetSingleSelectedRow();
			if (nameField && row >= 0) {
				const FileBrowserEntry& entry = model->GetEntry(row);
				if (!entry.isFolder)
					nameField->SetText(entry.name);
			}
			UpdateButtons();
		}

		void FileBrowserView::OnRowActivated(int row) {
			const FileBrowserEntry& entry = model->GetEntry(row);
			if (entry.isFolder) {
				Navigate(Child(entry.name));
				return;
			}
			if (!entry.accepted) {
				RejectEntry(entry);
				return;
			}
			if (nameField)
				nameField->SetText(entry.name);
			if (options.purpose != FileBrowserPurpose::Open || !options.multiSelect)
				Confirm();
		}

		void FileBrowserView::RejectEntry(const FileBrowserEntry& entry) {
			if (entryRejected) {
				entryRejected(entry);
				return;
			}
			SetError(entry.hint.empty() ? _Tr("FileBrowser", "This item cannot be used.")
			                            : entry.hint);
		}

		void FileBrowserView::OnHome() { Navigate(HomeDirectory()); }

		void FileBrowserView::OnUp() {
			if (!fs::IsRoot(dir))
				Navigate(fs::ParentDir(dir));
		}

		void FileBrowserView::OnNewFolder() {
			TextPromptScreen::Options prompt;
			prompt.title = _Tr("FileBrowser", "New Folder");
			prompt.validate = [this](const std::string& name) {
				std::string reason;
				if (!fs::IsValidFileName(name, &reason))
					return reason;
				if (fs::Exists(Child(name)))
					return _Tr("FileBrowser", "An item named '{0}' already exists.", name);
				return std::string();
			};
			Handle<TextPromptScreen> screen =
			  Handle<TextPromptScreen>::New(modalOwner, std::move(prompt));
			screen->closed = [this](UIElement& sender) { OnNewFolderClosed(sender); };
			screen->Run();
		}

		void FileBrowserView::OnNewFolderClosed(UIElement& sender) {
			TextPromptScreen* prompt = dynamic_cast<TextPromptScreen*>(&sender);
			if (!prompt || !prompt->GetResult())
				return;
			std::string error;
			std::string name = prompt->GetText();
			if (!fs::CreateFolder(Child(name), &error))
				ShowAlert(_Tr("FileBrowser", "Could not create the folder: {0}", error));
			Refresh();
			if (model)
				model->SelectByName(name);
		}

		void FileBrowserView::OnRename() {
			int row = model ? model->GetSingleSelectedRow() : -1;
			if (row < 0)
				return;
			std::string current = model->GetEntry(row).name;

			TextPromptScreen::Options prompt;
			prompt.title = _Tr("FileBrowser", "Rename '{0}'", current);
			prompt.initialText = current;
			prompt.confirmCaption = _Tr("FileBrowser", "Rename");
			prompt.selectStem = true;
			prompt.validate = [this, current](const std::string& name) {
				std::string reason;
				if (!fs::IsValidFileName(name, &reason))
					return reason;
				if (name != current && fs::Exists(Child(name)))
					return _Tr("FileBrowser", "An item named '{0}' already exists.", name);
				return std::string();
			};
			Handle<TextPromptScreen> screen =
			  Handle<TextPromptScreen>::New(modalOwner, std::move(prompt));
			screen->closed = [this, current](UIElement& sender) {
				TextPromptScreen* p = dynamic_cast<TextPromptScreen*>(&sender);
				if (!p || !p->GetResult() || p->GetText() == current)
					return;
				std::string error;
				std::string name = p->GetText();
				if (!fs::Rename(Child(current), Child(name), false, &error))
					ShowAlert(_Tr("FileBrowser", "Could not rename: {0}", error));
				Refresh();
				if (model)
					model->SelectByName(name);
			};
			screen->Run();
		}

		void FileBrowserView::OnDelete() {
			int row = model ? model->GetSingleSelectedRow() : -1;
			if (row < 0)
				return;
			const FileBrowserEntry& entry = model->GetEntry(row);
			Handle<ConfirmScreen> prompt = Handle<ConfirmScreen>::New(
			  modalOwner, _Tr("FileBrowser", "Delete '{0}'?", entry.name));
			prompt->closed = [this](UIElement& sender) { OnDeleteClosed(sender); };
			prompt->Run();
		}

		void FileBrowserView::OnDeleteClosed(UIElement& sender) {
			ConfirmScreen* prompt = dynamic_cast<ConfirmScreen*>(&sender);
			if (!prompt || !prompt->GetResult())
				return;
			int row = model ? model->GetSingleSelectedRow() : -1;
			if (row < 0)
				return;
			std::string error;
			if (!fs::Delete(Child(model->GetEntry(row).name), &error))
				ShowAlert(_Tr("FileBrowser", "Could not delete: {0}", error));
			Refresh();
		}

		void FileBrowserView::OnFilterCycle() {
			if (options.filters.size() < 2)
				return;
			filterIndex = (filterIndex + 1) % options.filters.size();
			filterButton->caption = ActiveFilter().label;
			Refresh();
		}

		bool FileBrowserView::CollectOpenPaths(std::vector<std::string>& paths) {
			std::vector<int> rows = model ? model->GetSelectedRows() : std::vector<int>();
			if (rows.empty()) {
				// With no selection, picking a folder means "this one".
				if (options.target == FileBrowserTarget::Files) {
					SetError(_Tr("FileBrowser", "Select a file first."));
					return false;
				}
				paths.push_back(dir);
				return true;
			}

			if (rows.size() == 1) {
				const FileBrowserEntry& entry = model->GetEntry(rows[0]);
				// A folder that cannot be returned is one the user wants to enter.
				if (entry.isFolder && options.target == FileBrowserTarget::Files) {
					Navigate(Child(entry.name));
					return false;
				}
				if (!entry.accepted) {
					RejectEntry(entry);
					return false;
				}
				paths.push_back(Child(entry.name));
				return true;
			}

			for (int row : rows) {
				const FileBrowserEntry& entry = model->GetEntry(row);
				if (!entry.accepted) {
					SetError(_Tr("FileBrowser", "The selection contains items that cannot be used."));
					return false;
				}
				paths.push_back(Child(entry.name));
			}
			return true;
		}

		bool FileBrowserView::CollectTypedPath(std::string& path, bool& needsOverwriteConfirm) {
			needsOverwriteConfirm = false;
			std::string name = nameField ? Trim(nameField->GetText()) : std::string();
			if (name.empty()) {
				SetError(_Tr("FileBrowser", "Enter a name."));
				return false;
			}

			// Typing the name of a folder navigates into it instead of saving.
			if (options.target == FileBrowserTarget::Files && fs::IsFolder(Child(name))) {
				Navigate(Child(name));
				nameField->SetText("");
				return false;
			}

			std::string reason;
			if (!fs::IsValidFileName(name, &reason)) {
				SetError(reason);
				return false;
			}

			const FileFilter& filter = ActiveFilter();
			if (options.appendFilterExtension && !filter.extensions.empty() && !filter.Matches(name))
				name += filter.extensions.front();

			std::string full = Child(name);
			bool exists = fs::Exists(full);
			if (exists && fs::IsFolder(full)) {
				SetError(_Tr("FileBrowser", "A folder with this name already exists."));
				return false;
			}
			if (exists && options.purpose == FileBrowserPurpose::Create) {
				SetError(_Tr("FileBrowser", "An item named '{0}' already exists.", name));
				return false;
			}
			needsOverwriteConfirm = exists && options.purpose == FileBrowserPurpose::Save;
			path = full;
			return true;
		}

		void FileBrowserView::Confirm() {
			SetError("");
			std::vector<std::string> paths;

			if (options.purpose == FileBrowserPurpose::Open) {
				if (!CollectOpenPaths(paths))
					return;
			} else {
				std::string path;
				bool needsOverwriteConfirm = false;
				if (!CollectTypedPath(path, needsOverwriteConfirm))
					return;
				if (needsOverwriteConfirm) {
					pendingSavePath = path;
					Handle<ConfirmScreen> prompt = Handle<ConfirmScreen>::New(
					  modalOwner, _Tr("FileBrowser", "Overwrite '{0}'?", fs::GetFileName(path)));
					prompt->closed = [this](UIElement& sender) { OnOverwriteClosed(sender); };
					prompt->Run();
					return;
				}
				paths.push_back(path);
			}

			Finish(std::move(paths));
		}

		void FileBrowserView::OnOverwriteClosed(UIElement& sender) {
			ConfirmScreen* prompt = dynamic_cast<ConfirmScreen*>(&sender);
			std::string path = pendingSavePath;
			pendingSavePath.clear();
			if (!prompt || !prompt->GetResult() || path.empty())
				return;
			Finish({path});
		}

		void FileBrowserView::Finish(std::vector<std::string> paths) {
			if (options.validate) {
				std::string error = options.validate(paths);
				if (!error.empty()) {
					SetError(error);
					return;
				}
			}
			FileBrowserResult result;
			result.accepted = true;
			result.paths = std::move(paths);
			result.directory = dir;
			if (accepted)
				accepted(result);
		}

		void FileBrowserView::Cancel() {
			if (cancelled)
				cancelled();
		}

		void FileBrowserView::SubmitPath() {
			std::string typed = fs::StripTrailingSeparators(Trim(pathField->GetText()));
			if (typed.empty())
				return;
			// A bare name means an entry of the folder on screen.
			if (fs::GetFileName(typed) == typed)
				typed = Child(typed);
			if (fs::IsFolder(typed)) {
				Navigate(typed);
				return;
			}

			std::string parent = fs::ParentDir(typed);
			std::string name = fs::GetFileName(typed);
			if (parent != dir) {
				if (!fs::IsFolder(parent)) {
					SetError(_Tr("FileBrowser", "No such folder."));
					return;
				}
				if (!LoadDirectory(parent))
					return;
			}

			if (options.purpose == FileBrowserPurpose::Open) {
				if (!fs::Exists(typed)) {
					// The owner may want to accept a path that does not exist yet
					// (creating a document, for instance).
					if (unknownPathSubmitted) {
						unknownPathSubmitted(typed);
						return;
					}
					SetError(_Tr("FileBrowser", "No such file or folder."));
					return;
				}
				if (!model || !model->SelectByName(name)) {
					SetError(_Tr("FileBrowser", "This file is not listed here."));
					return;
				}
				Confirm();
				return;
			}
			if (nameField) {
				nameField->SetText(name);
				Confirm();
			}
		}

		void FileBrowserView::SubmitDefault() { HotKey("Enter"); }

		void FileBrowserView::HotKey(const std::string& key) {
			if (!IsEnabled()) {
				UIElement::HotKey(key);
				return;
			}
			if (key == "Enter") {
				UIElement* focus = GetManager().GetActiveElement();
				bool hasSelection = model && !model->GetSelectedRows().empty();
				// An untouched path bar (it shows the current folder) says nothing
				// about intent, so a selection wins over re-listing that folder.
				bool pathEdited = fs::StripTrailingSeparators(Trim(pathField->GetText())) != dir;
				if (nameField && focus == nameField)
					Confirm();
				else if (focus == pathField && pathEdited)
					SubmitPath();
				else if (hasSelection)
					Confirm();
				else
					SubmitPath();
				return;
			}
			if (key == "Escape" && options.showFooter) {
				Cancel();
				return;
			}
			UIElement::HotKey(key);
		}
	} // namespace gui
} // namespace spades
