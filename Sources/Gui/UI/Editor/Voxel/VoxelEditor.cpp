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
// VoxelEditor: its making, the document, Escape, undo, the tools and the frame's update.

#include "VoxelEditorInternal.h"

DEFINE_SPADES_SETTING(cg_keyDelete, "Delete");

namespace spades {
	namespace gui {
		/**
		 * The model's pivot, as the transform panel sets it: a Location only,
		 * previewed live and kept as one Set Pivot step.
		 */
		class VoxelEditor::PivotTarget final : public ITransformTarget {
		public:
			explicit PivotTarget(VoxelEditor& editor) : editor(editor) {}

			unsigned ShownRows() const override { return kLocation; }
			std::string Title() const override { return "Pivot"; }

			bool CurrentTransform(Transform& t, std::string& name, bool& onlyMoves) const override {
				t.position = editor.GetPivot();
				t.rotation = MakeVector3(0.0F, 0.0F, 0.0F);
				t.scale = MakeVector3(1.0F, 1.0F, 1.0F);
				name = "of the " + editor.document.Noun();
				onlyMoves = true;
				return true;
			}
			void PreviewTransform(const Transform& t) override {
				previewing = true;
				value = t.position;
				editor.PreviewPivot(value);
			}
			void CommitTransform() override {
				if (!previewing)
					return;
				previewing = false;
				editor.EndPreviews(); // where it was, so the step runs from there
				editor.SetPivot(value);
			}
			void CancelTransform() override {
				if (!previewing)
					return;
				previewing = false;
				editor.EndPreviews();
			}

		private:
			VoxelEditor& editor;
			bool previewing = false;
			Vector3 value = MakeVector3(0.0F, 0.0F, 0.0F);
		};

		VoxelEditor::VoxelEditor(IVoxelDocument& document, IVoxelEditorHost& host,
		                         client::IRenderer& renderer, client::FontManager& fontManager,
		                         EditorUI& ui, SoftwareCursor& cursor, const Config& config)
		    : document(document),
		      host(host),
		      renderer(&renderer),
		      fontManager(&fontManager),
		      ui(ui),
		      cursor(cursor),
		      undo(*this, config.undo),
		      placeAtAim(config.placeAtAim) {
			SPADES_MARK_FUNCTION();
			transformPanel.reset(new TransformPanel(renderer, fontManager.GetSmallGuiFont()));
			pivotTarget.reset(new PivotTarget(*this));

			// The tools come from the registry (toolbar order = registration),
			// narrowed to the ones this editor offers.
			VoxelTools().BuildAll(tools);
			if (!config.tools.empty()) {
				tools.erase(std::remove_if(tools.begin(), tools.end(),
				                           [&](const VoxelToolSlot& slot) {
					                           return std::find(config.tools.begin(),
					                                            config.tools.end(),
					                                            slot.id) == config.tools.end();
				                           }),
				            tools.end());
			}
			// Open on the tool registered for it, or the first.
			activeTool = std::max(0, ToolWithRole(ToolRole::Initial));
			WireUI();
			BuildPointerLayers();
		}

		VoxelEditor::~VoxelEditor() { SPADES_MARK_FUNCTION(); }

		ITransformTarget& VoxelEditor::PivotTransform() { return *pivotTarget; }

		ITransformTarget* VoxelEditor::PanelTarget() {
			VoxelTool* t = ActiveTool();
			return t ? t->TransformTarget(*this) : nullptr;
		}

		void VoxelEditor::SettlePanel() {
			if (!transformPanel->IsTyping() && !transformPanel->IsHolding())
				return;
			if (ITransformTarget* target = PanelTarget())
				transformPanel->Settle(*target);
		}

		bool VoxelEditor::IsTyping() const {
			return transformPanel->IsTyping() || BarsEditing();
		}

		bool VoxelEditor::HoldsPointer() const { return transformPanel->IsHolding(); }

		bool VoxelEditor::OnPanel(const Vector2& at) {
			return transformPanel->Contains(at) && !ui.GetColorPicker()->IsOverPicker(at);
		}

