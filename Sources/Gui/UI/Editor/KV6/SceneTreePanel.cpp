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

#include "SceneTreePanel.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>
#include <Gui/UI/Components/EditorTheme.h>

namespace spades {
	namespace gui {
		namespace {
			constexpr float kRowH = 19.0F;
			constexpr float kWidth = 230.0F;
			constexpr float kIndent = 14.0F;
			constexpr float kFoldW = 12.0F;  // the arrow's box, left of the label
			constexpr float kEyeW = 18.0F;   // the eye's box, at the row's end
			constexpr float kLockW = 16.0F;  // the lock's, before the eye
			constexpr float kSlack = 6.0F;   // the selection bar's room, left of the rows
			constexpr float kTextScale = 0.9F;
			constexpr float kDragStart = 4.0F; // pixels a press moves before it drags
			constexpr float kWheelRows = 2.0F; // rows a wheel step scrolls
			constexpr std::size_t kMaxNameBytes = 255; // as the file holds them
			constexpr float kNamePad = 3.0F;           // the name, in from its box

			const Vector4 kText = MakeVector4(0.86F, 0.87F, 0.90F, 1.0F);
			const Vector4 kGroupText = MakeVector4(0.66F, 0.69F, 0.75F, 1.0F);
			const Vector4 kHoverText = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			// As the view outlines the selection.
			const Vector4 kActive = EditorTheme::Active();
			const Vector4 kSelected = EditorTheme::Selected();
			const Vector4 kShadow = EditorTheme::TextShadow();
			const Vector4 kIcon = MakeVector4(0.80F, 0.82F, 0.86F, 1.0F);
			const Vector4 kDrop = EditorTheme::DropTarget();
			const Vector4 kRefused = EditorTheme::Refused();
			const Vector4 kBranch = MakeVector4(0.58F, 0.61F, 0.68F, 0.55F);
			const float kDimmed = 0.42F; // alpha of what is hidden

			Vector4 Faded(Vector4 c, float alpha) {
				c.w *= alpha;
				return c;
			}

			NodePath ParentOf(const NodePath& path) {
				return NodePath(path.begin(), path.end() - 1);
			}

			// A node's path as text, telling rows apart to a double click.
			std::string PathKey(const NodePath& path) {
				std::string key;
				for (int i : path)
					key += std::to_string(i) + "/";
				return key;
			}

			// Without the spaces at either end, which nobody means to type.
			std::string Trimmed(const std::string& text) {
				const std::size_t first = text.find_first_not_of(' ');
				if (first == std::string::npos)
					return std::string();
				return text.substr(first, text.find_last_not_of(' ') - first + 1);
			}
		} // namespace

		SceneTreePanel::SceneTreePanel(client::IRenderer& renderer, client::IFont& font)
		    : renderer(renderer), font(font),
		      nameField(renderer, font, kTextScale, kMaxNameBytes) {}

		void SceneTreePanel::Layout(float l, float t, float b) {
			left = l;
			top = t;
			bottom = std::max(t + kRowH, b);
		}

		void SceneTreePanel::Reset() {
			scroll = 0;
			held = Held::None;
			dragged.clear();
			anchor.clear();
			anchorShape = -1;
			rowClicks.Reset();
			CancelRename();
		}

		// --- Rows --------------------------------------------------------------

		std::vector<SceneTreePanel::Row> SceneTreePanel::Rows(const KV6Scene& scene) const {
			std::vector<Row> rows;
			rows.push_back(
			  Row{NodePath(), nullptr, 0, !scene.Roots().empty(), false, false, true, {}});
			NodePath path;
			std::vector<bool> branchesOn;
			std::function<void(const VoxelObject&, int, bool, bool)> visit =
			  [&](const VoxelObject& o, int depth, bool hiddenAbove, bool last) {
				  const bool folded = scene.IsFolded(path);
				  rows.push_back(Row{path, &o, depth, !o.children.empty(), folded, hiddenAbove, last,
				                     branchesOn});
				  if (folded)
					  return;
				  branchesOn.push_back(!last);
				  for (std::size_t i = 0; i < o.children.size(); i++) {
					  path.push_back(int(i));
					  visit(o.children[i], depth + 1, hiddenAbove || o.hidden,
					        i + 1 == o.children.size());
					  path.pop_back();
				  }
				  branchesOn.pop_back();
			  };
			const std::vector<VoxelObject>& roots = scene.Roots();
			for (std::size_t i = 0; i < roots.size(); i++) {
				path.assign(1, int(i));
				visit(roots[i], 1, false, i + 1 == roots.size());
			}
			return rows;
		}

