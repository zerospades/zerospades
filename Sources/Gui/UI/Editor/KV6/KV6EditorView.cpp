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

#include "KV6EditorView.h"

#include <algorithm>
#include <exception>
#include <initializer_list>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IModel.h>
#include <Client/ScreenShot.h>
#include <Core/Bitmap.h>
#include <Gui/UI/Editor/Shell/RenderImage.h>
#include <Core/Debug.h>
#include <Core/LocalFileSystem.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Gui/AutomationOptions.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/Gizmo/TransformGizmo.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Editor/Shell/EditorFileDialog.h>
#include <Gui/UI/Editor/Shell/EditorKeys.h>
#include <Gui/UI/Editor/Shell/EditorPrompts.h>
#include <Gui/UI/Components/MenuBar.h>
#include <Gui/UI/Editor/Shell/EditorUI.h>
#include <Gui/UI/Editor/Voxel/VoxelEditor.h>
#include <Gui/UI/Editor/Voxel/VoxelOverlayLines.h>

#include "KV6ModelIO.h"

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyScreenshot);
// Which projection the model editor opens in, kept between runs: a way of
// working, not a property of what is open.
DEFINE_SPADES_SETTING(cg_editorOrthogonal, "0");

namespace spades {
	namespace gui {
		namespace {
			// The view's own Ctrl shortcuts, the document's files. Ctrl is also
			// the descend key, so a chord costs the movement key bound to the same
			// letter: keep this set clear of them.
			bool IsFileShortcut(const std::string& key) {
				for (const char* k : {"s", "n", "o"}) {
					if (EqualsIgnoringCase(key, k))
						return true;
				}
				return false;
			}

			// The editor's ground grid. It never moves: no model is deeper than
			// the KV6 limit, so a plane at that depth always sits below whatever is
			// being edited (voxel z spans [z-0.5, z+0.5], so the deepest possible
			// voxel ends half a voxel above it).
			constexpr float kGroundPlaneZ = float(KV6Document::kMaxDepth);
			constexpr int kGroundReach = 256;    // voxels from the origin, each way
			constexpr int kGroundStep = 4;       // voxels between lines
			constexpr int kGroundMajorEvery = 4; // every Nth line is brighter

			// Camera speed, in voxels per second per voxel of the model's size.
			constexpr float kCameraSpeedPerSize = 0.7F;

			// The navigation cube's box on screen, its distance from the right
			// edge and from the bars, and how far the cube sits inside the box.
			constexpr float kNaviCubeBox = 174.0F;
			constexpr float kNaviCubeMargin = 16.0F;
			constexpr float kNaviCubeTopGap = 8.0F;
			constexpr float kNaviCubeInset = 14.0F;
			// The transform panel's foot, as far up from the bottom as the
			// colour picker's, clear of the hint lines.
			constexpr float kPanelBottomGap = 44.0F;

			// Edge length of the cube a new model's volume starts as.
			constexpr int kNewModelSize = 32;
			// What framing an empty, which has no size, shows round it.
			constexpr float kEmptyFrameSize = 16.0F;

			// The scene's tree: in from the left edge, under the bars, and
			// clear of the hint lines along the bottom.
			constexpr float kTreeLeft = 14.0F;
			constexpr float kTreeTopGap = 12.0F;
			constexpr float kTreeBottomGap = 60.0F;
			constexpr float kTreeWidth = 236.0F; // as the tree lays its rows out

			// The view keys: back to the opening view, and frame the selection.
			// Named as SDL names them (Home), compared ignoring case.
			// What the view is drawn on.
			const Vector3 kBackground = MakeVector3(0.10F, 0.10F, 0.12F);
			// How many times further than the far plane the fog is put for an
			// image of the model: far enough to leave it its own colours.
			constexpr float kRenderFogReach = 1000.0F;
			// The two backgrounds an image's model is told from. As dark between
			// them as the view's own, so whatever the renderer's filters make of
			// a scene by its background they make of these what the view shows;
			// far apart in hue, so that background is unmistakable.
			const Vector3 kRenderKeyFirst = MakeVector3(0.30F, 0.00F, 0.02F);
			const Vector3 kRenderKeySecond = MakeVector3(0.00F, 0.30F, 0.02F);
			const char* const kHomeViewKey = "Home";
			// Renames the active object in the tree, as in file managers.
			const char* const kRenameKey = "F2";
			const char* const kFrameSelectionKey = "F";
			// Blender's Numpad 5, which this keyboard may not have, and the
			// digits belong to the sub-tools: the key beside F it is.
			const char* const kProjectionKey = "G";

			// Whether one of the player's key settings the view acts on (the
			// camera's, the screenshot key) is bound to `key`; those come before
			// the view keys.
			bool BoundToKeySetting(const std::string& key) {
				const std::string bound[] = {cg_keyMoveForward, cg_keyMoveBackward, cg_keyMoveLeft,
				                             cg_keyMoveRight,   cg_keyJump,         cg_keyCrouch,
				                             cg_keySprint,      cg_keyScreenshot};
				for (const std::string& b : bound) {
					if (EditorKeyMatches(b, key))
						return true;
				}
				return false;
			}

			// Where the voxels [lo, hi] are seen whole from: their middle, each
			// voxel centred on its index, and their largest extent.
			void BoxFrame(const IntVector3& lo, const IntVector3& hi, Vector3& centre,
			              float& size) {
				centre = MakeVector3(float(lo.x + hi.x), float(lo.y + hi.y), float(lo.z + hi.z)) *
				         0.5F;
				const IntVector3 extent = hi - lo + MakeIntVector3(1, 1, 1);
				size = float(std::max(extent.x, std::max(extent.y, extent.z)));
			}
		} // namespace

		KV6EditorView::KV6EditorView(client::IRenderer* r, client::IAudioDevice* dev,
		                             client::FontManager* fm, SoftwareCursor* c,
		                             const std::string& path, bool isNew)
		    : renderer(r), audioDevice(dev), fontManager(fm) {
			SPADES_MARK_FUNCTION();
			if (c) {
				softwareCursor = c;
			} else {
				ownedCursor = std::make_unique<SoftwareCursor>(*renderer);
				softwareCursor = ownedCursor.get();
			}

			// The projection the editor was last left in: a way of working, like
			// the tool keys, rather than anything about the model opened.
			cam.SetOrthogonal(static_cast<int>(cg_editorOrthogonal) != 0);

			ui = Handle<EditorUI>::New(renderer.GetPointerOrNull(), audioDevice.GetPointerOrNull(),
			                           &*fontManager, *this, softwareCursor);
			editor = std::make_unique<VoxelEditor>(document, *this, *renderer, *fontManager, *ui,
			                                       *softwareCursor, VoxelEditor::Config());
			tree = std::make_unique<SceneTreePanel>(*renderer, fontManager->GetSmallGuiFont());
			resample = std::make_unique<ResampleTool>(*renderer, fontManager->GetSmallGuiFont(),
			                                        audioDevice.GetPointerOrNull());

			// The editor always opens with a document. A file the player asked to
			// create is started under its name; one that exists but cannot be read
			// leaves an untitled document instead, because naming it after a file
			// whose contents are still on disk is how Save comes to overwrite
			// something this editor never managed to read.
			if (isNew && LocalFileSystem::HasExtension(path, DefaultExtension(DocumentKind::Scene)))
				NewScene(kNewModelSize, path);
			else if (isNew || path.empty())
				NewModel(kNewModelSize, path);
			else if (!LoadModel(path))
				NewModel(kNewModelSize, std::string());

			// --editor-reference: images to model from, shown from the start.
			for (const auto& reference : Automation().editorReferences)
				editor->ShowReferenceImage(reference.first, reference.second);

			// --editor-generate: Generate KV6 open on a node, to look it over.
			if (scene && !Automation().editorGenerate.empty())
				OpenResampleByName(Automation().editorGenerate, Automation().editorGenerateAt);
		}

		KV6EditorView::~KV6EditorView() { SPADES_MARK_FUNCTION(); }

		// --- Document ---------------------------------------------------------

		bool KV6EditorView::IsDirty() const {
			if (scene) {
				// The objects' placements, and the voxels of the one being edited
				// since they were last taken back.
				return scene->Changed() ||
				       (!ShowingObjects() && editor->DocumentStateId() != editedState);
			}
			return editor->DocumentStateId() != savedDocumentId;
		}

		bool KV6EditorView::ShowingObjects() const { return scene && editor->InObjectMode(); }

		void KV6EditorView::OpenTreeMenu() {
			NodePath row;
			if (tree->ContextRow(*scene, softwareCursor->GetPosition(), row))
				OpenNodeMenu(row);
		}

		bool KV6EditorView::OpenObjectMenu() {
			const Vector2& at = softwareCursor->GetPosition();
			Vector3 toward;
			if (!editor->IsFreeViewportPoint(at) ||
			    naviCube.ButtonAt(at, NavigationCube::kHomeButton) ||
			    naviCube.ButtonAt(at, NavigationCube::kProjectionButton) ||
			    naviCube.DirectionAt(cam.View(), at, toward))
				return false;
			const int index = scene->ObjectAt(cam.View(), at);
			if (index < 0)
				return false; // on nothing: the tools' (a box taking objects away)
			// The object becomes the selection unless it is in it already, as a
			// right press on a row of a file manager does; the menu acts on it.
			const NodePath path = scene->PathOf(index);
			if (!scene->IsSelected(path))
				scene->Select({path}, path);
			tree->Reveal(*scene, path);
			editor->ObjectSelectionChanged();
			OpenNodeMenu(path);
			return true;
		}