		void VoxelEditor::TextInput(const std::string& text) {
			if (transformPanel->IsTyping()) {
				TextEntry::Modifiers m;
				m.ctrl = ctrlHeld;
				m.alt = altHeld;
				m.meta = metaHeld;
				if (TextEntry::Types(m)) // not the letter of a shortcut
					transformPanel->Insert(text);
				return;
			}
			ui.GetOptionBar()->TextInputEvent(text); // whichever is being typed into
			ui.GetContextStrip()->TextInputEvent(text);
		}

		bool VoxelEditor::BarsEditing() const {
			return ui.GetOptionBar()->IsEditing() || ui.GetContextStrip()->IsEditing();
		}

		void VoxelEditor::EndBarEdits(bool commit) {
			ui.GetOptionBar()->EndEditing(commit);
			ui.GetContextStrip()->EndEditing(commit);
		}

		void VoxelEditor::TextComposition(const std::string& text) {
			if (transformPanel->IsTyping())
				transformPanel->Compose(text);
		}

		bool VoxelEditor::AcceptsTextInput() const { return IsTyping(); }

		AABB2 VoxelEditor::TextInputRect() const {
			if (transformPanel->IsTyping())
				return transformPanel->TextRect();
			return ui.GetContextStrip()->IsEditing() ? ui.GetContextStrip()->EditingRect()
			                                         : ui.GetOptionBar()->EditingRect();
		}

		void VoxelEditor::WireUI() {
			// Buttons report ids, resolved against the tools as they are now; the
			// toolbar only reports clicks on enabled buttons.
			ui.GetToolbar()->OnToolClicked = [this](const std::string& id) {
				SetActiveTool(ToolIndex(id));
			};
			ui.GetToolbar()->OnUndoClicked = [this]() { Undo(); };
			ui.GetToolbar()->OnRedoClicked = [this]() { Redo(); };

			// The option bar's content belongs to the tool whose bar it is, the
			// strip's to the tool it is the strip of; a click only counts while
			// that tool still is the one, and goes to it whichever is active.
			WireBar(*ui.GetOptionBar(), [this] { return BarToolIndex(); });
			WireBar(*ui.GetContextStrip(), [this] { return StripToolIndex(); });

			// The brush colour is the editor's, shared by every tool: its swatch
			// sits on the main toolbar and opens or closes the picker, which
			// edits it and stays up across tool switches until closed.
			ui.GetToolbar()->OnColorSwatchClicked = [this] {
				ColorPicker& picker = *ui.GetColorPicker();
				if (picker.IsOpen()) {
					picker.Close();
				} else {
					picker.SetColor(PickedColor());
					picker.Open();
				}
			};
			// The active tool follows the colour as it is chosen (Select
			// recolours the selection with it), live while the picker is pressed.
			ui.GetColorPicker()->OnColorChanged = [this](std::uint32_t c) {
				// A tool with a colour of its own for the picker has it set
				// and that is all: the brush, and what follows it, stay put.
				if (SetToolColor(c))
					return;
				currentColor = c;
				if (pickerHeld)
					pickerChangedColor = true;
				BrushColorChanged(!pickerHeld);
			};
		}

		const ToolOption* VoxelEditor::ToolColorOption() {
			VoxelTool* tool = ActiveTool();
			ToolOptions* options = tool ? tool->Options() : nullptr;
			if (!options)
				return nullptr;
			tool->UpdateOptions(*this); // as the tool holds it now
			return options->First(ToolOption::Type::Color);
		}

		std::uint32_t VoxelEditor::PickedColor() {
			const ToolOption* own = ToolColorOption();
			return own ? own->color : currentColor;
		}

		bool VoxelEditor::SetToolColor(std::uint32_t color) {
			const ToolOption* own = ToolColorOption();
			if (!own)
				return false;
			const std::string id = own->id; // the handler may rebuild the options
			ActiveTool()->OnOptionColorChanged(*this, id, color);
			return true;
		}