		int SceneTreePanel::VisibleRows() const {
			return std::max(1, int(std::floor((bottom - top) / kRowH)));
		}

		int SceneTreePanel::ClampedScroll(int rowCount) const {
			return std::max(0, std::min(scroll, rowCount - VisibleRows()));
		}

		float SceneTreePanel::RowTop(int row) const { return top + float(row - scroll) * kRowH; }

		float SceneTreePanel::LabelX(int depth) const {
			return left + float(depth) * kIndent + kFoldW + 2.0F;
		}

		float SceneTreePanel::BranchX(int depth) const {
			return LabelX(depth - 1) - kFoldW * 0.5F - 2.0F;
		}

		int SceneTreePanel::RowAt(const std::vector<Row>& rows, const Vector2& at) const {
			if (at.x < left - kSlack || at.x >= left + kWidth || at.y < top)
				return -1;
			const int shown = int(std::floor((at.y - top) / kRowH));
			const int row = shown + scroll;
			if (shown >= VisibleRows() || row >= int(rows.size()))
				return -1;
			return row;
		}

		SceneTreePanel::Part SceneTreePanel::PartAt(const Row& row, const Vector2& at) const {
			if (row.node && at.x >= left + kWidth - kEyeW)
				return Part::Eye;
			const float foldX = LabelX(row.depth) - kFoldW - 2.0F;
			if (row.node && row.hasChildren && at.x >= foldX - 2.0F && at.x < foldX + kFoldW)
				return Part::Fold;
			return Part::Label;
		}

		std::string SceneTreePanel::Label(const Row& row) const {
			if (!row.node)
				return sceneName;
			return row.node->name.empty() ? std::string("(unnamed)") : row.node->name;
		}

		std::string SceneTreePanel::Elide(const std::string& text, float width) const {
			if (font.Measure(text).x * kTextScale <= width)
				return text;
			std::string cut = text;
			while (!cut.empty()) {
				// Back to the start of the last UTF-8 character.
				std::size_t end = cut.size() - 1;
				while (end > 0 && (static_cast<unsigned char>(cut[end]) & 0xC0) == 0x80)
					end--;
				cut.erase(end);
				if (font.Measure(cut + "...").x * kTextScale <= width)
					return cut + "...";
			}
			return std::string();
		}

		// --- Input -------------------------------------------------------------

		bool SceneTreePanel::IsOver(const KV6Scene& scene, const Vector2& at) const {
			return RowAt(Rows(scene), at) >= 0;
		}