		void KV6EditorView::OpenNodeMenu(const NodePath& row) {
			ui->GetOptionBar()->EndEditing(true);
			editor->CancelToolInteraction();
			// Copying and deleting act on the selection, which the row is in;
			// adding and pasting go under the row.
			const bool onScene = row.empty();
			const std::vector<NodePath> selection = scene->Selection();
			const bool selected = !onScene && !selection.empty();
			const VoxelObject* node = scene->Node(row);
			const bool voxels = scene->HoldsVoxels(row);
			std::vector<ContextMenuItem> items;
			items.push_back(ContextMenuItem(
			  "Add Child",
			  std::vector<ContextMenuItem>{
			    ContextMenuItem("Empty", [this, row] { AddEmptyUnder(row); }),
			    ContextMenuItem("Single Voxel", [this, row] { AddObjectUnder(row); }),
			    ContextMenuItem("KV6 or 2KV6 File...", [this, row] { OpenAddChildFile(row); })}));
			if (!onScene)
				items.push_back(ContextMenuItem("Rename", [this, row] { RenameInTree(row); })
				                  .Enabled(node && !node->locked)
				                  .Shortcut(kRenameKey));
			items.push_back(
			  ContextMenuItem("Save as KV6...", [this, row] { SaveNodeAsModel(row); }).Enabled(voxels));
			items.push_back(
			  ContextMenuItem("Generate KV6...", [this, row] { OpenResample(row); })
			    .Enabled(voxels));
			items.push_back(ContextMenuItem("Copy", [this, selection] { CopyObjects(selection); })
			                  .Enabled(selected)
			                  .Separated());
			items.push_back(
			  ContextMenuItem(onScene ? "Paste" : "Paste Into", [this, row] { PasteObjects(row); })
			    .Enabled(!objectClipboard.empty()));
			// A template's locks, near the end: they are set once, and undone
			// only on purpose.
			if (!onScene) {
				const bool locked = node && node->locked;
				items.push_back(
				  ContextMenuItem(
				    "Templating",
				    std::vector<ContextMenuItem>{
				      ContextMenuItem("Lock", [this, row] { SetNodeLocked(row, true); }).Enabled(!locked),
				      ContextMenuItem("Unlock...", [this, row] { ConfirmUnlock(row); }).Enabled(locked)})
				    .Separated());
			}
			items.push_back(ContextMenuItem("Delete", [this, selection] { DeleteObjects(selection); })
			                  .Enabled(selected)
			                  .Separated(onScene));
			ui->GetContextMenu()->Open(softwareCursor->GetPosition(), std::move(items));
		}

		namespace {
			std::string Objects(std::size_t n) {
				return n == 1 ? std::string("1 object") : std::to_string(n) + " objects";
			}
		} // namespace

		void KV6EditorView::CopyObjects(const std::vector<NodePath>& nodes) {
			objectClipboard = scene->Copy(nodes);
			editor->SetStatus("Copied " + Objects(objectClipboard.size()));
		}

		void KV6EditorView::PasteObjects(const NodePath& parent) {
			std::string why;
			if (!scene->Paste(objectClipboard, parent, why)) {
				editor->SetStatus("Cannot paste there: " + why);
				return;
			}
			tree->Reveal(*scene, scene->Active());
			editor->SetStatus("Pasted " + Objects(scene->Selection().size()));
			editor->ObjectSelectionChanged();
		}

		void KV6EditorView::DeleteObjects(const std::vector<NodePath>& nodes) {
			std::string why;
			if (scene->Delete(nodes, why))
				editor->SetStatus("Deleted " + Objects(nodes.size()));
			else
				editor->SetStatus("Cannot delete: " + why);
			editor->ObjectSelectionChanged();
		}

		void KV6EditorView::SetNodeLocked(const NodePath& path, bool locked) {
			if (!scene || !scene->SetLocked(path, locked))
				return;
			const VoxelObject* o = scene->Node(path);
			editor->SetStatus((locked ? "Locked \"" : "Unlocked \"") + o->name + "\"");
			editor->ObjectSelectionChanged(); // the panel and the gizmo follow the lock
		}

		void KV6EditorView::ConfirmUnlock(const NodePath& path) {
			const VoxelObject* o = scene->Node(path);
			if (!o)
				return;
			editor->ReleaseHeldInput();
			// The dialog is modal: the tree cannot change under `path` meanwhile.
			ConfirmEditorAction(
			  *ui->GetOverlay(),
			  "Unlock \"" + o->name +
			    "\"? A template locks it because the game finds it by its name and its place "
			    "in the tree. Unlocked, it can be renamed, moved under another object, turned, "
			    "scaled or deleted, and the game may no longer find it.",
			  [this, path] { SetNodeLocked(path, false); });
		}

		void KV6EditorView::RenameInTree(const NodePath& path) {
			ui->GetOptionBar()->EndEditing(true);
			editor->CancelToolInteraction();
			std::string why;
			if (!tree->BeginRename(*scene, path, globalTime, why))
				editor->SetStatus("Cannot rename: " + why);
		}

		void KV6EditorView::SettleRename() {
			if (!tree->IsRenaming() || !scene)
				return;
			std::string said;
			if (!tree->CommitRename(*scene, said)) {
				tree->CancelRename();
				said = "Not renamed: " + said;
			}
			if (!said.empty())
				editor->SetStatus(said);
		}

		void KV6EditorView::SaveNodeAsModel(const NodePath& path) {
			const VoxelObject* node = scene->Node(path);
			if (!node && !path.empty())
				return;
			// Named after the node, or for the whole scene after its file.
			std::string stem = node ? node->name : LocalFileSystem::GetFileName(filePath);
			if (!node && stem.rfind('.') != std::string::npos)
				stem.erase(stem.rfind('.'));
			if (stem.empty())
				stem = node ? "Object" : "Scene";
			ShowModelFileDialog(
			  "Save \"" + stem + "\" as KV6", FileBrowserPurpose::Save,
			  stem + DefaultExtension(DocumentKind::Model),
			  [this, path](const std::string& file) {
				  // Made as the file is written, from the node as it is then.
				  std::string why;
				  Handle<VoxelModel> model = scene ? scene->ModelOf(path, why) : Handle<VoxelModel>();
				  if (!model) {
					  editor->SetStatus("Cannot save: " + why);
					  return;
				  }
				  if (KV6ModelIO::Save(*model, file))
					  editor->SetStatus("Saved " + LocalFileSystem::GetFileName(file));
				  else
					  editor->SetStatus("Could not write " + file);
			  },
			  DocumentKind::Model);
		}

		// --- Models at another resolution -----------------------------------------------------

		void KV6EditorView::OpenResample(const NodePath& path) {
			SettleTyping();
			ui->GetOptionBar()->EndEditing(true);
			editor->CancelToolInteraction();
			std::string why;
			if (!resample->Open(*scene, path, why))
				editor->SetStatus("Cannot generate a KV6: " + why);
		}

		void KV6EditorView::OpenResampleByName(const std::string& names,
		                                       const std::string& at) {
			// The node at the path of names, "/" (or nothing) the scene.
			NodePath path;
			std::size_t start = names == "/" ? names.size() : 0;
			while (start < names.size()) {
				const std::size_t slash = names.find('/', start);
				const std::string name = names.substr(start, slash - start);
				const std::vector<VoxelObject>& level =
				  path.empty() ? scene->Roots() : scene->Node(path)->children;
				int found = -1;
				for (std::size_t i = 0; i < level.size(); i++)
					if (level[i].name == name)
						found = int(i);
				if (found < 0) {
					SPLog("--editor-generate: no node '%s'", names.c_str());
					return;
				}
				path.push_back(found);
				start = slash == std::string::npos ? names.size() : slash + 1;
			}
			OpenResample(path);
			VoxelResolution resolution;
			if (!VoxelResolution::Parse(at, resolution) || !resample->Choose(*scene, resolution))
				SPLog("--editor-generate: resolution '%s' is not offered", at.c_str());
		}

		bool KV6EditorView::ResampleInput(const std::string& key, bool down) {
			if (!resample->IsOpen() || !scene)
				return false;
			std::string said;
			if (resample->Input(*scene, key, down, softwareCursor->GetPosition(), said)) {
				if (!said.empty())
					editor->SetStatus(said);
				if (!resample->IsOpen())
					tree->Reveal(*scene, scene->Active()); // what it added, if anything
				return true;
			}
			if (key == "MiddleMouseButton")
				return false; // the camera looks the preview over
			if (!down) {
				cam.MovementKey(key, down, globalTime); // let go of what was held
				return true;
			}
			// The camera's keys and the view's still work; nothing that would
			// change the scene under the preview does.
			if (EditorKeyMatches(cg_keyScreenshot, key))
				wantScreenShot = true;
			else if (!cam.MovementKey(key, down, globalTime) && !editor->IsCtrlHeld())
				ViewKey(key);
			return true;
		}