		void VoxelEditor::WireBar(OptionBar& bar, std::function<int()> indexOf) {
			auto toolOf = [this, indexOf]() -> VoxelTool* {
				const int index = indexOf();
				return index < 0 ? nullptr : tools[static_cast<size_t>(index)].tool.get();
			};
			bar.CurrentOwner = [toolOf] { return static_cast<const void*>(toolOf()); };
			bar.OnSubToolClicked = [this, indexOf](int sub) {
				const int index = indexOf();
				SetActiveTool(index); // says why not, when it has nothing to work on
				if (index >= 0 && activeTool == index)
					tools[static_cast<size_t>(index)].tool->SetSubTool(*this, sub);
			};
			bar.OnBoolToggled = [this, toolOf](const std::string& id) {
				VoxelTool* tool = toolOf();
				if (ToolOption* opt = FindOption(tool, id, ToolOption::Type::Bool)) {
					opt->bvalue = !opt->bvalue;
					tool->OnOptionToggled(*this, id, opt->bvalue);
				}
			};
			bar.OnActionClicked = [this, toolOf](const std::string& id) {
				VoxelTool* tool = toolOf();
				const ToolOption* opt = FindOption(tool, id, ToolOption::Type::Action);
				if (!opt || !opt->enabled)
					return;
				// A command of a tool that is not the active one runs behind
				// the active one's back, as a command from a menu does.
				if (tool != ActiveTool())
					CancelToolInteraction();
				tool->OnAction(*this, id);
			};
			bar.OnNumberChanged = [this, toolOf](const std::string& id, float value,
			                                     bool committed) {
				VoxelTool* tool = toolOf();
				if (!FindOption(tool, id, ToolOption::Type::Number))
					return;
				// A settled value is one user action, so a typed number or an
				// arrow press undoes in one go; an unsettled one only previews
				// and has nothing to record.
				if (committed)
					undo.BeginAction();
				UserActionEnd end(*this, committed);
				tool->OnOptionNumberChanged(*this, id, value, committed);
			};
		}

		void VoxelEditor::BrushColorChanged(bool committed) {
			if (VoxelTool* t = ActiveTool())
				t->OnBrushColorChanged(*this, currentColor, committed);
		}

		void VoxelEditor::EndPickerPress(bool commit) {
			pickerHeld = false;
			ui.GetColorPicker()->MouseUp();
			const bool changed = pickerChangedColor;
			pickerChangedColor = false;
			// A press of the other button began the action this one joined, and
			// ends it on its own release.
			UserActionEnd end(*this, !rmbHeld);
			if (commit && changed)
				BrushColorChanged(true);
		}

		float VoxelEditor::RibbonHeight() { return kRibbonH; }
		float VoxelEditor::BarsHeight() { return kBarsH; }

		// --- Document ---------------------------------------------------------

		void VoxelEditor::DocumentReplaced() {
			// Nothing of the old document's edit state carries over but which
			// axes mirror: that is how the user works, not part of the document.
			MirrorSetup kept;
			for (int a = 0; a < 3; a++)
				kept.enabled[a] = edit.mirror.enabled[a];
			edit = EditState();
			edit.mirror = kept;
			movedMiddle = SelectionMiddle();
			previewedOrigin.reset();
			previewedMirrorPlane.reset();
			// What a live recolour painted over was the old document's.
			previewedColors.reset();
			previewedPlacement.reset();
			undo.Clear();
			// The mirror planes start on the new document's pivot.
			PlaceMirrorPlane(GetPivot());
			NoteDocumentColors();
			NotifyDocumentChanged();
		}

		bool VoxelEditor::InBounds(int x, int y, int z) const {
			const IntVector3 size = document.Size();
			return x >= 0 && y >= 0 && z >= 0 && x < size.x && y < size.y && z < size.z;
		}

		bool VoxelEditor::Editable(int x, int y, int z) const {
			return InBounds(x, y, z) && document.IsEditable(x, y, z);
		}

		void VoxelEditor::SetStatus(const std::string& s) {
			statusMessage = s;
			statusTimer = 2.5F;
		}

		std::string VoxelEditor::SizeLimitMessage() const {
			return "Reached the maximum " + document.Noun() + " size";
		}