		std::string SceneTreePanel::Press(KV6Scene& scene, const Vector2& at, bool toggle,
		                                  bool extend, float time) {
			const std::vector<Row> rows = Rows(scene);
			held = Held::Swallowed;
			// On the name being typed, the press is the field's: a drag from it
			// selects.
			if (IsOverRename(scene, at)) {
				nameField.Press(at, extend, time);
				held = Held::Text;
				return std::string();
			}

			const int index = RowAt(rows, at);
			if (index < 0)
				return std::string();
			const Row& row = rows[std::size_t(index)];
			switch (PartAt(row, at)) {
				case Part::Fold: scene.SetFolded(row.path, !row.folded); return std::string();
				case Part::Eye:
					scene.SetHidden(row.path, !row.node->hidden);
					return std::string();
				case Part::Label: break;
			}
			if (!row.node) {
				// The scene's own row: no node, but its pivot to set.
				if (!toggle && !extend)
					scene.SelectScene();
				return std::string();
			}

			// A second plain click on the same name types a new one over it.
			if (!toggle && !extend) {
				if (rowClicks.Click(at, time, PathKey(row.path))) {
					scene.Select({row.path}, row.path);
					std::string why;
					if (!BeginRename(scene, row.path, time, why))
						return "Cannot rename: " + why;
					return std::string();
				}
			}

			const NodePath& path = row.path;
			std::vector<NodePath> selection = scene.Selection();
			const bool selected = scene.IsSelected(path);
			// Shift reaches from the row last clicked (or else the active one),
			// as the rows stand: with the tree's shape changed since, it starts
			// over from the active row.
			const NodePath from =
			  anchorShape == scene.ShapeVersion() && !anchor.empty() ? anchor : scene.Active();

			if (extend) {
				int a = -1, b = index;
				for (std::size_t i = 1; i < rows.size(); i++) {
					if (rows[i].path == from)
						a = int(i);
				}
				if (a < 0)
					a = index;
				if (!toggle)
					selection.clear();
				for (int i = std::min(a, b); i <= std::max(a, b); i++) {
					if (std::find(selection.begin(), selection.end(), rows[std::size_t(i)].path) ==
					    selection.end())
						selection.push_back(rows[std::size_t(i)].path);
				}
				scene.Select(selection, path);
				anchor = from.empty() ? path : from;
			} else if (toggle) {
				if (selected) {
					selection.erase(std::find(selection.begin(), selection.end(), path));
					scene.Select(selection, scene.Active() == path ? NodePath() : scene.Active());
				} else {
					selection.push_back(path);
					scene.Select(selection, path);
				}
				anchor = path;
			} else {
				// A selected row stays among the others until the press turns
				// out to be a click, so they can all be dragged together.
				if (selected) {
					scene.Select(selection, path);
					selectOnRelease = true;
				} else {
					scene.Select({path}, path);
					selectOnRelease = false;
				}
				anchor = path;
			}
			anchorShape = scene.ShapeVersion();

			if (!scene.IsSelected(path))
				return std::string(); // taken away from the selection: nothing to drag
			held = Held::Row;
			pressed = path;
			pressAt = at;
			pressShape = scene.ShapeVersion();
			return std::string();
		}

		void SceneTreePanel::Move(const KV6Scene& scene, const Vector2& at) {
			if (held == Held::Text) {
				nameField.Drag(at);
				return;
			}
			if (held != Held::Row)
				return;
			const Vector2 d = at - pressAt;
			if (std::fabs(d.x) < kDragStart && std::fabs(d.y) < kDragStart)
				return;
			held = Held::Drag;
			selectOnRelease = false;
			dragged = scene.Selection();
		}

		std::string SceneTreePanel::Release(KV6Scene& scene, const Vector2& at) {
			const Held was = held;
			held = Held::None;
			if (was == Held::Text)
				return std::string();
			// The tree changed under the press (an undo): what it was about may
			// be somewhere else.
			if (pressShape != scene.ShapeVersion())
				return std::string();
			if (was == Held::Row && selectOnRelease) {
				scene.Select({pressed}, pressed);
				return std::string();
			}
			if (was != Held::Drag)
				return std::string();

			const Drop drop = DropAt(scene, Rows(scene), at);
			if (!drop.any)
				return std::string();
			if (!drop.why.empty())
				return "Cannot move there: " + drop.why;
			std::string why;
			if (!scene.Move(dragged, drop.parent, drop.position, why))
				return why.empty() ? std::string() : "Cannot move there: " + why;
			// What it went into opens, to show it there.
			const NodePath& moved = scene.Selection().front();
			for (std::size_t n = 1; n < moved.size(); n++)
				scene.SetFolded(NodePath(moved.begin(), moved.begin() + long(n)), false);
			anchor = scene.Active();
			anchorShape = scene.ShapeVersion();
			const std::size_t count = scene.Selection().size();
			return count == 1 ? std::string("Moved 1 object")
			                  : "Moved " + std::to_string(count) + " objects";
		}

