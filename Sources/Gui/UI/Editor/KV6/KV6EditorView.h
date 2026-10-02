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

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <Client/IAudioDevice.h>
#include <Client/IRenderer.h>
#include <Client/SceneDefinition.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include <Gui/DocumentTypes.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserTypes.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Editor/Shell/EditorCamera.h>
#include <Gui/UI/Editor/Shell/NavigationCube.h>
#include <Gui/UI/Editor/Voxel/VoxelEditorHost.h>
#include <Gui/View.h>

#include "KV6Document.h"

namespace spades {
	namespace client {
		class FontManager;
		class IModel;
	} // namespace client
	namespace gui {
		class EditorUI;
		class VoxelEditor;

		/**
		 * In-app KV6 voxel model editor.
		 *
		 * Hosted by `MainScreen` as a `subview` (like the game `Client`), so the
		 * Runner forwards input and frame events to it. It hosts a VoxelEditor,
		 * which does all the editing, over a KV6Document: the view adds the
		 * orbiting camera with its navigation cube, the lit and outlined render
		 * of the model over a ground grid, and the model files (new, open, save,
		 * insert). It consumes relative mouse motion and draws a software cursor
		 * for the 2D UI (which may be shared across views).
		 */
		class KV6EditorView : public View, public IVoxelEditorHost, public IEditorMenuHost {
		public:
			KV6EditorView(client::IRenderer* renderer, client::IAudioDevice* audioDevice,
			              client::FontManager* fontManager, SoftwareCursor* cursor,
			              const std::string& path, bool isNew);

			void MouseEvent(float x, float y) override;
			void WheelEvent(float x, float y) override;
			void KeyEvent(const std::string&, bool down) override;
			void TextInputEvent(const std::string&) override;
			void TextEditingEvent(const std::string&, int start, int len) override;
			bool AcceptsTextInput() override;
			AABB2 GetTextInputRect() override;
			bool NeedsAbsoluteMouseCoordinate() override { return false; }

			void RunFrame(float dt) override;
			void Closing() override {}
			bool WantsToBeClosed() override { return wantsClose; }

			// --- IVoxelEditorHost ---------------------------------------------
			const GizmoView& CurrentView() const override { return cam.View(); }
			const Vector2& AimPosition() const override { return softwareCursor->GetPosition(); }
			bool KeyIsReserved(const std::string& key) const override;
			bool ClickViewportWidget(const Vector2& cursor) override;
			void DocumentReframed(const Vector3& shift) override { cam.Shift(shift); }
			void ReleaseHeldKeys() override { cam.ReleaseHeld(); }

		protected:
			~KV6EditorView();

		private:
			Handle<client::IRenderer> renderer;
			Handle<client::IAudioDevice> audioDevice;
			Handle<client::FontManager> fontManager;

			SoftwareCursor* softwareCursor = nullptr;
			std::unique_ptr<SoftwareCursor> ownedCursor; // if no cursor was provided
			Handle<EditorUI> ui;

			// --- Document -----------------------------------------------------
			KV6Document document;
			std::unique_ptr<VoxelEditor> editor;
			std::string filePath;
			// The document is dirty while its state differs from the one captured
			// at the last save (-1 = never saved).
			long savedDocumentId = -1;
			bool IsDirty() const;
			// What saving would change: the journaled edits, plus voxels still
			// waiting to be placed (a paste leaves the document itself untouched).
			bool HasUnsavedChanges() const;

			// Rebuilt from the model before a frame, when the model changed.
			Handle<client::IModel> renderModel;
			std::uint64_t renderModelVersion = ~std::uint64_t(0); // document it shows

			float globalTime = 0.0F;
			bool wantsClose = false;
			bool wantScreenShot = false; // set by the screenshot key, served at end of frame

			// --- Camera -------------------------------------------------------
			EditorCamera cam;
			NavigationCube naviCube;
			void FrameCamera();
			// Home flies back to the view the model opened with, F frames the
			// selection (or the model); true when `key` was one of them.
			bool ViewKey(const std::string& key);
			// The camera's scene for the full screen, lit and outlined for editing.
			client::SceneDefinition SetupScene(float width, float height);

			void NewModel(int n, const std::string& path);
			/** Replaces the document with the model at `path`. False leaves the
			 *  document untouched: the file could not be read. */
			bool LoadModel(const std::string& path);
			/** Writes the document to its path; false if there is none, or on error. */
			bool Save();

			// Drawing
			void DrawHelpers();
			void DrawOriginAxes();
			void DrawRibbon(float sw); // full-width title/filename bar above the toolbar
			// The keys the view handles itself, for the help line.
			std::string HelpLine() const;

			// --- IEditorMenuHost ---
			/** The document the menu is about: its file name, or that it has never
			 *  been saved, with a marker while it has changes that have not been
			 *  written. The menu is where saving is decided, so it is where the
			 *  player has to be able to see what they are deciding about. */
			std::string GetMenuTitle() override;
			std::vector<EditorMenuItem> GetMenuItems() override;
			bool OnMenuEscape() override;

			// --- Document commands behind the menu items ---
			std::string GetDocumentExtension() const { return DefaultExtension(DocumentKind::Model); }
			bool SaveDocument(const std::string& path);
			/** Asks for a path with the shared file browser, then saves to it.
			 *  `after` runs only once the document has actually been written. */
			void OpenSaveAsDialog(std::function<void()> after = std::function<void()>());
			/** Opens another model in place of this one, guarding unsaved changes. */
			void OpenDocument();
			/** Loads `path` and starts placing its voxels in the current document,
			 *  which is a paste from a file rather than a change of document. */
			void InsertModel(const std::string& path);
			/** Asks for a model with the shared file browser, then inserts it. */
			void OpenInsertDialog();
			/** Starts an empty document, once it is safe to lose this one. */
			void NewDocument();
			/**
			 * Shows the shared model file browser over the editor, starting in the
			 * document's folder; `picked` gets the chosen path.
			 */
			void ShowModelFileDialog(const std::string& title, FileBrowserPurpose purpose,
			                         const std::string& initialName,
			                         std::function<void(const std::string&)> picked);
			/** A plain message over the editor with a single way out. */
			void ShowMessage(const std::string& text);
			/**
			 * Runs `proceed` once it is safe to lose the current document: right
			 * away when it is clean, otherwise after the user picks Save or Discard.
			 */
			void ConfirmDiscardChanges(std::function<void()> proceed);
		};
	} // namespace gui
} // namespace spades
