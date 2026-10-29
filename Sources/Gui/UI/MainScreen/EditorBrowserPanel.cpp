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

#include "EditorBrowserPanel.h"

#include <algorithm>

#include <Core/LocalFileSystem.h>
#include <Core/Strings.h>
#include <Gui/MainScreenHelper.h>
#include <Gui/UI/Editor/Shell/EditorFileDialog.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Widgets/TextPromptScreen.h>

namespace {
	// The tab's own layout: the action row sits at y=200 and the list header at
	// `headerPos`, with the browser's error line fitting under the list.
	const float kActionRowY = 200.0F;
	const float kActionRowH = 30.0F;
	const float kErrorRowH = 25.0F;

	// The "New" prompt: the question, then a button per document type.
	const float kPromptButtonsTop = 70.0F;
	const float kPromptButtonPitch = 40.0F;
	const float kPromptButtonW = 200.0F;
	const float kPromptButtonH = 30.0F;
	const float kPromptBottomGap = 10.0F;
} // namespace

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::Label;
		using ui::UIElement;
		using ui::UIManager;

		// -- NewDocumentPrompt --

		NewDocumentPrompt::NewDocumentPrompt(UIElement* owner)
		    : UIElement(&owner->GetManager()), owner(owner) {
			SetFont(GetManager().GetRootElement().GetFont());
			SetBounds(owner->GetBounds());

			UIManager* manager = &GetManager();
			const std::vector<DocumentType>& types = DocumentTypes();
			float sw = manager->screenWidth;
			float sh = manager->screenHeight;
			float w = std::min(sw - 16.0F, 500.0F);
			float h = kPromptButtonsTop + kPromptButtonPitch * float(types.size()) + kPromptBottomGap;
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
			for (std::size_t i = 0; i < types.size(); i++) {
				const DocumentType& type = types[i];
				Handle<Button> btn = Handle<Button>::New(manager);
				btn->caption = type.description;
				btn->SetBounds(AABB2(x + (w - kPromptButtonW) * 0.5F,
				                     y + kPromptButtonsTop + kPromptButtonPitch * float(i),
				                     kPromptButtonW, kPromptButtonH));
				btn->activated = [this, &type](UIElement&) { Choose(type); };
				btn->enable = type.editable; // listed, but no editor opens it yet
				AddChild(btn.GetPointerOrNull());
			}
		}

		void NewDocumentPrompt::Choose(const DocumentType& type) {
			result = &type;
			Close();
		}

		void NewDocumentPrompt::Close() {
			Handle<NewDocumentPrompt> keepAlive(this);
			owner->enable = true;
			GetParent()->RemoveChild(this);
			if (closed)
				closed(*this);
		}

		void NewDocumentPrompt::Run() {
			owner->enable = false;
			owner->GetParent()->AddChild(this);
		}

		void NewDocumentPrompt::HotKey(const std::string& key) {
			if (IsEnabled() && key == "Escape") {
				result = nullptr;
				Close();
			} else {
				UIElement::HotKey(key);
			}
		}

		// -- EditorBrowserPanel --

		EditorBrowserPanel::EditorBrowserPanel(UIManager* manager, MainScreenHelper* helper,
		                                       UIElement* modalOwner, float contentsLeft,
		                                       float contentsWidth, float headerPos,
		                                       float headerHeight, float listPos, float footerPos)
		    : UIElement(manager), helper(helper), modalOwner(modalOwner) {
			SetBounds(AABB2(0.0F, 0.0F, manager->screenWidth, manager->screenHeight));

			// The options every editor dialog shares; this tab is the embedded one,
			// so it drives the browser itself instead of showing a footer.
			FileBrowserOptions options = EditorBrowserOptions();
			options.purpose = FileBrowserPurpose::Open;
			// Restore the last-used folder; the browser falls back to Home if it is
			// gone.
			options.initialDir = EditorRememberedFolder(EditorHomeDir());
			options.homeDir = EditorHomeDir();
			options.showFooter = false;    // the tab itself has no OK/Cancel row
			options.showListHeader = true; // "Name", like the other tabs
			// Line the rows up with the tab's own layout (action row, header, list).
			options.listTopGap = headerPos - kActionRowY - kActionRowH;
			{
				FileBrowserAction action;
				action.caption = _Tr("MainScreen", "New");
				action.width = 105.0F;
				action.run = [this] { OnNew(); };
				options.extraActions.push_back(std::move(action));
			}

			Handle<FileBrowserView> view =
			  Handle<FileBrowserView>::New(manager, std::move(options), modalOwner);
			browser = view.GetPointerOrNull();
			browser->SetBounds(AABB2(contentsLeft, kActionRowY, contentsWidth,
			                         (footerPos - 44.0F + kErrorRowH) - kActionRowY));
			browser->accepted = [this](const FileBrowserResult& result) {
				if (!result.paths.empty())
					OpenDocument(result.paths.front(), false);
			};
			browser->directoryChanged = [](const std::string& dir) {
				EditorRememberFolder(dir); // the editors' own dialogs open here too
			};
			browser->entryRejected = [this](const FileBrowserEntry& entry) {
				OnEntryRejected(entry);
			};
			// Typing a path that does not exist yet creates that document, a model
			// unless the name says otherwise.
			browser->unknownPathSubmitted = [this](const std::string& path) {
				OpenDocument(DocumentFileName(path, DocumentKind::Model), true);
			};
			AddChild(browser);

			// The browser may have fallen back to another folder (the remembered one
			// could be gone), and it settles that before anyone can subscribe.
			EditorRememberFolder(browser->GetDirectory());

			(void)headerHeight;
			(void)listPos;
		}

		void EditorBrowserPanel::SubmitDefault() { browser->SubmitDefault(); }
		void EditorBrowserPanel::Refresh() { browser->Refresh(); }

		void EditorBrowserPanel::OpenDocument(const std::string& absPath, bool isNew) {
			std::string msg = helper->OpenEditor(absPath, isNew);
			if (msg.size() > 0) {
				Handle<AlertScreen> al = Handle<AlertScreen>::New(modalOwner, msg);
				al->Run();
			}
		}

		void EditorBrowserPanel::OnEntryRejected(const FileBrowserEntry&) {
			Handle<AlertScreen> al =
			  Handle<AlertScreen>::New(modalOwner, UnsupportedDocumentMessage(), 120.0F);
			al->Run();
		}

		std::string EditorBrowserPanel::ValidateNewName(const std::string& name) const {
			std::string reason;
			if (!LocalFileSystem::IsValidFileName(name, &reason))
				return reason;
			if (LocalFileSystem::Exists(LocalFileSystem::Join(browser->GetDirectory(), name)))
				return _Tr("MainScreen", "An item named '{0}' already exists.", name);
			return std::string();
		}

		void EditorBrowserPanel::OnNew() {
			Handle<NewDocumentPrompt> prompt = Handle<NewDocumentPrompt>::New(modalOwner);
			prompt->closed = [this](UIElement& s) { OnNewTypeClosed(s); };
			prompt->Run();
		}

		void EditorBrowserPanel::OnNewTypeClosed(UIElement& sender) {
			NewDocumentPrompt* p = dynamic_cast<NewDocumentPrompt*>(&sender);
			if (!p || !p->result)
				return;
			newKind = p->result->kind;

			TextPromptScreen::Options options;
			options.title = _Tr("MainScreen", "New {0}", std::string(p->result->description));
			options.initialText = "untitled";
			options.validate = [this](const std::string& name) {
				// An extension on its own would silently become a hidden, nameless file.
				std::string file = DocumentFileName(name, newKind);
				if (file.size() <= DefaultExtension(newKind).size())
					return _Tr("MainScreen", "The name is empty.");
				return ValidateNewName(file);
			};
			Handle<TextPromptScreen> namePrompt =
			  Handle<TextPromptScreen>::New(modalOwner, std::move(options));
			namePrompt->closed = [this](UIElement& s) { OnNewNameClosed(s); };
			namePrompt->Run();
		}

		void EditorBrowserPanel::OnNewNameClosed(UIElement& sender) {
			TextPromptScreen* p = dynamic_cast<TextPromptScreen*>(&sender);
			if (!p || !p->GetResult())
				return;
			OpenDocument(LocalFileSystem::Join(browser->GetDirectory(),
			                                   DocumentFileName(p->GetText(), newKind)),
			             true);
		}
	} // namespace gui
} // namespace spades