		bool SceneTreePanel::ContextRow(KV6Scene& scene, const Vector2& at, NodePath& row) {
			const std::vector<Row> rows = Rows(scene);
			const int index = RowAt(rows, at);
			if (index < 0)
				return false;
			row = rows[std::size_t(index)].path;
			if (!row.empty() && !scene.IsSelected(row)) {
				scene.Select({row}, row);
				anchor = row;
				anchorShape = scene.ShapeVersion();
			}
			return true;
		}

		bool SceneTreePanel::CancelDrag() {
			if (held != Held::Drag)
				return false;
			held = Held::Swallowed; // its release ends the press
			return true;
		}

		bool SceneTreePanel::Wheel(const KV6Scene& scene, const Vector2& at, float amount) {
			const std::vector<Row> rows = Rows(scene);
			if (RowAt(rows, at) < 0)
				return false;
			scroll = ClampedScroll(int(rows.size()));
			scroll += int(std::lround(amount * kWheelRows));
			scroll = ClampedScroll(int(rows.size()));
			return true;
		}

		void SceneTreePanel::Reveal(KV6Scene& scene, const NodePath& path) {
			for (std::size_t n = 1; n < path.size(); n++)
				scene.SetFolded(NodePath(path.begin(), path.begin() + long(n)), false);
			const std::vector<Row> rows = Rows(scene);
			for (std::size_t i = 0; i < rows.size(); i++) {
				if (rows[i].path != path)
					continue;
				const int row = int(i);
				if (row < scroll)
					scroll = row;
				else if (row >= scroll + VisibleRows())
					scroll = row - VisibleRows() + 1;
				break;
			}
			scroll = ClampedScroll(int(rows.size()));
		}

		SceneTreePanel::Drop SceneTreePanel::DropAt(const KV6Scene& scene,
		                                            const std::vector<Row>& rows,
		                                            const Vector2& at) const {
			Drop drop;
			if (at.x < left - kSlack || at.x >= left + kWidth || at.y < top || at.y >= bottom)
				return drop;
			drop.any = true;
			const int index = RowAt(rows, at);
			if (index <= 0) {
				// The scene's row, or below the last: the end of the scene's top
				// objects.
				drop.parent.clear();
				drop.position = -1;
				if (index == 0) {
					drop.into = true;
					drop.row = 0;
				} else {
					drop.row = std::min(int(rows.size()), scroll + VisibleRows()) - 1;
					drop.lineY = RowTop(drop.row) + kRowH;
					drop.lineX = LabelX(1) - kFoldW;
				}
			} else {
				const Row& row = rows[std::size_t(index)];
				const float within = (at.y - RowTop(index)) / kRowH;
				drop.row = index;
				if (within < 0.28F) { // the gap above it
					drop.parent = ParentOf(row.path);
					drop.position = row.path.back();
					drop.lineY = RowTop(index);
					drop.lineX = LabelX(row.depth) - kFoldW;
				} else if (within > 0.72F) { // the gap below it
					if (row.hasChildren && !row.folded) {
						// Below an open parent is above its first child.
						drop.parent = row.path;
						drop.position = 0;
						drop.lineX = LabelX(row.depth + 1) - kFoldW;
					} else {
						drop.parent = ParentOf(row.path);
						drop.position = row.path.back() + 1;
						drop.lineX = LabelX(row.depth) - kFoldW;
					}
					drop.lineY = RowTop(index) + kRowH;
				} else {
					drop.parent = row.path;
					drop.position = -1;
					drop.into = true;
				}
			}
			drop.why = scene.WhyNotMove(dragged, drop.parent, drop.position);
			return drop;
		}

		// --- Drawing -----------------------------------------------------------