		std::string VoxelEditor::SizeLimitMessage(const IntVector3& wanted) const {
			// Which way it ran out, and by how much. "Too large" leaves the user
			// to guess which of three limits they met and how far off they are,
			// and the three are not alike: a model may be thousands of voxels
			// across and only a few dozen deep.
			const IntVector3 limit = document.MaxSize();
			const char* const axes[3] = {"X", "Y", "Z"};
			const int want[3] = {wanted.x, wanted.y, wanted.z};
			const int most[3] = {limit.x, limit.y, limit.z};
			for (int a = 0; a < 3; a++) {
				if (want[a] > most[a]) {
					return "A " + document.Noun() + " reaches " + std::to_string(most[a]) +
					       " voxels along " + axes[a] + ", and this would need " +
					       std::to_string(want[a]);
				}
			}
			return SizeLimitMessage();
		}

		// --- Escape -----------------------------------------------------------

		VoxelEditor::EscapeLayer VoxelEditor::NextEscape() {
			// A number is a number wherever it is being typed: the panel's, and
			// the bar's boxes, which answer for their own Escape through
			// OptionBar but never see it once Escape has somewhere else to go.
			if (transformPanel->IsTyping() || transformPanel->IsHolding() ||
			    BarsEditing())
				return EscapeLayer::Number;
			if (ui.GetColorPicker()->GetEyedropperMode())
				return EscapeLayer::Eyedropper;
			if (VoxelTool* tool = ActiveTool()) {
				if (!tool->EscapeLabel(*this).empty())
					return EscapeLayer::Tool;
			}
			if (edit.placing)
				return EscapeLayer::Placement;
			if (!edit.selection.Empty())
				return EscapeLayer::Selection;
			return EscapeLayer::None;
		}

		std::string VoxelEditor::EscapeLabel(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Number: return "give the number up";
				case EscapeLayer::Eyedropper: return "stop picking";
				case EscapeLayer::Tool: return ActiveTool()->EscapeLabel(*this);
				case EscapeLayer::Placement: return "drop the pending voxels";
				case EscapeLayer::Selection: return "select nothing";
				case EscapeLayer::None: break;
			}
			// Nothing in hand: what Escape does then is the host's to decide
			// (this one leaves, asking about unsaved work first).
			return "exit to menu";
		}

		void VoxelEditor::Escape(EscapeLayer layer) {
			switch (layer) {
				case EscapeLayer::Number:
					if (BarsEditing()) {
						EndBarEdits(false); // what was typed is given up
						break;
					}
					if (ITransformTarget* target = PanelTarget()) {
						if (!transformPanel->CancelScrub(*target))
							transformPanel->Key(*target, "Escape", TextEntry::Modifiers(), clock);
					}
					break;
				case EscapeLayer::Eyedropper: ui.GetColorPicker()->SetEyedropperMode(false); break;
				case EscapeLayer::Tool: ActiveTool()->OnEscape(*this); break;
				case EscapeLayer::Placement: CancelPlacement(); break;
				case EscapeLayer::Selection:
					ClearSelection();
					SetStatus("Selection cleared");
					break;
				case EscapeLayer::None: break;
			}
		}

		bool VoxelEditor::EscapeOnce() {
			const EscapeLayer layer = NextEscape();
			Escape(layer);
			return layer != EscapeLayer::None;
		}

		// --- Undo / redo ------------------------------------------------------

		void VoxelEditor::WriteVoxel(int x, int y, int z, bool solid, std::uint32_t color) {
			if (!Editable(x, y, z))
				return;
			if (!solid)
				edit.selection.Remove(MakeIntVector3(x, y, z)); // it holds solid voxels only
			const bool oldSolid = document.IsSolid(x, y, z);
			const std::uint32_t oldColor = document.Color(x, y, z);
			const std::uint32_t newColor = solid ? (color & 0xFFFFFF) : oldColor; // air keeps its colour
			document.Write(x, y, z, solid, newColor);
			undo.RecordVoxel(x, y, z, oldSolid, oldColor, solid, newColor);
		}

		void VoxelEditor::UndoApplyVoxel(int x, int y, int z, bool solid, std::uint32_t color) {
			document.Write(x, y, z, solid, color);
		}

		void VoxelEditor::UndoApplyReframe(int w, int h, int d, int ox, int oy, int oz) {
			ReframeRaw(MakeIntVector3(w, h, d), MakeIntVector3(ox, oy, oz));
		}

		void VoxelEditor::UndoApplyOrigin(const Vector3& origin) { document.SetOrigin(origin); }

