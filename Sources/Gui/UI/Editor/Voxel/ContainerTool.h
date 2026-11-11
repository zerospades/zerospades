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

#include <memory>
#include <string>
#include <vector>

#include "VoxelTool.h"
#include "SubTool.h"

namespace spades {
	namespace gui {
		/**
		 * A tool that is just a set of child tools (shown in the secondary
		 * toolbar). All input and drawing forwards to the active child, so
		 * concrete tools (Draw, Select) only have to populate `subs` and provide
		 * a Label.
		 *
		 * Options work the same way. The bar shows the container's own, which
		 * hold whichever child is active (Select's commands), and then the
		 * active child's, which are that child's alone (a brush's size, a
		 * transform's boxes) and go when it does. A child declares what it
		 * needs, answers for it, and nothing has to be hidden and shown again
		 * as the children change.
		 */
		class ContainerTool : public VoxelTool {
		public:
			int SubToolCount() const override { return int(subs.size()); }
			const char* SubToolLabel(int i) const override { return subs[i]->Label(); }
			const char* SubToolGroup(int i) const override {
				return i >= 0 && i < int(subGroups.size()) ? subGroups[i].c_str() : "";
			}
			VoxelTool* SubTool(int i) override {
				return (i >= 0 && i < int(subs.size())) ? subs[i].get() : nullptr;
			}
			int ActiveSubTool() const override { return active; }
			void SetSubTool(IVoxelEditContext&, int) override;

			void OnActivate(IVoxelEditContext&) override;
			void OnDeactivate(IVoxelEditContext&) override;
			void OnPointer(IVoxelEditContext&, const PointerInput&) override;
			void OnKey(IVoxelEditContext&, const KeyInput&) override;
			std::string EscapeLabel(IVoxelEditContext&) override;
			void OnEscape(IVoxelEditContext&) override;
			void CancelInteraction(IVoxelEditContext&) override;
			void OnDocumentChanged(IVoxelEditContext&) override;
			std::string Hint(IVoxelEditContext&) override;
			void DrawScene(IVoxelEditContext&) override;
			void DrawOverlay(IVoxelEditContext&) override;
			// The container's own options and the active sub-tool's, as one list
			// for the bar to draw. Rebuilt on the way out, after everything that
			// updates an option has run.
			ToolOptions* Options() override;
			// Subclasses overriding these call them first; each hands what it
			// does not answer for to the active sub-tool.
			void UpdateOptions(IVoxelEditContext& ed) override;
			void OnAction(IVoxelEditContext& ed, const std::string& id) override;
			void OnOptionToggled(IVoxelEditContext& ed, const std::string& id, bool value) override;
			void OnOptionNumberChanged(IVoxelEditContext& ed, const std::string& id, float value,
			                           bool committed) override;

			ITransformTarget* TransformTarget(IVoxelEditContext& ed) override {
				VoxelTool* t = Cur();
				return t ? t->TransformTarget(ed) : nullptr;
			}
			// The target comes from the sub-tool, so the step it is set in does
			// too: the two are one answer, and a container that passed on only
			// the first would have the panel setting it in some other step.
			float TransformStep() const override {
				const VoxelTool* t = Cur();
				return t ? t->TransformStep() : VoxelTool::TransformStep();
			}

		protected:
			std::vector<std::unique_ptr<VoxelTool>> subs;
			int active = 0;
			VoxelTool* Cur();
			const VoxelTool* Cur() const;

			/**
			 * Adds a sub-tool, in `group` where its container arranges its bar
			 * in groups. The group belongs to the container, not to the
			 * sub-tool: the same Box serves Draw, Paint and Select, which each
			 * lay their bar out their own way.
			 */
			void AddSub(std::unique_ptr<VoxelTool> sub, const char* group = "") {
				subGroups.resize(subs.size());
				subGroups.push_back(group);
				subs.push_back(std::move(sub));
			}

			/** The container's own options: what holds across its sub-tools. */
			ToolOptions options;
			/** Whether `id` is one of the container's own rather than a sub-tool's. */
			bool Owns(const std::string& id) { return options.Find(id) != nullptr; }

		private:
			std::vector<std::string> subGroups; // AddSub's, by sub-tool
			// `options` followed by the active sub-tool's, rebuilt by Options().
			ToolOptions shown;
		};
	} // namespace gui
} // namespace spades