		void SceneTreePanel::DrawBranches(const Row& row, float y) {
			if (row.depth < 1)
				return;
			// Each node hangs from its parent's branch, which goes on down past
			// it to the parent's next children; the branches of the nodes above
			// pass by on its left.
			const float midY = y + kRowH * 0.5F;
			std::vector<std::pair<Vector2, Vector2>> lines;
			for (int a = 1; a < row.depth; a++) {
				if (row.branchesOn[std::size_t(a - 1)])
					lines.emplace_back(MakeVector2(BranchX(a), y), MakeVector2(BranchX(a), y + kRowH));
			}
			const float x = BranchX(row.depth);
			lines.emplace_back(MakeVector2(x, y), MakeVector2(x, row.last ? midY : y + kRowH));
			const float tickEnd = row.hasChildren
			                        ? LabelX(row.depth) - kFoldW - 3.0F
			                        : LabelX(row.depth) - 4.0F;
			lines.emplace_back(MakeVector2(x, midY), MakeVector2(tickEnd, midY));
			// No panel behind: a dark line under each, wider than it, keeps it
			// apart from the grid and the model.
			for (const auto& l : lines)
				OverlayStrokeLine(renderer, l.first, l.second, 3.0F, Faded(kShadow, 0.7F));
			for (const auto& l : lines)
				OverlayStrokeLine(renderer, l.first, l.second, 1.0F, kBranch);
		}

		void SceneTreePanel::DrawText(const std::string& text, float x, float y,
		                              const Vector4& color) {
			const float h = font.Measure("Ag").y * kTextScale;
			const Vector2 at = MakeVector2(x, y + (kRowH - h) * 0.5F);
			// No panel behind: a dark outline all round reads over the grid and
			// the model alike, where a shadow below loses itself in either.
			const Vector4 outline = Faded(kShadow, color.w * 0.9F);
			for (const Vector2& d : {MakeVector2(-1, 0), MakeVector2(1, 0), MakeVector2(0, -1),
			                         MakeVector2(0, 1), MakeVector2(-1, -1), MakeVector2(1, -1),
			                         MakeVector2(-1, 1), MakeVector2(1, 1)})
				font.Draw(text, at + d, kTextScale, outline);
			font.Draw(text, at, kTextScale, color);
		}

		void SceneTreePanel::DrawEye(const Vector2& c, bool open, const Vector4& color) {
			constexpr float hw = 6.5F, hh = 3.9F;
			constexpr int kSteps = 10;
			const Vector4 shadow = Faded(kShadow, color.w * 0.8F);
			std::vector<Vector2> lid;
			if (open) {
				for (int i = 0; i <= kSteps; i++) {
					const float t = float(i) / float(kSteps);
					lid.push_back(MakeVector2(c.x + (2.0F * t - 1.0F) * hw,
					                          c.y - hh * std::sin(t * float(M_PI))));
				}
				for (int i = kSteps - 1; i > 0; i--) {
					const float t = float(i) / float(kSteps);
					lid.push_back(MakeVector2(c.x + (2.0F * t - 1.0F) * hw,
					                          c.y + hh * std::sin(t * float(M_PI))));
				}
				std::vector<Vector2> under = lid;
				for (Vector2& p : under)
					p += MakeVector2(0.0F, 1.0F);
				OverlayStrokePolyline(renderer, under.data(), under.size(), 1.3F, shadow, true);
				OverlayStrokePolyline(renderer, lid.data(), lid.size(), 1.3F, color, true);
				OverlayFillCircle(renderer, c + MakeVector2(0.0F, 1.0F), 2.1F, shadow);
				OverlayFillCircle(renderer, c, 2.1F, color);
				return;
			}
			// Shut: the lower lid, and three lashes under it.
			for (int i = 0; i <= kSteps; i++) {
				const float t = float(i) / float(kSteps);
				lid.push_back(MakeVector2(c.x + (2.0F * t - 1.0F) * hw,
				                          c.y + hh * 0.55F * std::sin(t * float(M_PI))));
			}
			std::vector<Vector2> under = lid;
			for (Vector2& p : under)
				p += MakeVector2(0.0F, 1.0F);
			OverlayStrokePolyline(renderer, under.data(), under.size(), 1.3F, shadow, false);
			OverlayStrokePolyline(renderer, lid.data(), lid.size(), 1.3F, color, false);
			for (float t : {0.25F, 0.5F, 0.75F}) {
				const float x = c.x + (2.0F * t - 1.0F) * hw;
				const float y = c.y + hh * 0.55F * std::sin(t * float(M_PI));
				const float lean = (t - 0.5F) * 3.0F;
				OverlayStrokeLine(renderer, MakeVector2(x, y), MakeVector2(x + lean, y + 2.6F), 1.1F,
				                  color);
			}
		}