		// Undo and redo replay the one history, pending voxels included: a
		// move or a turn is a step like any edit, so nothing is placed first.
		void VoxelEditor::Undo() {
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.UndoLabel();
			if (!undo.Undo()) {
				SetStatus("Nothing to undo");
				return;
			}
			SetStatus("Undid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}

		void VoxelEditor::Redo() {
			EndPreviews(); // the history holds only what was committed
			std::string label = undo.RedoLabel();
			if (!undo.Redo()) {
				SetStatus("Nothing to redo");
				return;
			}
			SetStatus("Redid " + (label.empty() ? std::string("edit") : label));
			HistoryReplayed();
		}

		void VoxelEditor::HistoryReplayed() {
			// The middle a moved selection turns about is no part of the history;
			// the selection the replay restored starts again from its box.
			movedMiddle.valid = false;
			if (edit.placing)
				ActivatePlacementTool();
			NotifyDocumentChanged();
		}

		// --- Tools and modes --------------------------------------------------

		std::string VoxelEditor::ToolLabel(int index) const {
			if (index < 0 || index >= int(tools.size()))
				return std::string();
			return tools[static_cast<size_t>(index)].tool->Label();
		}

		bool VoxelEditor::ToolAvailable(int index) const {
			if (index < 0 || index >= int(tools.size()))
				return false;
			// What the commands act on: the selection, or voxels waiting to be
			// placed while there are some.
			return !tools[static_cast<size_t>(index)].traits.needsSelection || SelectionCount() > 0;
		}

		void VoxelEditor::SelectTool(int index) { SetActiveTool(index); }

		void VoxelEditor::SetActiveTool(int index) {
			if (!ToolAvailable(index)) {
				if (index >= 0 && index < int(tools.size()))
					SetStatus("Select some voxels first");
				return;
			}
			Activate(index);
		}

		void VoxelEditor::Activate(int toolIndex) {
			if (toolIndex == activeTool)
				return;
			// A number being typed into the sub-toolbar, or the panel, belongs to
			// the tool whose box it is: it is settled here, while that tool is
			// still the one listening. Left to the bar, the edit would be dropped
			// on the next rebuild with whatever it was previewing still showing.
			SettlePanel();
			EndBarEdits(true);
			// Leaving a tool ends what it had in progress: this is where a pending
			// placement reaches the document.
			if (VoxelTool* previous = ActiveTool())
				previous->OnDeactivate(*this);
			activeTool = toolIndex;
			if (VoxelTool* current = ActiveTool())
				current->OnActivate(*this);
			// The picker shows the colour it now sets, which may be another's.
			if (ui.GetColorPicker()->IsOpen())
				ui.GetColorPicker()->SetColor(PickedColor());
		}

		int VoxelEditor::ToolIndex(const std::string& id) const {
			for (size_t i = 0; i < tools.size(); i++) {
				if (tools[i].id == id)
					return int(i);
			}
			return -1;
		}

		int VoxelEditor::StripToolIndex() const {
			if (activeTool < 0 || activeTool >= int(tools.size()))
				return -1;
			const VoxelToolSlot& current = tools[static_cast<size_t>(activeTool)];
			if (!current.traits.stripOf.empty())
				return activeTool; // lit on its own strip, whatever is left to it
			for (size_t i = 0; i < tools.size(); i++) {
				if (tools[i].traits.stripOf == current.id && ToolAvailable(int(i)))
					return int(i);
			}
			return -1;
		}

		VoxelTool* VoxelEditor::StripTool() {
			const int index = StripToolIndex();
			return index < 0 ? nullptr : tools[static_cast<size_t>(index)].tool.get();
		}

		ToolOption* VoxelEditor::FindOption(VoxelTool* tool, const std::string& id,
		                                    ToolOption::Type type) {
			if (!tool)
				return nullptr;
			ToolOptions* options = tool->Options();
			ToolOption* option = options ? options->Find(id) : nullptr;
			return (option && option->type == type) ? option : nullptr;
		}

		int VoxelEditor::BarToolIndex() const {
			if (activeTool < 0 || activeTool >= int(tools.size()))
				return -1;
			const int host = ToolIndex(tools[static_cast<size_t>(activeTool)].traits.stripOf);
			return host >= 0 ? host : activeTool;
		}

		VoxelTool* VoxelEditor::BarTool() {
			const int index = BarToolIndex();
			return index < 0 ? nullptr : tools[static_cast<size_t>(index)].tool.get();
		}

		// A sub-tool's key is its own where it declares one, and otherwise its
		// place in the bar. A sub-tool that was a tool of its own keeps the key
		// it was known by (Transform's T), and the digits stay where they are
		// for the rest, so nothing shifts under the user when one is added.
		std::string VoxelEditor::SubToolHotKey(VoxelTool& tool, int index) {
			if (auto* sub = tool.SubTool(index)) {
				const std::string own = sub->HotKey();
				if (!own.empty())
					return own;
			}
			// A strip is not the bar the digits count along.
			if (IsStrip(tool))
				return std::string();
			return (index >= 0 && index < 9) ? std::to_string(index + 1) : std::string();
		}

		bool VoxelEditor::IsStrip(const VoxelTool& tool) const {
			for (const VoxelToolSlot& slot : tools) {
				if (slot.tool.get() == &tool)
					return !slot.traits.stripOf.empty();
			}
			return false;
		}

		int VoxelEditor::ToolWithRole(ToolRole role) const {
			for (size_t i = 0; i < tools.size(); i++) {
				if (tools[i].traits.role == role)
					return int(i);
			}
			return -1;
		}

		void VoxelEditor::ReturnToSelect() {
			const int selection = ToolWithRole(ToolRole::Selection);
			if (selection < 0)
				return; // an editor offering no way of choosing has nowhere to go back to
			clearingSelection = true;
			pendingTool = tools[static_cast<size_t>(selection)].id;
		}

		bool VoxelEditor::KeyIsTaken(const std::string& key) const {
			return host.KeyIsReserved(key) || EditorKeyMatches(cg_keyDelete, key);
		}

		bool VoxelEditor::SwitchByHotKey(const std::string& key) {
			// Never mid-press: the tool taking over would see a drag it never saw start.
			if (key.empty() || lmbHeld || rmbHeld || KeyIsTaken(key))
				return false;
			for (size_t i = 0; i < tools.size(); i++) {
				if (!tools[i].hotKey.empty() && EqualsIgnoringCase(tools[i].hotKey, key)) {
					SetActiveTool(int(i));
					return true;
				}
			}
			// A key a sub-tool declares as its own (Transform's T) is good from
			// any tool; the digits, being only a place in the active tool's
			// bar, are not.
			// The digits count along the bar that is showing, which a strip
			// leaves to the tool it is the strip of.
			VoxelTool* active = BarTool();
			for (size_t i = 0; i < tools.size(); i++) {
				VoxelTool& tool = *tools[i].tool;
				for (int sub = 0; sub < tool.SubToolCount(); sub++) {
					const std::string subKey = SubToolHotKey(tool, sub);
					auto* candidate = tool.SubTool(sub);
					const bool own = candidate && !candidate->HotKey().empty();
					if ((!own && &tool != active) || !EqualsIgnoringCase(subKey, key))
						continue;
					SetActiveTool(int(i)); // says why not, when it has nothing to work on
					if (activeTool == int(i))
						tool.SetSubTool(*this, sub);
					return true;
				}
			}
			return false;
		}

		VoxelTool* VoxelEditor::ActiveTool() {
			if (activeTool >= 0 && activeTool < int(tools.size()))
				return tools[activeTool].tool.get();
			return nullptr;
		}

		// --- Frame ------------------------------------------------------------

		void VoxelEditor::Update(float dt) {
			clock += dt;
			if (statusTimer > 0.0F)
				statusTimer -= dt;
			RefreshPlacementModel(); // what edits since the last frame left stale
			// A tool left with nothing to work on -- the selection deleted,
			// cleared or undone from under Modify -- hands over to Select.
			// Never mid-press: the tool taking over would see a drag it never
			// saw start.
			if (!lmbHeld && !rmbHeld && !ToolAvailable(activeTool)) {
				const int selection = ToolWithRole(ToolRole::Selection);
				if (ToolAvailable(selection))
					SetActiveTool(selection);
			}
		}
	} // namespace gui
} // namespace spades
