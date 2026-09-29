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
#include <Core/Debug.h>
#include <Core/LocalFileSystem.h>
#include <Core/Settings.h>
#include <Core/VoxelModel.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/ColorPicker.h>
#include <Gui/UI/Components/FileBrowser/FileBrowserDialog.h>
#include <Gui/UI/Components/OptionBar.h>
#include <Gui/UI/Components/UIOverlayHost.h>
#include <Gui/UI/Editor/Shell/EditorFileDialog.h>
#include <Gui/UI/Editor/Shell/EditorKeys.h>
#include <Gui/UI/Editor/Shell/EditorUI.h>
#include <Gui/UI/Editor/Voxel/VoxelEditor.h>
#include <Gui/UI/Editor/Voxel/VoxelOverlayLines.h>
#include <Gui/UI/Widgets/MessageBox.h>

#include "KV6ModelIO.h"

SPADES_SETTING(cg_keyMoveForward);
SPADES_SETTING(cg_keyMoveBackward);
SPADES_SETTING(cg_keyMoveLeft);
SPADES_SETTING(cg_keyMoveRight);
SPADES_SETTING(cg_keyJump);
SPADES_SETTING(cg_keyCrouch);
SPADES_SETTING(cg_keySprint);
SPADES_SETTING(cg_keyScreenshot);

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

			// Edge length of the cube a new model's volume starts as.
			constexpr int kNewModelSize = 32;
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

			ui = Handle<EditorUI>::New(renderer.GetPointerOrNull(), audioDevice.GetPointerOrNull(),
			                           &*fontManager, *this, softwareCursor);
			editor = std::make_unique<VoxelEditor>(document, *this, *renderer, *fontManager, *ui,
			                                       *softwareCursor, VoxelEditor::Config());

			// The editor always opens with a document. A file the player asked to
			// create is started under its name; one that exists but cannot be read
			// leaves an untitled document instead, because naming it after a file
			// whose contents are still on disk is how Save comes to overwrite
			// something this editor never managed to read.
			if (isNew || path.empty())
				NewModel(kNewModelSize, path);
			else if (!LoadModel(path))
				NewModel(kNewModelSize, std::string());
		}

		KV6EditorView::~KV6EditorView() { SPADES_MARK_FUNCTION(); }

		// --- Document ---------------------------------------------------------

		bool KV6EditorView::IsDirty() const { return editor->DocumentStateId() != savedDocumentId; }

		bool KV6EditorView::HasUnsavedChanges() const { return IsDirty() || editor->HasPlacement(); }

		void KV6EditorView::FrameCamera() {
			const IntVector3 size = document.Size();
			cam.Frame(MakeVector3(size.x * 0.5F - 0.5F, size.y * 0.5F - 0.5F, size.z * 0.5F - 0.5F),
			          float(std::max(size.x, std::max(size.y, size.z))));
		}

		void KV6EditorView::NewModel(int n, const std::string& path) {
			Handle<VoxelModel> model = Handle<VoxelModel>::New(n, n, n);
			model->SetSolid(n / 2, n / 2, n / 2, editor->CurrentColor());
			// Anchor the pivot on the seed voxel so mirroring (whose planes start
			// there) is symmetric about it from the start.
			float c = float(n / 2);
			model->SetOrigin(MakeVector3(-c, -c, -c));
			document.Assign(model);
			editor->DocumentReplaced();
			filePath = path;
			FrameCamera();
			savedDocumentId = -1; // a fresh, never-saved document starts dirty
		}

		bool KV6EditorView::LoadModel(const std::string& path) {
			Handle<VoxelModel> loaded = KV6ModelIO::Load(path);
			if (!loaded) {
				// The document that is open is not the one that failed to load, so it
				// stays exactly as it is; the caller says how to break the news.
				editor->SetStatus("Could not load " + path);
				return false;
			}
			document.Assign(loaded);
			editor->DocumentReplaced();
			filePath = path;
			FrameCamera();
			savedDocumentId = editor->DocumentStateId(); // a freshly loaded document is clean
			return true;
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
			if (!KV6ModelIO::Save(document.Model(), filePath)) {
				editor->SetStatus("Save failed");
				return false;
			}
			savedDocumentId = editor->DocumentStateId(); // this document state is now clean
			editor->SetStatus("Saved " + filePath);
			return true;
		}

		std::string KV6EditorView::GetMenuTitle() {
			const std::string name = filePath.empty() ? std::string("Untitled model")
			                                          : LocalFileSystem::GetFileName(filePath);
			return HasUnsavedChanges() ? name + " *" : name;
		}

		void KV6EditorView::NewDocument() {
			ConfirmDiscardChanges([this] {
				NewModel(kNewModelSize, std::string());
				editor->SetStatus("New model");
			});
		}

		std::vector<EditorMenuItem> KV6EditorView::GetMenuItems() {
			std::vector<EditorMenuItem> items;
			// The file commands first, then the one command that edits the open
			// document with a file: inserting a model is a paste that happens to come
			// from disk, and it belongs beside Paste rather than beside Save.
			items.push_back(EditorMenuItem{"New Model", [this] { NewDocument(); }, true});
			items.push_back(EditorMenuItem{"Open Model...", [this] { OpenDocument(); }, true});
			// Always available: a document with nowhere to go yet is asked where.
			items.push_back(EditorMenuItem{"Save", [this] { Save(); }, true});
			items.push_back(EditorMenuItem{"Save As...", [this] { OpenSaveAsDialog(); }, true});
			items.push_back(EditorMenuItem{"Insert Model...", [this] { OpenInsertDialog(); }, true});
			items.push_back(EditorMenuItem{
			  "Exit to Menu", [this] { ConfirmDiscardChanges([this] { wantsClose = true; }); }, true});
			return items;
		}

		bool KV6EditorView::OnMenuEscape() {
			// The menu asks before opening, so it only opens once nothing is left
			// to back out of.
			return editor->EscapeOnce();
		}

		void KV6EditorView::ShowMessage(const std::string& text) {
			// An alert rather than a message box with one button: it is the same
			// thing to look at, and it answers to Enter and Escape as every other
			// dismissable box in the game does.
			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<AlertScreen> box =
			  Handle<AlertScreen>::New(&overlay->GetUIManager().GetRootElement(), text, 120.0F);
			editor->ReleaseHeldInput();
			overlay->Show(box.GetPointerOrNull());
		}

		void KV6EditorView::ConfirmDiscardChanges(std::function<void()> proceed) {
			if (!proceed)
				return;
			if (!HasUnsavedChanges()) {
				proceed();
				return;
			}

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<MessageBoxScreen> box = Handle<MessageBoxScreen>::New(
			  &overlay->GetUIManager().GetRootElement(), "This model has unsaved changes.",
			  std::vector<std::string>{"Save", "Discard", "Cancel"}, 160.0F);
			box->closed = [this, proceed](ui::UIElement& sender) {
				MessageBoxScreen* answer = dynamic_cast<MessageBoxScreen*>(&sender);
				if (!answer)
					return;
				if (answer->resultIndex == 1) { // Discard
					proceed();
				} else if (answer->resultIndex == 0) { // Save
					// A document that was never saved needs somewhere to go first;
					// `proceed` then runs only if that save succeeds.
					if (filePath.empty())
						OpenSaveAsDialog(proceed);
					else if (Save())
						proceed();
				}
			};
			editor->ReleaseHeldInput();
			overlay->Show(box.GetPointerOrNull());
		}

		void KV6EditorView::ShowModelFileDialog(const std::string& title, FileBrowserPurpose purpose,
		                                        const std::string& initialName,
		                                        std::function<void(const std::string&)> picked) {
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
				options.filters.push_back(FileFilter{DocumentFilterLabel(), {GetDocumentExtension()}});
			}
			options.homeDir = EditorHomeDir();
			// The document's own folder is where this document's dialogs belong;
			// anything else opens where the player last was, here or on the main
			// screen.
			options.initialDir = filePath.empty() ? EditorRememberedFolder(EditorHomeDir())
			                                      : LocalFileSystem::ParentDir(filePath);
			options.initialName = initialName;

			UIOverlayHost* overlay = ui->GetOverlay();
			Handle<FileBrowserDialog> dialog = Handle<FileBrowserDialog>::New(
			  &overlay->GetUIManager().GetRootElement(), title, std::move(options));
			dialog->closed = [picked](const FileBrowserResult& result) {
				// Where the player ended up is where every model dialog opens next,
				// whether or not they picked something here.
				EditorRememberFolder(result.directory);
				if (result.accepted && !result.paths.empty())
					picked(result.paths.front());
			};
			// A dialog takes over input: nothing should stay held while it is up.
			editor->ReleaseHeldInput();
			overlay->Show(dialog.GetPointerOrNull());
		}

		void KV6EditorView::OpenDocument() {
			ConfirmDiscardChanges([this] {
				ShowModelFileDialog("Open Model", FileBrowserPurpose::Open, std::string(),
				                    [this](const std::string& path) {
					                    if (!LoadModel(path))
						                    ShowMessage("Could not open " +
						                                LocalFileSystem::GetFileName(path) + ".");
				                    });
			});
		}

		void KV6EditorView::OpenSaveAsDialog(std::function<void()> after) {
			const std::string name = filePath.empty() ? UntitledFileName(DocumentKind::Model)
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

		bool KV6EditorView::KeyIsReserved(const std::string& key) const {
			const std::string bound[] = {cg_keyMoveForward, cg_keyMoveBackward, cg_keyMoveLeft,
			                             cg_keyMoveRight,   cg_keyJump,         cg_keyCrouch,
			                             cg_keySprint,      cg_keyScreenshot};
			for (const std::string& b : bound) {
				if (EditorKeyMatches(b, key))
					return true;
			}
			return false;
		}

		bool KV6EditorView::ClickViewportWidget(const Vector2& cursor) {
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
			Vector3 org = document.Origin();
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

			std::string name = (!filePath.empty()) ? filePath : "(unsaved model)";
			if (HasUnsavedChanges())
				name += " *";
			font.Draw("KV6 Editor", MakeVector2(12.0F, 4.0F), 0.95F, MakeVector4(1, 1, 1, 1));
			font.Draw(name + "   (" + std::to_string(document.VoxelCount()) + " voxels)",
			          MakeVector2(120.0F, 5.0F), 0.85F, MakeVector4(0.75F, 0.75F, 0.78F, 1.0F));

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
			softwareCursor->Accumulate(dx, dy);
			editor->PointerMoved(MakeVector2(dx, dy));
		}

		void KV6EditorView::WheelEvent(float x, float y) {
			if (ui->GetOverlay()->WheelEvent(x, y))
				return;
			if (ui->GetEditorMenu()->IsActive())
				return;
			cam.Zoom(y);
		}

		void KV6EditorView::KeyEvent(const std::string& key, bool down) {
			// Modifier state must track the keyboard even while a modal swallows
			// keys, or a Ctrl released during the menu stays "held" afterwards.
			editor->TrackModifiers(key, down);
			cam.TrackModifiers(key, down);

			// A modal dialog (file browser) owns every key while it is up.
			if (ui->GetOverlay()->KeyEvent(key, down))
				return;

			if (ui->GetEditorMenu()->KeyEvent(key, down)) {
				// Releases are swallowed too while the menu is up: drop held
				// movement/look so nothing keeps going once it closes.
				editor->ReleaseHeldInput();
				return;
			}

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
				cam.SetLooking(down);
				return;
			}
			if (key == "LeftMouseButton") {
				editor->ButtonEvent(PointerButton::Left, down);
				return;
			}
			if (key == "RightMouseButton") {
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
			editor->ToolKey(key, down);
		}

		void KV6EditorView::TextInputEvent(const std::string& text) {
			// A dialog takes typing over everything; otherwise it belongs to a
			// number box on the sub-toolbar, when one is being typed into.
			if (ui->GetOverlay()->IsActive())
				ui->GetOverlay()->TextInputEvent(text);
			else
				ui->GetOptionBar()->TextInputEvent(text);
		}

		void KV6EditorView::TextEditingEvent(const std::string& text, int start, int len) {
			ui->GetOverlay()->TextEditingEvent(text, start, len);
		}

		bool KV6EditorView::AcceptsTextInput() {
			return ui->GetOverlay()->AcceptsTextInput() || ui->GetOptionBar()->IsEditing();
		}

		AABB2 KV6EditorView::GetTextInputRect() {
			if (ui->GetOverlay()->IsActive())
				return ui->GetOverlay()->GetTextInputRect();
			return ui->GetOptionBar()->EditingRect();
		}

		void KV6EditorView::RunFrame(float dt) {
			SPADES_MARK_FUNCTION();
			float sw = renderer->ScreenWidth();
			float sh = renderer->ScreenHeight();
			globalTime += dt;

			// The movement keys fly the camera unless a modal has the keyboard;
			// its speed follows the model's size, so any model is crossed as fast.
			const IntVector3 size = document.Size();
			const int span = std::max(size.x, std::max(size.y, size.z));
			cam.Update(dt, globalTime, float(span) * kCameraSpeedPerSize,
			           !ui->GetEditorMenu()->IsActive() && !ui->GetOverlay()->IsActive());
			editor->Update(dt);

			renderer->SetFogColor(MakeVector3(0.10F, 0.10F, 0.12F));
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
			renderer->StartScene(sceneDef);
			if (renderModel) {
				client::ModelRenderParam param;
				// Cancel the KV6 pivot so the model sits in the editor's grid space.
				param.matrix = Matrix4::Translate(document.Origin() * -1.0F);
				renderer->RenderModel(*renderModel, param);
			}
			DrawHelpers();
			DrawOriginAxes();
			editor->DrawScene();
			renderer->EndScene();

			editor->DrawViewportOverlays(sw, sh, HelpLine());
			DrawRibbon(sw);
			editor->DrawBars(sw);

			// The navigation cube, top right under the bars.
			naviCube.Layout(sw - kNaviCubeMargin - kNaviCubeBox * 0.5F,
			                VoxelEditor::BarsHeight() + kNaviCubeTopGap + kNaviCubeBox * 0.5F,
			                kNaviCubeBox * 0.5F - kNaviCubeInset);
			naviCube.Draw(*renderer, fontManager->GetSmallGuiFont(), cam.View(),
			              softwareCursor->GetPosition());

			editor->DrawPanels();
			ui->GetEditorMenu()->Draw();

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