		void SceneTreePanel::DrawLock(const Vector2& c, const Vector4& color) {
			std::vector<Vector2> shackle;
			for (int i = 0; i <= 8; i++) {
				const float a = float(M_PI) * float(i) / 8.0F;
				shackle.push_back(MakeVector2(c.x + 2.6F * std::cos(a), c.y - 1.0F - 2.8F * std::sin(a)));
			}
			OverlayStrokePolyline(renderer, shackle.data(), shackle.size(), 1.3F, color, false);
			OverlayColorNP(renderer, color);
			OverlayFillRect(renderer, c.x - 4.0F, c.y - 1.0F, 8.0F, 6.0F);
		}

		void SceneTreePanel::DrawFold(const Vector2& c, bool folded, const Vector4& color) {
			Vector2 p[3];
			if (folded) {
				p[0] = MakeVector2(c.x - 2.0F, c.y - 4.0F);
				p[1] = MakeVector2(c.x + 3.0F, c.y);
				p[2] = MakeVector2(c.x - 2.0F, c.y + 4.0F);
			} else {
				p[0] = MakeVector2(c.x - 4.0F, c.y - 2.0F);
				p[1] = MakeVector2(c.x + 4.0F, c.y - 2.0F);
				p[2] = MakeVector2(c.x, c.y + 3.0F);
			}
			Vector2 s[3];
			for (int i = 0; i < 3; i++)
				s[i] = p[i] + MakeVector2(0.0F, 1.0F);
			OverlayFillConvexPolygon(renderer, s, 3, Faded(kShadow, color.w * 0.8F));
			OverlayFillConvexPolygon(renderer, p, 3, color);
		}

