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
#include <Gui/UI/Components/ContextMenu.h>
#include <Gui/UI/Components/EditorMenu.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserTypes.h>
#include <Gui/UI/Components/SoftwareCursor.h>
#include <Gui/UI/Editor/Shell/EditorCamera.h>
#include <Gui/UI/Editor/Shell/NavigationCube.h>
#include <Gui/UI/Editor/Voxel/ObjectModeHost.h>
#include <Gui/UI/Editor/Voxel/VoxelEditorHost.h>
#include <Gui/View.h>

#include "KV6Document.h"
#include "KV6Scene.h"
#include "LowPolyTool.h"
#include "SceneTransformTarget.h"
#include "SceneTreePanel.h"

namespace spades {
	namespace client {
		class FontManager;
		class IModel;
	} // namespace client
	namespace gui {
		class EditorUI;
		class VoxelEditor;

		/**
		 * In-app KV6 voxel model editor, which also opens .2kv6 scenes.
		 *
		 * Hosted by `MainScreen` as a `subview` (like the game `Client`), so the
		 * Runner forwards input and frame events to it. It hosts a VoxelEditor,
		 * which does all the editing, over a KV6Document: the view adds the
		 * orbiting camera with its navigation cube, the lit and outlined render
		 * of the model over a ground grid, and the model files (new, open, save,
		 * insert). It consumes relative mouse motion and draws a software cursor
		 * for the 2D UI (which may be shared across views).
		 *
		 * With a scene open, the editor has Object mode (see IObjectModeHost):
		 * the scene's objects are shown in place, with their tree down the left
		 * (SceneTreePanel); one is picked and placed with the gizmo, and Edit
		 * mode edits its voxels, the document being them for as long as it
		 * lasts.
		 */
		class KV6EditorView : public View,
		                      public IVoxelEditorHost,
		                      public IObjectModeHost,
		                      public IEditorMenuHost {
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
			void DocumentReframed(const Vector3& shift) override;
			IObjectModeHost* ObjectMode() override { return scene ? this : nullptr; }
			void ReleaseHeldKeys() override { cam.ReleaseHeld(); }
			void PickImageFile(const std::string& title,
			                   std::function<void(const std::string&)> picked) override;

			// --- IObjectModeHost ----------------------------------------------
			void SceneBox(Vector3& lo, Vector3& hi) const override { scene->Box(lo, hi); }
			bool PickObjectAtCursor() override;
			bool SelectObjectAtCursor(SelectHow how) override;
			int SelectObjectsOnScreen(const std::function<bool(const Vector2&)>& inside, bool through,
			                          bool removing) override;
			bool HasSelectedObjects() const override;
			void SelectNoObject() override;
			bool HasPickedObject() const override;
			std::string PickedObjectName() const override;
			bool PickedObjectPose(GizmoPose& pose) const override;
			void BeginObjectDrag() override;
			void PreviewObjectDrag(const GizmoTransform& total) override;
			void CommitObjectDrag() override;
			void CancelObjectDrag() override;
			bool CanUndoObjects() const override;
			bool CanRedoObjects() const override;
			void UndoObjects() override;
			void RedoObjects() override;
			void EditModeChanged(bool entering) override;
			ITransformTarget& ObjectTransform() override;

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
			// The scene open, when the file is a .2kv6; the document is then its
			// picked object's voxels while Edit mode lasts.
			std::unique_ptr<KV6Scene> scene;
			// How far the voxels of the object being edited were relabelled since
			// Edit mode began on it (a volume that grew), which the object makes up
			// for so it stays where it was.
			Vector3 editShift = MakeVector3(0.0F, 0.0F, 0.0F);
			// The document's state when the object's voxels were last taken back.
			long editedState = 0;
			/** Whether the scene's objects are shown (Object mode on a scene). */
			bool ShowingObjects() const;

			// The scene's tree, shown with its objects. It takes the presses on
			// its rows before the tools; a press the tools took stays theirs
			// until it is released, wherever the cursor goes.
			std::unique_ptr<SceneTreePanel> tree;
			bool leftToEditor = false, rightToEditor = false;
			bool metaHeld = false; // Cmd, which adds to a selection as Ctrl does
			bool altHeld = false;  // Option, which moves a caret by words on macOS
			/** Locks or unlocks the node at `path`, as a template holds it. */
			void SetNodeLocked(const NodePath& path, bool locked);
			/** Unlocks it once the player has been told what that gives up. */
			void ConfirmUnlock(const NodePath& path);
			/** Starts typing a new name for the node at `path`, in its row. */
			void RenameInTree(const NodePath& path);
			/** Settles a name being typed before the tree's rows are used for
			 *  anything else; if it is refused, it is given up, and said why. */
			void SettleRename();
			/** Whether a press at the cursor goes to the tree. */
			bool TreeTakesPress() const;
			// Objects copied, kept while other scenes are opened, to paste there.
			std::vector<KV6Scene::CopiedObject> objectClipboard;
			/** The menu of the row under the cursor, if any. */
			void OpenTreeMenu();
			/** The menu of the object under the cursor in the view, selecting
			 *  it first if it is not; false when none is there to have one. */
			bool OpenObjectMenu();
			/** The menu of the node at `row` (the scene, for an empty path). */
			void OpenNodeMenu(const NodePath& row);
			// The tree's commands, on nodes the menu was opened on.
			void CopyObjects(const std::vector<NodePath>& nodes);
			void PasteObjects(const NodePath& parent);
			void DeleteObjects(const std::vector<NodePath>& nodes);
			/** Asks for a .kv6 or .2kv6, from the scene's folder, to add under
			 *  `parent`. */
			void OpenAddChildFile(const NodePath& parent);
			/** A new object of one voxel, as a new model starts, under `parent`. */
			void AddObjectUnder(const NodePath& parent);
			/** The model or scene at `file` under `parent`, named after it. */
			void AddFileUnder(const NodePath& parent, const std::string& file);
			/** After a node was added: shows it, says so, gives it the gizmo. */
			void ShowAdded();
			/** Where the document's dialogs open: its folder, or for one never
			 *  saved, wherever the player was last. */
			std::string DialogFolder() const;
			/** Hands the voxels being edited back to the picked object, which
			 *  stays where it was whatever its volume did. */
			void TakeBackEditedObject();
			/** Replaces the document with the scene at `path`, in Object mode.
			 *  False leaves everything untouched. */
			bool LoadScene(const std::string& path);
			/** Opens a scene of one new model at `path`, unsaved. */
			void NewScene(int n, const std::string& path);
			/** Closes the scene, back to editing a lone model. */
			void CloseScene();
			/** Makes the lone model open a scene of one object, named after its
			 *  file, shown in Object mode; the scene is saved as a new .2kv6. */
			void ConvertToScene();
			Handle<VoxelModel> SeedModel(int n);
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
			// Flies back to the view the model opened with.
			void ResetView();
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
			DocumentKind GetDocumentKind() const {
				return scene ? DocumentKind::Scene : DocumentKind::Model;
			}
			std::string GetDocumentExtension() const { return DefaultExtension(GetDocumentKind()); }
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
			 * document's folder; `picked` gets the chosen path. Saving offers the
			 * type `writes` only, the document's own unless told otherwise.
			 */
			void ShowModelFileDialog(const std::string& title, FileBrowserPurpose purpose,
			                         const std::string& initialName,
			                         std::function<void(const std::string&)> picked,
			                         DocumentKind writes);
			void ShowModelFileDialog(const std::string& title, FileBrowserPurpose purpose,
			                         const std::string& initialName,
			                         std::function<void(const std::string&)> picked) {
				ShowModelFileDialog(title, purpose, initialName, std::move(picked),
				                    GetDocumentKind());
			}
			/** Asks where, then saves the node at `path` as a .kv6 (ModelOf). */
			void SaveNodeAsModel(const NodePath& path);

			// The active node's position, rotation and scale, as the editor's
			// transform panel sets them in Object mode (ObjectTransform).
			SceneTransformTarget transformTarget;
			/** Ends whatever is being typed (a name, a number) as a click
			 *  elsewhere would, before something else takes the input. */
			void SettleTyping();
			/** The modifiers held, for what is typed. */
			TextEntry::Modifiers HeldModifiers() const;

			// Low-poly models of a node, previewed in its place.
			std::unique_ptr<LowPolyTool> lowPoly;
			void OpenLowPoly(const NodePath& path);
			/** A key or button while it is open; true when taken: its own,
			 *  the camera's, and nothing that would change the scene. */
			bool LowPolyInput(const std::string& key, bool down);
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