		void KV6EditorView::OpenAddChildFile(const NodePath& parent) {
			SettleTyping();
			FileBrowserOptions options = EditorBrowserOptions();
			options.purpose = FileBrowserPurpose::Open;
			options.filters.clear();
			options.filters.push_back(FileFilter{
			  "KV6 models and 2KV6 scenes",
			  {DefaultExtension(DocumentKind::Model), DefaultExtension(DocumentKind::Scene)}});
			options.homeDir = EditorHomeDir();
			options.initialDir = DialogFolder();
			editor->ReleaseHeldInput();
			// The tree cannot change while the browser is up, so `parent` still
			// names the node once a file is picked.
			ShowEditorFileDialog(*ui->GetOverlay(), "Add Child from File", std::move(options),
			                     [this, parent](const std::string& file) {
				                     if (scene)
					                     AddFileUnder(parent, file);
			                     });
		}

		void KV6EditorView::AddObjectUnder(const NodePath& parent) {
			std::string why;
			if (!scene->AddChild(parent, SeedModel(kNewModelSize), "Object", why)) {
				editor->SetStatus("Cannot add an object there: " + why);
				return;
			}
			ShowAdded();
		}

		void KV6EditorView::AddEmptyUnder(const NodePath& parent) {
			std::string why;
			if (!scene->AddChild(parent, Handle<VoxelModel>(), "Empty", why)) {
				editor->SetStatus("Cannot add an empty there: " + why);
				return;
			}
			ShowAdded();
		}

		void KV6EditorView::AddFileUnder(const NodePath& parent, const std::string& file) {
			// Named after the file, as a scene's names allow: no leading '#',
			// at most 255 bytes (a file name never holds a '/').
			std::string name = LocalFileSystem::GetFileName(file);
			if (name.rfind('.') != std::string::npos && name.rfind('.') > 0)
				name.erase(name.rfind('.'));
			name.erase(0, name.find_first_not_of('#'));
			if (name.size() > 255)
				name.resize(255);
			if (name.empty())
				name = "Object";
			std::string why;
			bool added;
			if (LocalFileSystem::HasExtension(file, DefaultExtension(DocumentKind::Scene))) {
				added = scene->AddScene(parent, file, name, why);
			} else {
				Handle<VoxelModel> model = KV6ModelIO::Load(file);
				if (!model)
					why = "it is not a model this editor can read";
				added = model && scene->AddChild(parent, model, name, why);
			}
			if (!added) {
				editor->SetStatus("Cannot add " + LocalFileSystem::GetFileName(file) + ": " + why);
				return;
			}
			ShowAdded();
		}

		void KV6EditorView::ShowAdded() {
			tree->Reveal(*scene, scene->Active());
			editor->SetStatus("Added " + scene->Node(scene->Active())->name);
			editor->ObjectSelectionChanged();
		}

		std::string KV6EditorView::DialogFolder() const {
			return filePath.empty() ? EditorRememberedFolder(EditorHomeDir())
			                        : LocalFileSystem::ParentDir(filePath);
		}

		ITransformTarget& KV6EditorView::ObjectTransform() {
			transformTarget.SetScene(scene.get());
			return transformTarget;
		}

		void KV6EditorView::SettleTyping() {
			SettleRename();
			editor->CancelToolInteraction(); // settles a number being typed in the panel
		}

		TextEntry::Modifiers KV6EditorView::HeldModifiers() const {
			TextEntry::Modifiers m;
			m.shift = editor->IsShiftHeld();
			m.ctrl = editor->IsCtrlHeld();
			m.alt = altHeld;
			m.meta = metaHeld;
			return m;
		}

		bool KV6EditorView::TreeTakesPress() const {
			return ShowingObjects() && !leftToEditor && !rightToEditor &&
			       !ui->GetColorPicker()->IsOverPicker(softwareCursor->GetPosition()) &&
			       tree->IsOver(*scene, softwareCursor->GetPosition());
		}

		bool KV6EditorView::HasUnsavedChanges() const { return IsDirty() || editor->HasPlacement(); }

		void KV6EditorView::FrameCamera() {
			Vector3 centre;
			float size;
			if (ShowingObjects()) {
				scene->Bounds(centre, size);
				cam.Frame(centre, size);
				return;
			}
			BoxFrame(MakeIntVector3(0, 0, 0), document.Size() - MakeIntVector3(1, 1, 1), centre,
			         size);
			cam.Frame(centre + DocumentOrigin(), size);
		}

		void KV6EditorView::ResetView() {
			Vector3 centre;
			float size;
			if (ShowingObjects()) {
				scene->Bounds(centre, size);
				cam.FlyHome(centre, size);
			} else {
				BoxFrame(MakeIntVector3(0, 0, 0), document.Size() - MakeIntVector3(1, 1, 1),
				         centre, size);
				cam.FlyHome(centre + DocumentOrigin(), size);
			}
			editor->SetStatus("View reset");
		}

		bool KV6EditorView::ViewKey(const std::string& key) {
			const IntVector3 last = document.Size() - MakeIntVector3(1, 1, 1);
			Vector3 centre;
			float size;
			if (EqualsIgnoringCase(key, kHomeViewKey)) {
				ResetView();
				return true;
			}
			if (EqualsIgnoringCase(key, kProjectionKey)) {
				ToggleProjection();
				return true;
			}
			if (EqualsIgnoringCase(key, kFrameSelectionKey) && ShowingObjects()) {
				GizmoPose pose;
				const bool holdsVoxels = scene->Picked() >= 0;
				const bool empty = !holdsVoxels && HasPickedObject() && scene->ActivePose(pose);
				if (holdsVoxels) {
					scene->Bounds(scene->Picked(), centre, size);
				} else if (empty) { // a point, shown with room round it
					centre = pose.position;
					size = kEmptyFrameSize;
				} else {
					scene->Bounds(centre, size);
				}
				cam.FlyToFrame(centre, size);
				editor->SetStatus(holdsVoxels || empty ? "Framed the object" : "Framed the scene");
				return true;
			}
			if (EqualsIgnoringCase(key, kFrameSelectionKey)) {
				IntVector3 lo, hi;
				const bool selected = editor->SelectionBounds(lo, hi);
				if (!selected) {
					lo = MakeIntVector3(0, 0, 0);
					hi = last;
				}
				BoxFrame(lo, hi, centre, size);
				cam.FlyToFrame(centre + DocumentOrigin(), size);
				editor->SetStatus(selected ? "Framed the selection" : "Framed the model");
				return true;
			}
			return false;
		}

		Handle<VoxelModel> KV6EditorView::SeedModel(int n) {
			Handle<VoxelModel> model = Handle<VoxelModel>::New(n, n, n);
			model->SetSolid(n / 2, n / 2, n / 2, editor->CurrentColor());
			// Anchor the pivot on the seed voxel so mirroring (whose planes start
			// there) is symmetric about it from the start.
			float c = float(n / 2);
			model->SetOrigin(MakeVector3(-c, -c, -c));
			return model;
		}

		void KV6EditorView::NewModel(int n, const std::string& path) {
			CloseScene();
			document.Assign(SeedModel(n));
			editor->DocumentReplaced();
			filePath = path;
			FrameCamera();
			savedDocumentId = -1; // a fresh, never-saved document starts dirty
		}

		bool KV6EditorView::LoadModel(const std::string& path) {
			if (LocalFileSystem::HasExtension(path, DefaultExtension(DocumentKind::Scene)))
				return LoadScene(path);
			Handle<VoxelModel> loaded = KV6ModelIO::Load(path);
			if (!loaded) {
				// The document that is open is not the one that failed to load, so it
				// stays exactly as it is; the caller says how to break the news.
				editor->SetStatus("Could not load " + path);
				return false;
			}
			CloseScene();
			document.Assign(loaded);
			editor->DocumentReplaced();
			filePath = path;
			FrameCamera();
			savedDocumentId = editor->DocumentStateId(); // a freshly loaded document is clean
			return true;
		}

		// --- Scene ------------------------------------------------------------

		void KV6EditorView::CloseScene() {
			if (!scene)
				return;
			// Edit mode first, while the scene is still there to take the voxels
			// back; then nothing is left that offers Object mode.
			editor->SetObjectMode(false);
			resample->Cancel(*scene);
			transformTarget.SetScene(nullptr);
			tree->Reset();
			scene.reset();
			document.SetKeepsOneVoxel(true);
		}

		bool KV6EditorView::LoadScene(const std::string& path) {
			std::unique_ptr<KV6Scene> loaded = std::make_unique<KV6Scene>();
			if (!loaded->Load(path)) {
				editor->SetStatus("Could not load " + path);
				return false;
			}
			CloseScene();
			scene = std::move(loaded);
			filePath = path;
			// The document is the picked object's voxels, handed over as Edit
			// mode is entered; it opens on the objects.
			document.Assign(scene->Picked() >= 0 ? scene->Object(scene->Picked()).model
			                                     : Handle<VoxelModel>::New(1, 1, 1));
			editor->DocumentReplaced();
			editedState = editor->DocumentStateId();
			editShift = MakeVector3(0.0F, 0.0F, 0.0F);
			editor->SetObjectMode(true);
			FrameCamera();
			return true;
		}