		void SceneTreePanel::Draw(const KV6Scene& scene, const Vector2& cursor, float time) {
			// The name being typed goes with its node: should the node take
			// another path, or fold out of sight, the typing ends.
			if (IsRenaming() && (renameShape != scene.ShapeVersion() || !scene.Node(renamed)))
				CancelRename();
			// An undo or a redo during a drag changes what it was dragging.
			if (held != Held::None && pressShape != scene.ShapeVersion())
				held = Held::Swallowed;

			const std::vector<Row> rows = Rows(scene);
			if (IsRenaming() && RowOf(rows, renamed) < 0)
				CancelRename();
			scroll = ClampedScroll(int(rows.size()));
			const int hover = held == Held::None ? RowAt(rows, cursor) : -1;
			const Drop drop =
			  held == Held::Drag ? DropAt(scene, rows, cursor) : Drop();
			const int last = std::min(int(rows.size()), scroll + VisibleRows());

			for (int i = scroll; i < last; i++) {
				const Row& row = rows[std::size_t(i)];
				const float y = RowTop(i);
				const float midY = y + kRowH * 0.5F;
				const bool selected = row.node ? scene.IsSelected(row.path) : scene.SceneActive();
				const bool active = selected && scene.Active() == row.path;
				// Hidden, or stood in for by a preview (a model of it at another resolution).
				const bool hidden = row.node && (row.node->hidden || row.hiddenAbove ||
				                                 scene.PreviewHides(row.path));
				const bool hovered = i == hover;

				Vector4 color = !row.node || row.node->model ? kText : kGroupText;
				if (selected)
					color = active ? kActive : kSelected;
				else if (hovered)
					color = kHoverText;
				if (hidden)
					color = Faded(color, kDimmed);

				// A band across the row under the pointer, and the selected
				// ones, with a bar beside them.
				if (selected || hovered) {
					OverlayColorNP(renderer, selected ? Faded(color, active ? 0.22F : 0.14F)
					                                  : EditorTheme::Highlight());
					OverlayFillRect(renderer, left - kSlack + 1.0F, y + 1.0F, kWidth + kSlack - 1.0F,
					                kRowH - 2.0F);
				}
				if (selected) {
					OverlayColorNP(renderer, color);
					OverlayFillRect(renderer, left - kSlack + 1.0F, y + 3.0F, 2.0F, kRowH - 6.0F);
				}

				const float labelX = LabelX(row.depth);
				DrawBranches(row, y);
				if (row.hasChildren && row.node)
					DrawFold(MakeVector2(labelX - kFoldW * 0.5F - 2.0F, midY), row.folded,
					         Faded(hovered ? kHoverText : kIcon, hidden ? kDimmed : 1.0F));

				float labelEnd = left + kWidth - kEyeW - 4.0F;
				if (row.node && row.node->locked) {
					labelEnd -= kLockW;
					DrawLock(MakeVector2(left + kWidth - kEyeW - kLockW * 0.5F, midY),
					         Faded(kIcon, hidden ? kDimmed : 0.8F));
				}
				if (IsRenaming() && row.path == renamed) {
					nameField.SetRect(RenameBox(row, i));
					nameField.Draw(time, !scene.WhyNotRename(renamed, Trimmed(nameField.Text())).empty());
				}
				else
					DrawText(Elide(Label(row), labelEnd - labelX), labelX, y, color);

				if (row.node) {
					const bool overEye = hovered && PartAt(row, cursor) == Part::Eye;
					// The eye says what the node itself is set to; hidden by a node
					// above, it is dimmed.
					Vector4 eye = overEye ? kHoverText : kIcon;
					if (row.node->hidden || row.hiddenAbove)
						eye = Faded(eye, overEye ? 0.8F : kDimmed);
					DrawEye(MakeVector2(left + kWidth - kEyeW * 0.5F, midY), !row.node->hidden, eye);
				}
			}

			if (IsRenaming()) {
				const int index = RowOf(rows, renamed);
				if (index >= scroll && index < last)
					DrawRenameReason(scene);
			}

			if (held != Held::Drag)
				return;

			// Where the drop would land: a frame round the row it goes into, or a
			// line between two rows.
			if (drop.any && drop.row >= scroll && drop.row < last) {
				const Vector4 color = drop.why.empty() ? kDrop : kRefused;
				if (drop.into) {
					const Row& row = rows[std::size_t(drop.row)];
					const float x = LabelX(row.depth) - 3.0F;
					OverlayStrokeRect(renderer, x, RowTop(drop.row) + 1.0F, left + kWidth - x,
					                  kRowH - 2.0F, 1.5F, color);
				} else {
					OverlayFillCircle(renderer, MakeVector2(drop.lineX, drop.lineY), 3.0F, color);
					OverlayStrokeLine(renderer, MakeVector2(drop.lineX, drop.lineY),
					                  MakeVector2(left + kWidth, drop.lineY), 2.0F, color);
				}
			}

			// What is dragged, by the cursor, and why it cannot go where it is.
			std::string what = std::to_string(dragged.size()) + " objects";
			if (dragged.size() == 1) {
				const VoxelObject* o = scene.Node(dragged.front());
				what = o && !o->name.empty() ? o->name : std::string("(unnamed)");
			}
			const Vector2 ghost = cursor + MakeVector2(16.0F, 2.0F);
			DrawText(what, ghost.x, ghost.y, Faded(kHoverText, 0.85F));
			if (drop.any && !drop.why.empty())
				DrawText(drop.why, ghost.x, ghost.y + kRowH - 3.0F, kRefused);
		}

		// --- Renaming ----------------------------------------------------------

		int SceneTreePanel::RowOf(const std::vector<Row>& rows, const NodePath& path) const {
			for (std::size_t i = 0; i < rows.size(); i++) {
				if (rows[i].node && rows[i].path == path)
					return int(i);
			}
			return -1;
		}

