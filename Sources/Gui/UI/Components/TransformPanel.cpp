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

#include "TransformPanel.h"

#include <algorithm>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Gui/OverlayPaint.h>

#include "EditorTheme.h"
#include "NumberField.h"

namespace spades {
	namespace gui {
		namespace {
			constexpr float kWidth = 300.0F;
			constexpr float kPad = 10.0F;
			constexpr float kTitleH = 22.0F;
			constexpr float kRowH = 24.0F;
			constexpr float kRowGap = 4.0F;
			constexpr float kLabelW = 64.0F;
			constexpr float kFieldGap = 4.0F;
			constexpr float kTextScale = 0.85F;

			const char* const kRows[] = {"Location", "Rotation", "Scale"};
			const char* const kAxes[] = {"X", "Y", "Z"};
			// The axes in the colours the gizmo and the view's axes have.
			Vector4 AxisColor(int axis) {
				switch (axis) {
					case 0: return MakeVector4(1.0F, 0.42F, 0.42F, 1.0F);
					case 1: return MakeVector4(0.5F, 0.95F, 0.5F, 1.0F);
					default: return MakeVector4(0.5F, 0.68F, 1.0F, 1.0F);
				}
			}

			NumberField::Format FormatOf(int row) {
				NumberField::Format f;
				switch (row) {
					case 0: // voxels
						f.decimals = 2;
						f.perPixel = 0.25F;
						f.step = 1.0F;
						break;
					case 1: // degrees
						f.decimals = 1;
						f.suffix = "\xc2\xb0"; // the degree sign
						f.perPixel = 0.5F;
						f.step = 15.0F;
						break;
					default: // a factor
						f.decimals = 3;
						f.perPixel = 0.005F;
						f.step = 0.1F;
						f.minimum = 0.01F;
						break;
				}
				return f;
			}

			float& Component(ITransformTarget::Transform& t, int index) {
				Vector3& v = index < 3 ? t.position : index < 6 ? t.rotation : t.scale;
				switch (index % 3) {
					case 0: return v.x;
					case 1: return v.y;
					default: return v.z;
				}
			}
		} // namespace

		TransformPanel::TransformPanel(client::IRenderer& renderer, client::IFont& font)
		    : renderer(renderer), font(font) {
			for (int i = 0; i < 9; i++) {
				fields.emplace_back(new NumberField(renderer, font, kTextScale));
				fields.back()->SetLabel(kAxes[i % 3], AxisColor(i % 3));
				fields.back()->SetFormat(FormatOf(i / 3));
			}
		}

		TransformPanel::~TransformPanel() {}

		float TransformPanel::Left() const { return right - kWidth; }

		int TransformPanel::RowCount() const {
			int n = 0;
			for (int row = 0; row < 3; row++)
				n += (rows >> row) & 1;
			return n;
		}

		int TransformPanel::Slot(int row) const {
			if (!((rows >> row) & 1))
				return -1;
			int slot = 0;
			for (int r = 0; r < row; r++)
				slot += (rows >> r) & 1;
			return slot;
		}

		float TransformPanel::Height() const {
			const int n = RowCount();
			return kPad * 2.0F + kTitleH + float(n) * kRowH + float(std::max(0, n - 1)) * kRowGap;
		}

		void TransformPanel::Layout(float r, float t) {
			right = r;
			top = t;
			Place();
		}

		void TransformPanel::Place() {
			const float fieldW = (kWidth - kPad * 2.0F - kLabelW - kFieldGap * 2.0F) / 3.0F;
			for (int i = 0; i < 9; i++) {
				const int slot = std::max(0, Slot(i / 3)), axis = i % 3;
				const float x = Left() + kPad + kLabelW + float(axis) * (fieldW + kFieldGap);
				const float y = top + kPad + kTitleH + float(slot) * (kRowH + kRowGap);
				fields[std::size_t(i)]->SetRect(AABB2(x, y, fieldW, kRowH));
			}
		}

		bool TransformPanel::Contains(const Vector2& at) const {
			return shown && OverlayInRect(at, Left(), top, kWidth, Height());
		}

		bool TransformPanel::IsHolding() const {
			return active >= 0 && fields[std::size_t(active)]->IsHeld();
		}

		bool TransformPanel::IsTyping() const {
			return active >= 0 && fields[std::size_t(active)]->IsTyping();
		}

		bool TransformPanel::Begin(ITransformTarget& target, int index) {
			std::string name;
			bool onlyMoves = false;
			if (!target.CurrentTransform(editing, name, onlyMoves))
				return false;
			if ((onlyMoves && index >= 3) || Slot(index / 3) < 0)
				return false;
			active = index;
			fields[std::size_t(index)]->SetValue(Component(editing, index));
			return true;
		}

		void TransformPanel::Put(int index, float value) { Component(editing, index) = value; }

		void TransformPanel::Press(ITransformTarget& target, const Vector2& at) {
			for (int i = 0; i < 9; i++) {
				NumberField& field = *fields[std::size_t(i)];
				if (!field.Contains(at) || !field.IsEnabled())
					continue;
				if (Begin(target, i))
					field.Press(at);
				return;
			}
		}