		void KV6EditorView::NewScene(int n, const std::string& path) {
			CloseScene();
			scene = std::make_unique<KV6Scene>();
			scene->StartWith(SeedModel(n), "Object");
			filePath = path;
			document.Assign(scene->Object(0).model);
			editor->DocumentReplaced();
			editedState = editor->DocumentStateId();
			editShift = MakeVector3(0.0F, 0.0F, 0.0F);
			editor->SetObjectMode(true);
			FrameCamera();
		}

		void KV6EditorView::ConvertToScene() {
			if (scene)
				return;
			// The model as it stands, pending voxels placed.
			VoxelEditor::DocumentCommand command(*editor);
			std::string name = LocalFileSystem::GetFileName(filePath);
			const std::size_t dot = name.rfind('.');
			if (dot != std::string::npos)
				name.resize(dot);
			scene = std::make_unique<KV6Scene>();
			scene->StartWith(Handle<VoxelModel>(document.Model()), name.empty() ? "Object" : name);
			editedState = editor->DocumentStateId();
			editShift = MakeVector3(0.0F, 0.0F, 0.0F);
			// A new document: the model's file stays as it was, and saving asks
			// where the scene goes.
			filePath.clear();
			editor->SetObjectMode(true);
			FrameCamera();
			editor->SetStatus("Converted to a scene of one object; save it as a .2kv6");
		}

		void KV6EditorView::TakeBackEditedObject() {
			const int picked = scene ? scene->Picked() : -1;
			if (picked < 0)
				return;
			// A volume that grew is another model, and its voxels were relabelled:
			// the object takes the model as it is now, and moves back by as much.
			VoxelObject& object = scene->Object(picked);
			object.model = Handle<VoxelModel>(document.Model());
			scene->CompensateShift(picked, editShift);
			editShift = MakeVector3(0.0F, 0.0F, 0.0F);
			scene->InvalidateModel(picked);
			if (editor->DocumentStateId() != editedState)
				scene->MarkChanged();
			editedState = editor->DocumentStateId();
		}

		bool KV6EditorView::EditedObjectIsBlank() const {
			return document.VoxelCount() == 0 ||
			       (editingEmpty && editor->DocumentStateId() == seededState);
		}

		void KV6EditorView::LeaveEmptiedObject() {
			if (!scene || editor->InObjectMode() || document.VoxelCount() > 0 ||
			    editor->IsWorkingVoxels())
				return;
			// The scene can bring back what a real object held, as one step,
			// since leaving ends the voxel history that could: what was there
			// is one edit back. An empty that was given a voxel loses nothing.
			if (!editingEmpty) {
				editor->Undo();
				if (document.VoxelCount() > 0)
					emptiedVoxels = KV6Scene::CopyOf(document.Model());
				editor->Redo();
			}
			editor->SetObjectMode(true);
		}

		void KV6EditorView::DocumentReframed(const Vector3& shift) {
			cam.Shift(shift);
			if (scene && !editor->InObjectMode())
				editShift = editShift + shift;
		}

		// --- IObjectModeHost ----------------------------------------------------

		bool KV6EditorView::PickObjectAtCursor() {
			const int index = scene->ObjectAt(cam.View(), softwareCursor->GetPosition());
			scene->Pick(index);
			if (index >= 0)
				tree->Reveal(*scene, scene->PathOf(index));
			if (index >= 0)
				editor->SetStatus("Picked " + PickedObjectName());
			return index >= 0;
		}

		bool KV6EditorView::SelectObjectAtCursor(SelectHow how) {
			const int index = scene->ObjectAt(cam.View(), softwareCursor->GetPosition());
			if (index < 0) {
				if (how == SelectHow::Only)
					scene->Select({}, {});
				return false;
			}
			const NodePath path = scene->PathOf(index);
			std::vector<NodePath> selection = scene->Selection();
			NodePath active = scene->Active();
			const auto at = std::find(selection.begin(), selection.end(), path);
			const bool in = at != selection.end();
			if (how == SelectHow::Toggle)
				how = in ? SelectHow::Remove : SelectHow::Add;
			switch (how) {
				case SelectHow::Only:
					selection.assign(1, path);
					active = path;
					break;
				case SelectHow::Add:
					if (!in)
						selection.push_back(path);
					active = path;
					break;
				case SelectHow::Remove:
					if (in)
						selection.erase(at);
					if (active == path)
						active = selection.empty() ? NodePath() : selection.front();
					break;
				case SelectHow::Toggle: break;
			}
			scene->Select(selection, active);
			if (scene->IsSelected(path))
				tree->Reveal(*scene, path);
			return true;
		}

		int KV6EditorView::SelectObjectsOnScreen(const std::function<bool(const Vector2&)>& inside,
		                                         bool through, bool removing) {
			const std::vector<int> found = scene->ObjectsOnScreen(cam.View(), inside, through);
			std::vector<NodePath> selection = scene->Selection();
			NodePath active = scene->Active();
			for (int index : found) {
				const NodePath& path = scene->PathOf(index);
				const auto at = std::find(selection.begin(), selection.end(), path);
				if (removing && at != selection.end()) {
					selection.erase(at);
				} else if (!removing && at == selection.end()) {
					selection.push_back(path);
					if (active.empty())
						active = path; // the gizmo goes on the first taken
				}
			}
			if (std::find(selection.begin(), selection.end(), active) == selection.end())
				active = selection.empty() ? NodePath() : selection.front();
			scene->Select(selection, active);
			return int(found.size());
		}

		bool KV6EditorView::HasSelectedObjects() const {
			return scene && (!scene->Selection().empty() || scene->SceneActive());
		}

		void KV6EditorView::SelectNoObject() {
			if (scene)
				scene->Select({}, {});
		}

		bool KV6EditorView::HasPickedObject() const { return scene && !scene->Active().empty(); }

		std::string KV6EditorView::PickedObjectName() const {
			if (!HasPickedObject())
				return std::string();
			const std::string& name = scene->Node(scene->Active())->name;
			if (!name.empty())
				return name;
			return scene->Picked() >= 0 ? "Object " + std::to_string(scene->Picked() + 1)
			                            : std::string("Empty");
		}

		bool KV6EditorView::PickedObjectPose(GizmoPose& pose) const {
			return scene && scene->ActivePose(pose);
		}

		IObjectModeHost::Handling KV6EditorView::PickedObjectHandling() const {
			if (scene && scene->SceneActive())
				return Handling::ScenePivot;
			const VoxelObject* o = scene ? scene->Node(scene->Active()) : nullptr;
			if (o && o->locked)
				return Handling::MoveOnly;
			return o && !o->model ? Handling::MoveAndTurn : Handling::Free;
		}

		void KV6EditorView::BeginObjectDrag() { scene->BeginDrag(); }

		void KV6EditorView::PreviewObjectDrag(const GizmoTransform& total) {
			scene->PreviewDrag(total);
		}

		void KV6EditorView::CommitObjectDrag() {
			if (scene->SceneActive()) {
				// The objects go the other way and the view with them, so nothing
				// moves on the screen but the origin, onto the new pivot.
				const Vector3 offset = scene->ScenePivotOffset();
				if (scene->CommitDrag()) {
					cam.Shift(offset * -1.0F);
					editor->SetStatus("Set the scene's pivot");
				}
				return;
			}
			if (scene->CommitDrag())
				editor->SetStatus("Placed " + PickedObjectName());
		}

		void KV6EditorView::CancelObjectDrag() { scene->CancelDrag(); }

		bool KV6EditorView::CanUndoObjects() const { return scene && scene->CanUndo(); }
		bool KV6EditorView::CanRedoObjects() const { return scene && scene->CanRedo(); }

		void KV6EditorView::UndoObjects() {
			const std::string what = scene->UndoLabel();
			scene->Undo();
			editor->SetStatus("Undid " + what);
		}

		void KV6EditorView::RedoObjects() {
			const std::string what = scene->RedoLabel();
			scene->Redo();
			editor->SetStatus("Redid " + what);
		}

		void KV6EditorView::EditModeChanged(bool entering) {
			if (!scene || scene->Active().empty())
				return;
			if (entering) {
				// An empty starts from one voxel on its origin, so its frame and
				// what hangs from it stay put.
				editingEmpty = !scene->Node(scene->Active())->model;
				if (editingEmpty) {
					Handle<VoxelModel> seed = Handle<VoxelModel>::New(1, 1, 1);
					seed->SetSolid(0, 0, 0, editor->CurrentColor());
					scene->GiveVoxels(scene->Active(), seed);
				}
				// The picked object's voxels become the document, in its own frame.
				document.SetKeepsOneVoxel(false);
				document.Assign(scene->Object(scene->Picked()).model);
				editor->DocumentReplaced();
				editedState = seededState = editor->DocumentStateId();
				editShift = MakeVector3(0.0F, 0.0F, 0.0F);
				FrameCamera();
				editor->SetStatus("Editing " + PickedObjectName());
				return;
			}
			if (EditedObjectIsBlank()) {
				const bool emptied = document.VoxelCount() == 0;
				const std::string name = PickedObjectName();
				// Nothing to undo to, yet the file may differ: what it was given
				// was saved, or it held voxels the history could not bring back.
				if (!emptiedVoxels && (!editingEmpty || editedState != seededState))
					scene->MarkChanged();
				scene->TakeVoxels(scene->Active(), std::move(emptiedVoxels));
				emptiedVoxels = Handle<VoxelModel>();
				editedState = editor->DocumentStateId();
				editShift = MakeVector3(0.0F, 0.0F, 0.0F);
				if (emptied)
					editor->SetStatus(name + " holds no voxels now: an empty");
				GizmoPose pose;
				if (scene->ActivePose(pose))
					cam.Frame(pose.position, kEmptyFrameSize);
				return;
			}
			TakeBackEditedObject();
			Vector3 centre;
			float size;
			scene->Bounds(scene->Picked(), centre, size);
			cam.Frame(centre, size);
		}