		AABB2 SceneTreePanel::RenameBox(const Row& row, int index) const {
			const float x = LabelX(row.depth) - kNamePad;
			return AABB2(x, RowTop(index) + 1.0F, left + kWidth - kEyeW - 4.0F - x, kRowH - 2.0F);
		}

		bool SceneTreePanel::BeginRename(KV6Scene& scene, const NodePath& path, float time,
		                                 std::string& why) {
			const VoxelObject* o = scene.Node(path);
			if (!o) {
				why = "there is nothing to rename";
				return false;
			}
			if (o->locked) {
				why = scene.WhyNotRename(path, o->name + "?");
				return false;
			}
			if (held == Held::Drag)
				CancelDrag();
			Reveal(scene, path);
			renamed = path;
			renameShape = scene.ShapeVersion();
			nameField.Begin(o->name, true, time);
			PlaceRename(scene);
			return true;
		}

		void SceneTreePanel::PlaceRename(const KV6Scene& scene) {
			const std::vector<Row> rows = Rows(scene);
			const int index = RowOf(rows, renamed);
			if (index >= 0)
				nameField.SetRect(RenameBox(rows[std::size_t(index)], index));
		}

		bool SceneTreePanel::IsOverRename(const KV6Scene& scene, const Vector2& at) const {
			if (!IsRenaming())
				return false;
			const int index = RowOf(Rows(scene), renamed);
			return index >= scroll && index < scroll + VisibleRows() && nameField.Contains(at);
		}

		bool SceneTreePanel::CommitRename(KV6Scene& scene, std::string& said) {
			said.clear();
			if (!IsRenaming())
				return true;
			const VoxelObject* o = scene.Node(renamed);
			const std::string typed = Trimmed(nameField.Text());
			if (!o || typed == o->name) {
				CancelRename();
				return true;
			}
			std::string why;
			if (!scene.Rename(renamed, typed, why)) {
				said = why;
				return false;
			}
			said = "Renamed to \"" + typed + "\"";
			CancelRename();
			return true;
		}

		void SceneTreePanel::CancelRename() {
			nameField.End();
			if (held == Held::Text)
				held = Held::Swallowed; // its release ends the press
		}

		std::string SceneTreePanel::RenameKey(KV6Scene& scene, const std::string& key,
		                                      const TextEntry::Modifiers& modifiers) {
			if (!IsRenaming())
				return std::string();
			if (key == "Enter" || key == "Keypad Enter" || key == "Tab") {
				std::string said;
				if (!CommitRename(scene, said))
					return "Cannot rename: " + said;
				return said;
			}
			if (key == "Escape") {
				CancelRename();
				return std::string();
			}
			nameField.Key(key, modifiers);
			return std::string();
		}

		void SceneTreePanel::RenameText(const std::string& text) {
			if (IsRenaming())
				nameField.Insert(text);
		}

		void SceneTreePanel::RenameComposition(const std::string& text) {
			if (IsRenaming())
				nameField.Compose(text);
		}

		AABB2 SceneTreePanel::RenameRect(const KV6Scene& scene) const {
			return IsRenaming() ? nameField.Rect() : AABB2();
		}

		void SceneTreePanel::DrawRenameReason(const KV6Scene& scene) {
			const std::string why = scene.WhyNotRename(renamed, Trimmed(nameField.Text()));
			if (why.empty())
				return;
			// Under the box, over the rows below it: drawn after them.
			const AABB2& box = nameField.Rect();
			const float x = box.GetMinX(), y = box.GetMaxY() + 2.0F;
			const float w = font.Measure(why).x * kTextScale + 10.0F;
			OverlayColorNP(renderer, EditorTheme::FieldFill());
			OverlayFillRect(renderer, x, y, w, kRowH - 2.0F);
			OverlayStrokeRect(renderer, x, y, w, kRowH - 2.0F, 1.0F,
			                  Faded(EditorTheme::Refused(), 0.6F));
			DrawText(why, x + 5.0F, y - 1.0F, EditorTheme::Refused());
		}
	} // namespace gui
} // namespace spades
