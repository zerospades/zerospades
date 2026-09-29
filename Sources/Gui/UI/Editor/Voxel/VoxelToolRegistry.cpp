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

#include "VoxelTool.h"

#include "DrawTool.h"
#include "MirrorTool.h"
#include "PaintTool.h"
#include "PivotTool.h"
#include "SelectTool.h"
#include "TransformTool.h"

namespace spades {
	namespace gui {
		namespace {
			// Tools that edit the voxels or the selection, then tools that set
			// the model up (its mirror planes, its pivot).
			constexpr int kEditingGroup = 0;
			constexpr int kSetupGroup = 1;

			template <class T> VoxelToolRegistry::Factory Make() {
				return [] { return std::unique_ptr<VoxelTool>(new T()); };
			}
		} // namespace

		VoxelToolRegistry& VoxelTools() {
			static VoxelToolRegistry registry;
			static bool seeded = false;
			if (!seeded) {
				seeded = true;
				// Built-in tools, in toolbar order: the everyday ones first, the
				// set-up ones last. Seeding here (rather than via static
				// initialisers in each tool's TU) keeps the order deterministic.
				// The keys lean on common editor conventions (Q select, B brush,
				// T transform) and keep clear of the default WASD movement keys.
				registry.Register(Make<SelectTool>(), "select", kEditingGroup, "Q");
				registry.Register(Make<DrawTool>(), "draw", kEditingGroup, "B");
				registry.Register(Make<PaintTool>(), "paint", kEditingGroup, "P");
				registry.Register(Make<TransformTool>(), "transform", kEditingGroup, "T");
				registry.Register(Make<MirrorTool>(), "mirror", kSetupGroup, "M");
				registry.Register(Make<PivotTool>(), "pivot", kSetupGroup, "O");
			}
			return registry;
		}
	} // namespace gui
} // namespace spades