		bool KV6EditorView::Save() {
			// A document that has never been saved has nowhere to go yet, so "save"
			// means asking where. Asked before anything is committed, so cancelling
			// the dialog leaves the document exactly as it was.
			if (filePath.empty()) {
				OpenSaveAsDialog();
				return false;
			}

			// Saving writes the document, so anything still pending belongs in it.
			VoxelEditor::DocumentCommand command(*editor);
			if (scene) {
				// The voxels being edited are the picked object's, which the scene
				// holds once they are taken back.
				const bool blank = !editor->InObjectMode() && EditedObjectIsBlank();
				if (!editor->InObjectMode() && !blank)
					TakeBackEditedObject();
				// The voxel an empty was lent to edit from is not part of it.
				Handle<VoxelModel> lent;
				if (blank) {
					lent = scene->Object(scene->Picked()).model;
					scene->Object(scene->Picked()).model = Handle<VoxelModel>();
				}
				const bool saved = scene->Save(filePath);
				if (blank)
					scene->Object(scene->Picked()).model = lent;
				if (!saved) {
					editor->SetStatus("Save failed");
					return false;
				}
				scene->MarkSaved();
				editor->SetStatus("Saved " + filePath);
				return true;
			}
			if (!KV6ModelIO::Save(document.Model(), filePath)) {
				editor->SetStatus("Save failed");
				return false;
			}
			savedDocumentId = editor->DocumentStateId(); // this document state is now clean
			editor->SetStatus("Saved " + filePath);
			return true;
		}

		void KV6EditorView::NewDocument() {
			ConfirmDiscardChanges([this] {
				NewModel(kNewModelSize, std::string());
				editor->SetStatus("New model");
			});
		}

		std::vector<EditorMenuGroup> KV6EditorView::GetMenus() {
			// A command edits behind whatever the active tool had in progress,
			// as a shortcut does, so that is abandoned before it runs.
			auto command = [this](std::function<void()> run) {
				return [this, run] {
					editor->CancelToolInteraction();
					run();
				};
			};
			const bool selected = editor->SelectionCount() > 0;

			EditorMenuGroup file;
			file.title = "File";
			file.items.push_back(MenuCommand("New Model", [this] { NewDocument(); }, true, "Ctrl+N"));
			file.items.push_back(
			  MenuCommand("Open...", [this] { OpenDocument(); }, true, "Ctrl+O"));
			// Always available: a document with nowhere to go yet is asked where.
			file.items.push_back(MenuCommand("Save", [this] { Save(); }, true, "Ctrl+S"));
			file.items.push_back(MenuCommand("Save As...", [this] { OpenSaveAsDialog(); }));
			// A lone model can become the first object of a scene.
			if (!scene)
				file.items.push_back(
				  MenuCommand("Convert to Scene", command([this] { ConvertToScene(); })));
			// The one command that edits the open document with a file:
			// inserting a model is a paste that happens to come from disk.
			// Inserting places voxels, which only Edit mode edits.
			if (!ShowingObjects())
				file.items.push_back(
				  MenuCommand("Insert Model...", command([this] { OpenInsertDialog(); })));
			file.items.push_back(MenuRule());
			file.items.push_back(MenuCommand(
			  "Exit to Menu", [this] { ConfirmDiscardChanges([this] { wantsClose = true; }); }));

			// Undo and Redo step through the scene's history in Object mode and
			// the voxels' in Edit mode; the rest of what is edited and selected
			// here is voxels, which only Edit mode has. Objects are selected,
			// copied and removed from the scene tree and the object's own menu.
			const bool voxels = !ShowingObjects();
			EditorMenuGroup edit;
			edit.title = "Edit";
			edit.items.push_back(
			  MenuCommand("Undo", command([this] { editor->Undo(); }), editor->CanUndo(), "Ctrl+Z"));
			edit.items.push_back(
			  MenuCommand("Redo", command([this] { editor->Redo(); }), editor->CanRedo(), "Ctrl+Y"));
			if (voxels) {
				edit.items.push_back(MenuRule());
				edit.items.push_back(MenuCommand(
				  "Cut", command([this] { editor->CutSelection(); }), selected, "Ctrl+X"));
				edit.items.push_back(MenuCommand(
				  "Copy", command([this] { editor->CopySelection(); }), selected, "Ctrl+C"));
				edit.items.push_back(MenuCommand("Paste", command([this] { editor->Paste(); }),
				                                 editor->CanPaste(), "Ctrl+V"));
				edit.items.push_back(MenuCommand("Delete",
				                                 command([this] { editor->DeleteSelection(); }),
				                                 selected, "Del"));
			}

			// Which voxels are selected, apart from what is done with them: the
			// Select tool's bar has the same three.
			EditorMenuGroup select;
			select.title = "Select";
			select.items.push_back(
			  MenuCommand("All", command([this] { editor->SelectAll(); }), true, "Ctrl+A"));
			select.items.push_back(MenuCommand("None",
			                                   command([this] { editor->ClearSelection(); }),
			                                   selected, "Ctrl+Shift+A"));
			select.items.push_back(MenuCommand(
			  "Invert", command([this] { editor->InvertSelection(); }), true, "Ctrl+I"));

			// What the view does, which until now only its keys could ask for.
			EditorMenuGroup view;
			view.title = "View";
			view.items.push_back(
			  MenuCommand("Reset View", [this] { ResetView(); }, true, kHomeViewKey));
			view.items.push_back(MenuCommand("Frame Selection",
			                                 [this] { ViewKey(kFrameSelectionKey); }, true,
			                                 kFrameSelectionKey));
			EditorMenuItem parallel = MenuCommand("Parallel Projection",
			                                      [this] { ToggleProjection(); }, true,
			                                      kProjectionKey);
			parallel.checked = cam.IsOrthogonal();
			view.items.push_back(parallel);
			view.items.push_back(MenuRule());
			view.items.push_back(MenuCommand("Save Screenshot", [this] { wantScreenShot = true; },
			                                 true, cg_keyScreenshot));

			if (!voxels)
				return {file, edit, view};
			return {file, edit, select, view};
		}

		void KV6EditorView::ShowMessage(const std::string& text) {
			editor->ReleaseHeldInput();
			ShowEditorMessage(*ui->GetOverlay(), text);
		}

		void KV6EditorView::ConfirmDiscardChanges(std::function<void()> proceed) {
			if (!proceed)
				return;
			if (!HasUnsavedChanges()) {
				proceed();
				return;
			}
			editor->ReleaseHeldInput();
			gui::ConfirmDiscardChanges(*ui->GetOverlay(),
			                           scene ? "This scene has unsaved changes."
			                                 : "This model has unsaved changes.",
			                           [this](std::function<void()> after) {
				                           // A document that was never saved needs
				                           // somewhere to go first; `after` then runs
				                           // only if that save succeeds.
				                           if (filePath.empty())
					                           OpenSaveAsDialog(after);
				                           else if (Save())
					                           after();
			                           },
			                           proceed);
		}

		void KV6EditorView::ShowModelFileDialog(const std::string& title, FileBrowserPurpose purpose,
		                                        const std::string& initialName,
		                                        std::function<void(const std::string&)> picked,
		                                        DocumentKind writes) {
			// The same options the model tab on the main screen is built from, so a
			// model file behaves the same way wherever it is picked.
			FileBrowserOptions options = EditorBrowserOptions();
			options.purpose = purpose;

			// Reading lists every type; writing offers only the one this editor
			// produces. The browser takes a name that already ends in a listed type
			// at its word, so a document named `model.vxl` would be written as a KV6
			// under a name promising something else.
			if (purpose != FileBrowserPurpose::Open) {
				options.filters.clear();
				options.filters.push_back(FileFilter{DocumentFilterLabel(), {DefaultExtension(writes)}});
			}
			options.homeDir = EditorHomeDir();
			// The document's own folder is where this document's dialogs belong;
			// anything else opens where the player last was, here or on the main
			// screen.
			options.initialDir = DialogFolder();
			options.initialName = initialName;

			// A dialog takes over input: nothing should stay held while it is up.
			editor->ReleaseHeldInput();
			ShowEditorFileDialog(*ui->GetOverlay(), title, std::move(options), std::move(picked));
		}

		void KV6EditorView::OpenDocument() {
			ConfirmDiscardChanges([this] {
				ShowModelFileDialog("Open", FileBrowserPurpose::Open, std::string(),
				                    [this](const std::string& path) {
					                    if (!LoadModel(path))
						                    ShowMessage("Could not open " +
						                                LocalFileSystem::GetFileName(path) + ".");
				                    });
			});
		}