		void TransformPanel::Act(ITransformTarget& target, int result, float time) {
			if (active < 0)
				return;
			NumberField& field = *fields[std::size_t(active)];
			switch (NumberField::Result(result)) {
				case NumberField::Result::None: return;
				case NumberField::Result::Preview:
					Put(active, field.Value());
					target.PreviewTransform(editing);
					return;
				case NumberField::Result::Cancel:
					target.CancelTransform();
					active = -1;
					return;
				case NumberField::Result::Commit:
				case NumberField::Result::Next:
				case NumberField::Result::Previous: {
					Put(active, field.Value());
					target.PreviewTransform(editing);
					target.CommitTransform();
					const int from = active;
					active = -1;
					if (NumberField::Result(result) == NumberField::Result::Commit)
						return;
					// On to the next field that can be set, as Tab goes in a form.
					const int direction = NumberField::Result(result) == NumberField::Result::Next ? 1 : -1;
					for (int n = 1; n < 9; n++) {
						const int next = ((from + direction * n) % 9 + 9) % 9;
						if (fields[std::size_t(next)]->IsEnabled() && Begin(target, next)) {
							fields[std::size_t(next)]->BeginTyping(time);
							return;
						}
					}
					return;
				}
			}
		}

		void TransformPanel::Drag(ITransformTarget& target, float dx, bool fine, bool inSteps) {
			if (IsHolding())
				Act(target, int(fields[std::size_t(active)]->Drag(dx, fine, inSteps)), 0.0F);
		}

		void TransformPanel::Release(ITransformTarget& target, float time) {
			if (IsHolding())
				Act(target, int(fields[std::size_t(active)]->Release(time)), time);
		}

		bool TransformPanel::CancelScrub(ITransformTarget& target) {
			if (!IsHolding())
				return false;
			Act(target, int(fields[std::size_t(active)]->CancelScrub()), 0.0F);
			return true;
		}

		void TransformPanel::Key(ITransformTarget& target, const std::string& key,
		                         const TextEntry::Modifiers& modifiers, float time) {
			if (IsTyping())
				Act(target, int(fields[std::size_t(active)]->Key(key, modifiers)), time);
		}

		void TransformPanel::Insert(const std::string& text) {
			if (IsTyping())
				fields[std::size_t(active)]->Insert(text);
		}

		void TransformPanel::Compose(const std::string& text) {
			if (IsTyping())
				fields[std::size_t(active)]->Compose(text);
		}

		AABB2 TransformPanel::TextRect() const {
			return IsTyping() ? fields[std::size_t(active)]->TextRect() : AABB2();
		}

		void TransformPanel::Settle(ITransformTarget& target) {
			if (IsTyping())
				Act(target, int(fields[std::size_t(active)]->Settle()), 0.0F);
			else if (IsHolding())
				CancelScrub(target);
		}

		void TransformPanel::Draw(ITransformTarget& target, const Vector2& cursor, float time) {
			ITransformTarget::Transform now;
			std::string name;
			bool onlyMoves = false;
			shown = target.CurrentTransform(now, name, onlyMoves);
			if (rows != target.ShownRows()) {
				rows = target.ShownRows();
				Place();
			}
			if (!shown) {
				if (active >= 0)
					Settle(target); // what it was setting is gone
				return;
			}

			const float x = Left(), h = Height();
			EditorTheme::PaintPanel(renderer, x, top, kWidth, h);
			auto text = [&](const std::string& s, float tx, float ty, float scale, const Vector4& c) {
				font.DrawShadow(s, MakeVector2(tx, ty), scale, c, EditorTheme::TextShadow());
			};
			const std::string title = target.Title();
			text(title, x + kPad, top + kPad, 0.95F, EditorTheme::Text());
			const float titleW = font.Measure(title + "  ").x * 0.95F;
			std::string shownName = name;
			while (!shownName.empty() &&
			       font.Measure(shownName).x * 0.85F > kWidth - kPad * 2.0F - titleW)
				shownName.pop_back();
			text(shownName == name ? name : shownName + "...", x + kPad + titleW, top + kPad + 1.0F,
			     0.85F, EditorTheme::TextDim());

			const float labelH = font.Measure("Ag").y * kTextScale;
			for (int row = 0; row < 3; row++) {
				const int slot = Slot(row);
				if (slot < 0) {
					for (int axis = 0; axis < 3; axis++)
						fields[std::size_t(row * 3 + axis)]->SetEnabled(false); // Tab passes it by
					continue;
				}
				const float y = top + kPad + kTitleH + float(slot) * (kRowH + kRowGap);
				const bool settable = row == 0 || !onlyMoves;
				text(kRows[row], x + kPad, y + (kRowH - labelH) * 0.5F, kTextScale,
				     settable ? EditorTheme::TextDim() : EditorTheme::TextFaint());
				for (int axis = 0; axis < 3; axis++) {
					const int i = row * 3 + axis;
					NumberField& field = *fields[std::size_t(i)];
					field.SetEnabled(settable);
					if (i != active)
						field.SetValue(Component(now, i));
					field.Draw(time, field.Contains(cursor));
				}
			}
		}
	} // namespace gui
} // namespace spades