		void KV6EditorView::OpenSaveAsDialog(std::function<void()> after) {
			const std::string name = filePath.empty() ? UntitledFileName(GetDocumentKind())
			                                          : LocalFileSystem::GetFileName(filePath);
			ShowModelFileDialog("Save As", FileBrowserPurpose::Save, name,
			                    [this, after](const std::string& path) {
				                    if (SaveDocument(path) && after)
					                    after();
			                    });
		}

		void KV6EditorView::OpenInsertDialog() {
			ShowModelFileDialog("Insert Model", FileBrowserPurpose::Open, std::string(),
			                    [this](const std::string& path) { InsertModel(path); });
		}

		void KV6EditorView::InsertModel(const std::string& path) {
			Handle<VoxelModel> imported = KV6ModelIO::Load(path);
			if (!imported) {
				editor->SetStatus("Could not load " + path);
				return;
			}
			editor->InsertModel(*imported);
		}

		bool KV6EditorView::SaveDocument(const std::string& path) {
			// The document only moves to the new path once the write has succeeded:
			// a failed Save As leaves it where it was, rather than pointing it at a
			// file it could not produce and offering to "save" there again.
			const std::string previousPath = filePath;
			filePath = path;
			if (Save())
				return true;

			filePath = previousPath;
			return false;
		}

		// --- Host -------------------------------------------------------------

		void KV6EditorView::PickImageFile(const std::string& title,
		                                  std::function<void(const std::string&)> picked) {
			// The editor's browser, listing the images the game can read rather
			// than documents: a reference image is no document, and nothing is
			// written to it.
			FileBrowserOptions options = EditorBrowserOptions();
			options.purpose = FileBrowserPurpose::Open;
			options.filters.clear();
			options.filters.push_back(
			  FileFilter{"Images", {".png", ".jpg", ".jpeg", ".bmp", ".tga", ".tif"}});
			options.describeEntry = nullptr;
			options.appendFilterExtension = false;
			options.homeDir = EditorHomeDir();
			options.initialDir = DialogFolder();
			editor->ReleaseHeldInput();
			ShowEditorFileDialog(*ui->GetOverlay(), title, std::move(options), std::move(picked));
		}

		void KV6EditorView::ToggleProjection() {
			cam.SetOrthogonal(!cam.IsOrthogonal());
			cg_editorOrthogonal = cam.IsOrthogonal() ? 1 : 0;
			editor->SetStatus(cam.IsOrthogonal() ? "Parallel projection"
			                                     : "Perspective projection");
		}

		void KV6EditorView::LookIsometric(float elevation) {
			// Isometric is a parallel projection by definition: in perspective
			// the three axes would not keep their one scale.
			cam.SetOrthogonal(true);
			cg_editorOrthogonal = 1;
			cam.TurnToCorner(elevation);
		}

		bool KV6EditorView::KeyIsReserved(const std::string& key) const {
			return BoundToKeySetting(key) || EqualsIgnoringCase(key, kHomeViewKey) ||
			       EqualsIgnoringCase(key, kFrameSelectionKey) ||
			       EqualsIgnoringCase(key, kProjectionKey) || EqualsIgnoringCase(key, kRenameKey);
		}

		bool KV6EditorView::ClickViewportWidget(const Vector2& cursor) {
			if (naviCube.ButtonAt(cursor, NavigationCube::kHomeButton)) {
				ResetView();
				return true;
			}
			if (naviCube.ButtonAt(cursor, NavigationCube::kProjectionButton)) {
				ToggleProjection();
				return true;
			}
			Vector3 navDir;
			if (!naviCube.DirectionAt(cam.View(), cursor, navDir))
				return false;
			cam.SnapToward(navDir);
			return true;
		}

		client::SceneDefinition KV6EditorView::SetupScene(float width, float height) {
			client::SceneDefinition sceneDef = cam.Scene(width, height);
			// The editor draws its own ground: the game world brings chunk culling
			// and terrain that crowds the model, and water is a plane following the
			// camera at sea level with a mirrored scene rendered into it.
			sceneDef.skipWorld = true;
			sceneDef.skipWater = true;
			// A voxel has six face normals, so sunlight splits one palette colour
			// into several on-screen shades and the model stops reading as the
			// colour it was painted. Light it evenly instead, and outline it so the
			// shape survives the loss of shading.
			sceneDef.flatModelLighting = true;
			sceneDef.forceOutlines = true;
			sceneDef.denyCameraBlur = true;
			sceneDef.time = (unsigned int)(globalTime * 1000.0F);
			return sceneDef;
		}

		// Long world axes through the model's origin (pivot), which renders at
		// world coordinate -origin in the editor's grid space.
		void KV6EditorView::DrawOriginAxes() {
			// The scene's origin with its objects shown, else the model's pivot.
			Vector3 org = ShowingObjects() ? MakeVector3(0.0F, 0.0F, 0.0F) : document.Origin();
			Vector3 p = MakeVector3(-org.x, -org.y, -org.z);
			float L = 1000.0F;
			renderer->AddDebugLine(p - MakeVector3(L, 0, 0), p + MakeVector3(L, 0, 0),
			                       MakeVector4(1.0F, 0.3F, 0.3F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, L, 0), p + MakeVector3(0, L, 0),
			                       MakeVector4(0.4F, 1.0F, 0.4F, 0.5F));
			renderer->AddDebugLine(p - MakeVector3(0, 0, L), p + MakeVector3(0, 0, L),
			                       MakeVector4(0.45F, 0.6F, 1.0F, 0.5F));
		}

		void KV6EditorView::DrawHelpers() {
			// Opaque (alpha 1.0, so the lines cover what is behind them) but kept
			// close to the background tone: the floor should be legible without
			// competing with the model. Contrast comes from the minor/major
			// difference rather than from brightness.
			Vector4 grid = MakeVector4(0.19F, 0.20F, 0.23F, 1.0F);
			Vector4 gridMajor = MakeVector4(0.30F, 0.32F, 0.37F, 1.0F);
			Vector4 box = MakeVector4(0.4F, 0.7F, 1.0F, 0.5F);

			// A fixed grid in world space: it does not follow the model, the volume
			// or the camera, so it reads as the ground rather than something moving
			// with the subject.
			float z = kGroundPlaneZ;
			float from = float(-kGroundReach) - 0.5F; // voxel edges, not centres
			float to = float(kGroundReach) - 0.5F;
			for (int i = -kGroundReach; i <= kGroundReach; i += kGroundStep) {
				float at = float(i) - 0.5F;
				bool major = (i % (kGroundStep * kGroundMajorEvery)) == 0;
				const Vector4& color = major ? gridMajor : grid;
				renderer->AddDebugLine(MakeVector3(at, from, z), MakeVector3(at, to, z), color);
				renderer->AddDebugLine(MakeVector3(from, at, z), MakeVector3(to, at, z), color);
			}

			// The volume being edited; the scene outlines its picked object itself.
			if (ShowingObjects())
				return;
			const IntVector3 size = document.Size();
			Vector3 a = MakeVector3(-0.5F, -0.5F, -0.5F);
			Vector3 b = MakeVector3(float(size.x) - 0.5F, float(size.y) - 0.5F, float(size.z) - 0.5F);
			ForEachBoxEdge(a, b,
			               [&](const Vector3& p, const Vector3& q) { renderer->AddDebugLine(p, q, box); });
		}

		void KV6EditorView::DrawRibbon(float sw) {
			client::IFont& font = fontManager->GetSmallGuiFont();
			OverlayColorNP(*renderer, MakeVector4(0.06F, 0.06F, 0.08F, 1.0F));
			OverlayFillRect(*renderer, 0.0F, 0.0F, sw, VoxelEditor::RibbonHeight());

			// The menus stand where the editor's name used to: the file open is
			// named right of them, which says more than the application does.
			MenuBar& menus = *ui->GetMenuBar();
			const float namesAt =
			  12.0F + menus.Layout(font, 12.0F, VoxelEditor::RibbonHeight()) + 16.0F;
			menus.DrawTitles(*renderer, *fontManager, softwareCursor->GetPosition());

			std::string name =
			  (!filePath.empty()) ? filePath : (scene ? "(unsaved scene)" : "(unsaved model)");
			if (HasUnsavedChanges())
				name += " *";
			const std::string count =
			  ShowingObjects() ? std::to_string(scene->Count()) + " objects"
			                   : std::to_string(document.VoxelCount()) + " voxels";
			font.Draw(name + "   (" + count + ")", MakeVector2(namesAt, 5.0F), 0.85F,
			          MakeVector4(0.75F, 0.75F, 0.78F, 1.0F));

			std::string save = "[Ctrl+S] save";
			Vector2 cs = font.Measure(save);
			font.Draw(save, MakeVector2(sw - 12.0F - cs.x * 0.8F, 5.0F), 0.8F,
			          MakeVector4(0.6F, 0.6F, 0.63F, 1.0F));
		}

		std::string KV6EditorView::HelpLine() const {
			// The camera's keys, named as they are bound: the help never names a
			// key that does something else.
			std::string help = "[MMB] look  |  [Shift+MMB] pan";
			auto keys = [&](std::initializer_list<std::string> bound, const char* what) {
				std::string names;
				for (const std::string& key : bound) {
					if (!key.empty())
						names += (names.empty() ? "" : "/") + key;
				}
				if (!names.empty())
					help += "  |  [" + names + "] " + what;
			};
			keys({cg_keyMoveForward, cg_keyMoveLeft, cg_keyMoveBackward, cg_keyMoveRight}, "move");
			keys({cg_keyJump, cg_keyCrouch}, "up/down");
			keys({cg_keySprint}, "faster");
			help += "  |  [Wheel] zoom";
			// A view key that a key setting is bound to does that instead, so it
			// is not named for what it no longer does.
			auto viewKey = [&](const char* key, const char* what) {
				if (!BoundToKeySetting(key))
					help += std::string("  |  [") + key + "] " + what;
			};
			viewKey(kHomeViewKey, "reset view");
			viewKey(kFrameSelectionKey, "frame selection");
			viewKey(kProjectionKey, "perspective/parallel");
			keys({cg_keyScreenshot}, "screenshot");
			return help;
		}

		// --- View interface ---------------------------------------------------

		void KV6EditorView::MouseEvent(float dx, float dy) {
			if (cam.IsLooking() && editor->IsShiftHeld()) { // Shift + wheel-button drag pans
				// The cursor travels with the drag, staying on the grabbed spot
				// (it stops at the screen edge while the pan carries on).
				softwareCursor->Accumulate(dx, dy);
				cam.Pan(dx, dy);
				return;
			}
			if (cam.IsLooking()) {
				cam.Look(dx, dy);
				return;
			}
			// A number being scrubbed takes the motion, the pointer held still,
			// so the drag never runs into the screen's edge.
			if (!editor->HoldsPointer())
				softwareCursor->Accumulate(dx, dy);
			if (resample->IsOpen() && scene) {
				resample->Move(*scene, softwareCursor->GetPosition()); // its slider, while held
				return;
			}
			if (ui->GetContextMenu()->IsOpen())
				return; // it follows the cursor as it is drawn
			// The tree's press follows the cursor; over its rows, nothing under
			// them answers to it.
			if (tree->IsHolding() && scene) {
				tree->Move(*scene, softwareCursor->GetPosition());
				return;
			}
			if (TreeTakesPress())
				return;
			editor->PointerMoved(MakeVector2(dx, dy));
		}

		void KV6EditorView::WheelEvent(float x, float y) {
			if (ui->GetOverlay()->WheelEvent(x, y))
				return;
			if (ui->GetMenuBar()->IsOpen())
				return;
			if (ui->GetContextMenu()->IsOpen()) {
				ui->GetContextMenu()->Close();
				return;
			}
			if (ShowingObjects() && tree->Wheel(*scene, softwareCursor->GetPosition(), y))
				return;
			cam.Zoom(y);
		}

		void KV6EditorView::KeyEvent(const std::string& key, bool down) {
			// Modifier state must track the keyboard even while a modal swallows
			// keys, or a Ctrl released during the menu stays "held" afterwards.
			editor->TrackModifiers(key, down);
			cam.TrackModifiers(key, down);
			if (key == "Meta")
				metaHeld = down;
			if (key == "Alt")
				altHeld = down;

			// A modal dialog (file browser) owns every key while it is up.
			if (ui->GetOverlay()->KeyEvent(key, down))
				return;

			// The Generate KV6 panel, while it is up: its keys and clicks, and the
			// camera's, nothing else.
			if (ResampleInput(key, down))
				return;

			// So does the tree's menu, but for releases of keys held from before
			// it opened, which have to be let go.
			if (ui->GetContextMenu()->IsOpen()) {
				const Vector2& at = softwareCursor->GetPosition();
				if (key == "LeftMouseButton" || key == "RightMouseButton") {
					if (down) {
						ui->GetContextMenu()->Press(at);
						// A right press on another row, or object, opens its
						// menu there.
						if (!ui->GetContextMenu()->IsOpen() && key == "RightMouseButton") {
							if (TreeTakesPress())
								OpenTreeMenu();
							else if (ShowingObjects())
								OpenObjectMenu();
						}
					} else if (key == "LeftMouseButton") {
						ui->GetContextMenu()->Release(at);
					}
					return;
				}
				if (down) {
					ui->GetContextMenu()->Key(key);
					return;
				}
			}

			// A name being typed in the tree owns the keyboard as a dialog would:
			// letters, Escape and Enter are the name's. A press anywhere but on
			// it settles it first, then goes on to do what it does.
			if (tree->IsRenaming() && scene) {
				const Vector2& at = softwareCursor->GetPosition();
				if (key == "LeftMouseButton" || key == "RightMouseButton") {
					if (down && key == "LeftMouseButton" && tree->IsOverRename(*scene, at)) {
						tree->Press(*scene, at, false, editor->IsShiftHeld(), globalTime);
						return;
					}
					if (down)
						SettleRename();
				} else if (down) {
					TextEntry::Modifiers m;
					m.shift = editor->IsShiftHeld();
					m.ctrl = editor->IsCtrlHeld();
					m.alt = altHeld;
					m.meta = metaHeld;
					const std::string said = tree->RenameKey(*scene, key, m);
					if (!said.empty())
						editor->SetStatus(said);
					return;
				}
			}


			// A menu of the bar owns the keyboard while it is down, Escape
			// included: it is what Escape puts away first.
			if (ui->GetMenuBar()->KeyEvent(key, down)) {
				editor->ReleaseHeldInput();
				return;
			}
			// A press of any button but the left one, while a menu of the bar is
			// down, only puts the menu away: the middle button is handled here,
			// before the editor sees it, and would otherwise grab the view from
			// under a menu that was merely being read. The left button has
			// somewhere to land and is answered where the pointer is.
			if (down && key != "LeftMouseButton" &&
			    key.find("MouseButton") != std::string::npos && ui->GetMenuBar()->IsOpen()) {
				ui->GetMenuBar()->Close();
				return;
			}
			// Escape backs out of whatever the editor has in hand -- a number
			// being typed, a box half drawn, the selection -- one layer at a
			// time. With nothing left to back out of it leaves the editor,
			// asking about unsaved work first; everything else the editor can
			// be told to do is on the menu bar, in sight.
			if (down && key == "Escape") {
				// A drag in the tree first, then what the editor has in hand.
				if (!tree->CancelDrag() && !editor->EscapeOnce()) {
					editor->ReleaseHeldInput();
					ConfirmDiscardChanges([this] { wantsClose = true; });
				}
				return;
			}

			// A number being typed has the keyboard, its Ctrl shortcuts too:
			// Ctrl+Z undoes what was typed, not the document.
			if (down && editor->IsTyping() && key.find("MouseButton") == std::string::npos &&
			    editor->OptionBarKey(key))
				return;

			if (down && editor->IsCtrlHeld() &&
			    (IsFileShortcut(key) || VoxelEditor::IsEditShortcut(key))) {
				// Ctrl is also the default descend key (cg_keyCrouch), so the view
				// has been sinking since Ctrl went down: undo that drift and stop
				// descending for the rest of this press.
				cam.CancelCtrlDescent();
				if (VoxelEditor::IsEditShortcut(key)) {
					editor->RunEditShortcut(key);
					return;
				}
				// The document commands the menu offers, reachable without it: the
				// menu is where they are found, not where they have to be used.
				editor->CancelToolInteraction();
				if (EqualsIgnoringCase(key, "s")) {
					if (editor->IsShiftHeld())
						OpenSaveAsDialog();
					else
						Save();
				} else if (EqualsIgnoringCase(key, "n"))
					NewDocument();
				else if (EqualsIgnoringCase(key, "o"))
					OpenDocument();
				return;
			}

			if (key == "MiddleMouseButton") {
				// Turning and panning go round the voxel under the cursor, wherever
				// flying has left the orbited point.
				if (down && !ShowingObjects()) {
					editor->DoPick();
					if (editor->HasPick()) {
						const IntVector3 v = editor->PickSolid();
						cam.OrbitAtDepthOf(MakeVector3(float(v.x), float(v.y), float(v.z)) +
						                   DocumentOrigin());
					}
				}
				cam.SetLooking(down);
				return;
			}
			if (key == "LeftMouseButton") {
				if (down && TreeTakesPress()) {
					// As a press anywhere off the option bar does, it settles a
					// number being typed there.
					ui->GetOptionBar()->EndEditing(true);
					editor->CancelToolInteraction();
					if (editor->IsCtrlHeld())
						cam.CancelCtrlDescent(); // Ctrl is the default descend key
					const std::string said =
					  tree->Press(*scene, softwareCursor->GetPosition(),
					              editor->IsCtrlHeld() || metaHeld, editor->IsShiftHeld(), globalTime);
					if (!said.empty())
						editor->SetStatus(said);
					editor->ObjectSelectionChanged(); // a row picked shows its gizmo
					return;
				}
				if (!down && tree->IsHolding()) {
					const std::string said =
					  scene ? tree->Release(*scene, softwareCursor->GetPosition()) : std::string();
					if (!said.empty())
						editor->SetStatus(said);
					editor->ObjectSelectionChanged();
					return;
				}
				leftToEditor = down;
				editor->ButtonEvent(PointerButton::Left, down);
				return;
			}
			if (key == "RightMouseButton") {
				// A right press on a row opens its menu; its release, and any
				// release the tools never had the press of, are nobody's.
				if (down && TreeTakesPress()) {
					OpenTreeMenu();
					return;
				}
				// On an object in the view, its menu; with no button held for
				// a tool, so a drag is still cancelled by it.
				if (down && ShowingObjects() && !leftToEditor && OpenObjectMenu())
					return;
				if (!down && !rightToEditor)
					return;
				rightToEditor = down;
				editor->ButtonEvent(PointerButton::Right, down);
				return;
			}

			// A box being typed into owns the keyboard, so none of what follows
			// runs: a letter would otherwise fly the camera or switch tools.
			// Releases still fall through, or a key held from before the box was
			// clicked would never be let go.
			if (down && editor->OptionBarKey(key))
				return;

			if (down && EditorKeyMatches(cg_keyScreenshot, key)) {
				wantScreenShot = true;
				return;
			}
			if (editor->CommandKey(key, down))
				return;
			if (cam.MovementKey(key, down, globalTime))
				return;
			if (down && !editor->IsCtrlHeld() && ViewKey(key))
				return;
			if (down && ShowingObjects() && EqualsIgnoringCase(key, kRenameKey)) {
				if (!scene->Active().empty())
					RenameInTree(scene->Active());
				return;
			}
			editor->ToolKey(key, down);
		}

		void KV6EditorView::TextInputEvent(const std::string& text) {
			// A dialog takes typing over everything; otherwise it belongs to a
			// name being typed in the tree, or to the number being typed into,
			// in the option bar or the transform panel.
			if (ui->GetOverlay()->IsActive()) {
				ui->GetOverlay()->TextInputEvent(text);
			} else if (tree->IsRenaming()) {
				if (TextEntry::Types(HeldModifiers())) // not the letter of a shortcut
					tree->RenameText(text);
			} else {
				editor->TextInput(text);
			}
		}

		void KV6EditorView::TextEditingEvent(const std::string& text, int start, int len) {
			if (ui->GetOverlay()->IsActive())
				ui->GetOverlay()->TextEditingEvent(text, start, len);
			else if (tree->IsRenaming())
				tree->RenameComposition(text);
			else
				editor->TextComposition(text);
		}

		bool KV6EditorView::AcceptsTextInput() {
			return ui->GetOverlay()->AcceptsTextInput() || tree->IsRenaming() ||
			       editor->AcceptsTextInput();
		}

		AABB2 KV6EditorView::GetTextInputRect() {
			if (ui->GetOverlay()->IsActive())
				return ui->GetOverlay()->GetTextInputRect();
			if (tree->IsRenaming() && scene)
				return tree->RenameRect(*scene);
			return editor->TextInputRect();
		}

		void KV6EditorView::DrawSceneContents(bool modelOnly) {
			if (ShowingObjects()) {
				scene->Render(*renderer, modelOnly);
			} else if (renderModel) {
				client::ModelRenderParam param;
				// Cancel the KV6 pivot so the model sits in the editor's grid space.
				param.matrix = Matrix4::Translate(document.Origin() * -1.0F);
				renderer->RenderModel(*renderModel, param);
			}
			if (modelOnly)
				return;
			DrawHelpers();
			DrawOriginAxes();
			editor->DrawScene();
		}

		void KV6EditorView::ServeRender(const client::SceneDefinition& sceneDef) {
			// The model alone, twice, on two backgrounds: what differs between
			// the two pictures is background and what does not is the model,
			// whatever the renderer's filters did to either (see LiftSubject).
			// Each is a whole frame, finished and read back before the frame
			// the user sees is begun: `FrameDone` is what makes the read-back
			// a complete image, as for a screenshot.
			auto picture = [&](const Vector3& fog) {
				renderer->SetFogColor(fog);
				renderer->StartScene(sceneDef);
				DrawSceneContents(true);
				renderer->EndScene();
				renderer->FrameDone();
				return renderer->ReadBitmap();
			};
			// No fog near enough to reach the model, or it would take on each
			// background's colour with distance and read as part of it.
			renderer->SetFogDistance(cam.ViewDistance() * kRenderFogReach);
			try {
				Handle<Bitmap> first = picture(kRenderKeyFirst);
				Handle<Bitmap> second = picture(kRenderKeySecond);
				Handle<Bitmap> image =
				  LiftSubject(*first, *second, renderBackground, renderTransparent);
				editor->SetStatus("Render saved: " + SaveRenderImage(*image));
			} catch (const std::exception& ex) {
				editor->SetStatus("Render failed");
				SPLog("Editor render failed: %s", ex.what());
			}
			renderer->SetFogColor(kBackground);
			renderer->SetFogDistance(cam.ViewDistance());
		}

		void KV6EditorView::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			globalTime += dt;

			// The movement keys fly the camera unless a modal has the keyboard;
			// its speed follows the model's size, so any model is crossed as fast.
			const IntVector3 size = document.Size();
			int span = std::max(size.x, std::max(size.y, size.z));
			if (ShowingObjects()) {
				Vector3 centre;
				float extent;
				scene->Bounds(centre, extent);
				span = std::max(1, int(extent));
			}
			cam.Update(dt, globalTime, float(span) * kCameraSpeedPerSize,
			           !ui->GetMenuBar()->IsOpen() && !ui->GetOverlay()->IsActive());
			editor->Update(dt);
			LeaveEmptiedObject();

			// While an image of the model is being made the view shows the model
			// alone, as the image will.
			const bool modelOnly = editor->PresentsRender();
			renderer->SetFogColor(kBackground);
			// Matches the far plane: fog that ended closer would swallow the model
			// at the far end of the zoom range.
			renderer->SetFogDistance(cam.ViewDistance());

			// The scene is rendered full-screen (sub-viewport rendering isn't
			// guaranteed across renderers — keep this renderer-agnostic), and the
			// ribbon + toolbar bars are drawn opaque over the top. So the camera
			// projection and the cursor->ray pick both use the full screen.
			client::SceneDefinition sceneDef = SetupScene(sw, sh);

			// The render model is derived from the document, rebuilt once before
			// the frame whatever edits since the last one changed.
			if (!renderModel || renderModelVersion != document.Version()) {
				renderModel = renderer->CreateModel(document.Model());
				renderModelVersion = document.Version();
			}
			if (wantRender) {
				wantRender = false;
				ServeRender(sceneDef);
			}
			renderer->StartScene(sceneDef);
			DrawSceneContents(modelOnly);
			renderer->EndScene();

			editor->DrawViewportOverlays(sw, sh, HelpLine());
			DrawRibbon(sw);
			editor->DrawBars(sw);

			// The navigation cube, top right under the bars.
			naviCube.Layout(sw - kNaviCubeMargin - kNaviCubeBox * 0.5F,
			                VoxelEditor::BarsHeight() + kNaviCubeTopGap + kNaviCubeBox * 0.5F,
			                kNaviCubeBox * 0.5F - kNaviCubeInset);
			naviCube.Draw(*renderer, fontManager->GetSmallGuiFont(), cam.View(),
			              softwareCursor->GetPosition(), cam.IsOrthogonal());

			// The transform panel, in the view's bottom right corner, its foot
			// level with the colour picker's.
			editor->SetPanelCorner(sw - kNaviCubeMargin, sh - kPanelBottomGap);

			// The scene's tree, down the left between the bars and the hint
			// lines, in Object mode only.
			if (ShowingObjects()) {
				tree->SetSceneName(filePath.empty() ? std::string("Scene")
				                                    : LocalFileSystem::GetFileName(filePath));
				tree->Layout(kTreeLeft, VoxelEditor::BarsHeight() + kTreeTopGap, sh - kTreeBottomGap);
				tree->Draw(*scene, softwareCursor->GetPosition(), globalTime);
				resample->Draw(kTreeLeft + kTreeWidth + 16.0F, VoxelEditor::BarsHeight() + kTreeTopGap,
				              softwareCursor->GetPosition());
			} else {
				if (tree->IsHolding())
					tree->CancelDrag(); // left for Edit mode mid-drag
				tree->CancelRename();
				ui->GetContextMenu()->Close();
				if (scene)
					resample->Cancel(*scene);
			}

			editor->DrawPanels();
			ui->GetContextMenu()->Draw(softwareCursor->GetPosition());
			// Over everything, the bars included: a menu hangs from the ribbon
			// across them, though it answers for its presses before they do.
			ui->GetMenuBar()->DrawOpenMenu(*renderer, *fontManager,
			                               softwareCursor->GetPosition());

			// Dialogs draw their own pointer (with the text-field I-beam), so the
			// editor's cursor steps aside while one is open.
			ui->GetOverlay()->RunFrame(dt);
			ui->GetOverlay()->Draw();
			if (!ui->GetOverlay()->IsActive())
				softwareCursor->Draw();

			// The runner is the only thing that presents, so the capture happens here
			// rather than after the frame: `FrameDone` flushes everything drawn above so
			// the read-back is a complete image. Same shape as `Client::TakeScreenShot`.
			// Shares the client's Screenshots/ numbering and cg_screenshotFormat.
			if (wantScreenShot) {
				wantScreenShot = false;
				renderer->FrameDone();
				try {
					std::string name = client::SaveScreenShot(*renderer);
					editor->SetStatus("Screenshot saved: " + name);
				} catch (const std::exception& ex) {
					editor->SetStatus("Screenshot failed");
					SPLog("Editor screenshot failed: %s", ex.what());
				}
			}
		}
	} // namespace gui
} // namespace spades
