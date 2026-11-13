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
// VoxelEditor: what a model is set up with: its pivot, the reference images, the mirror planes.

#include "VoxelEditorInternal.h"

namespace spades {
	namespace gui {
		// --- Pivot ------------------------------------------------------------

		Vector3 VoxelEditor::GetPivot() const { return document.Origin() * -1.0F; }

		void VoxelEditor::SetPivot(const Vector3& pivot) {
			DocumentCommand command(*this);
			Vector3 before = document.Origin();
			Vector3 after = pivot * -1.0F;
			if (after.x == before.x && after.y == before.y && after.z == before.z)
				return;
			VoxelUndoStack::Step step(undo, "Set Pivot");
			document.SetOrigin(after);
			undo.RecordOrigin(before, after);
		}

		// Live, non-journaled pivot move for a drag in progress; the tool commits the
		// net change with one SetPivot on release.
		void VoxelEditor::PreviewPivot(const Vector3& pivot) {
			if (!previewedOrigin)
				previewedOrigin = document.Origin();
			document.SetOrigin(pivot * -1.0F);
		}

		// --- Reference images -------------------------------------------------

		namespace {
			const char* const kAxisName[3] = {"X", "Y", "Z"};
		} // namespace

		ReferenceImage& VoxelEditor::Reference(int axis) {
			return references[Clamp(axis, 0, 2)];
		}

		void VoxelEditor::LoadReferenceImage(int axis) {
			axis = Clamp(axis, 0, 2);
			// The browser takes the input over while it is up.
			ReleaseHeldInput();
			host.PickImageFile(std::string("Reference Image Across ") + kAxisName[axis],
			                   [this, axis](const std::string& path) { LoadReferenceFile(axis, path); });
		}

		bool VoxelEditor::LoadReferenceFile(int axis, const std::string& path) {
			Handle<client::IImage> image;
			int width = 0, height = 0;
			try {
				std::FILE* f = LocalFileSystem::OpenFile(path, "rb");
				if (!f)
					SPRaise("cannot open the file");
				StdStream stream(f, true); // takes ownership of the FILE*
				Handle<Bitmap> bitmap = Bitmap::Load(stream);
				width = bitmap->GetWidth();
				height = bitmap->GetHeight();
				// Its alpha is kept: what is transparent in it shows the document
				// and the rest of the image planes through it.
				image = renderer->CreateImage(*bitmap);
			} catch (const std::exception& ex) {
				SPLog("Could not load reference image %s: %s", path.c_str(), ex.what());
			}
			if (!image || width <= 0 || height <= 0) {
				SetStatus("Could not load " + LocalFileSystem::GetFileName(path));
				return false;
			}

			ReferenceImage& ref = references[axis];
			const bool first = !ref.Loaded();
			ref.path = path;
			ref.image = image;
			ref.pixelWidth = width;
			ref.pixelHeight = height;
			ref.visible = true;
			// A first image is fitted to the document: as wide as its volume
			// across the image, its middle on the volume's, and just behind the
			// volume, where it never cuts through the voxels being modelled. A
			// replacement keeps how the one before it was set up.
			if (first) {
				int u, v;
				ReferenceAxes(axis, u, v);
				const IntVector3 size = document.Size();
				const float extent[3] = {float(size.x), float(size.y), float(size.z)};
				ref.size = std::max(1.0F, std::max(extent[u], extent[v]));
				Vector3& centre = ref.centre;
				centre[u] = extent[u] * 0.5F - 0.5F;
				centre[v] = extent[v] * 0.5F - 0.5F;
				// Behind the voxels as each view is usually looked from: the side
				// from +X, the front from -Y, and the top from above (Z points
				// down, so above is the low end of Z).
				centre[axis] = axis == 0 ? -1.0F : axis == 1 ? extent[1] : extent[2];
				ref.rotation = MakeVector4(0.0F, 0.0F, 0.0F, 1.0F);
			}
			SetStatus(std::string("Reference image across ") + kAxisName[axis] + ": " +
			          LocalFileSystem::GetFileName(path));
			return true;
		}

		void VoxelEditor::ClearReferenceImage(int axis) {
			ReferenceImage& ref = references[Clamp(axis, 0, 2)];
			if (!ref.Loaded())
				return;
			const ReferenceImage cleared;
			// The placement stays, so loading another image puts it where this was.
			ReferenceImage kept = cleared;
			kept.size = ref.size;
			kept.centre = ref.centre;
			kept.rotation = ref.rotation;
			kept.opacity = ref.opacity;
			kept.flipped = ref.flipped;
			ref = kept;
			SetStatus(std::string("Reference image across ") + kAxisName[axis] + " removed");
		}

		void VoxelEditor::DrawReferenceImages() {
			for (int axis = 0; axis < 3; axis++) {
				const ReferenceImage& ref = references[axis];
				if (!ref.Loaded() || !ref.visible || ref.opacity <= 0.0F)
					continue;
				Vector3 frame[3];
				ReferenceFrame(axis, ref.rotation, frame);
				const Vector3 alongU = frame[0] * (ref.flipped ? -ref.size : ref.size);
				const Vector3 alongV = frame[1] * ref.Height();
				const Vector3 origin =
				  ref.centre - alongU * 0.5F - alongV * 0.5F + host.DocumentOrigin();
				const float o = Clamp(ref.opacity, 0.0F, 1.0F);
				renderer->SetColorAlphaPremultiplied(MakeVector4(o, o, o, o));
				renderer->AddImagePlane(*ref.image, origin, alongU, alongV);
			}
		}

		// --- Mirror -----------------------------------------------------------

		bool VoxelEditor::MirrorEnabled(int axis) const {
			if (axis < 0 || axis > 2)
				return false;
			return edit.mirror.enabled[axis];
		}

		// The mirror setup is journaled like the selection (each undo step
		// restores it), so changing it is a command like any edit.
		void VoxelEditor::SetMirrorEnabled(int axis, bool on) {
			if (axis < 0 || axis > 2)
				return;
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Mirror Axis");
			edit.mirror.enabled[axis] = on;
		}

		void VoxelEditor::SetMirrorPlane(const Vector3& plane) {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Move Mirror");
			PlaceMirrorPlane(plane);
		}

		void VoxelEditor::PreviewMirrorPlane(const Vector3& plane) {
			if (!previewedMirrorPlane)
				previewedMirrorPlane = edit.mirror.plane;
			PlaceMirrorPlane(plane);
		}

		void VoxelEditor::ResetMirrorPlane() {
			DocumentCommand command(*this);
			VoxelUndoStack::Step step(undo, "Reset Mirror");
			PlaceMirrorPlane(GetPivot());
		}

		void VoxelEditor::PlaceMirrorPlane(const Vector3& plane) {
			// MirrorIdx only sees whole half steps, so anything finer would move the
			// handle without moving the reflection.
			auto snap = [](float v) { return float(HalfSteps(v)) * 0.5F; };
			edit.mirror.plane = MakeVector3(snap(plane.x), snap(plane.y), snap(plane.z));
		}

		// Semi-transparent quad at each enabled mirror plane (drawn as a fan of
		// debug lines, since the editor only has line primitives in 3D).
		void VoxelEditor::DrawMirrorPlanes() {
			const IntVector3 size = document.Size();
			const int w = size.x, h = size.y, d = size.z;

			// Grid lines are spaced one tile (2 voxels) apart. Extend one tile past
			// the volume on every side, rounding the far edge up to the tile grid so
			// the plane is a whole number of tiles and always shows a complete border
			// (the volume spans [-0.5, dim-0.5]; the near edge -2.5 is already aligned).
			auto stop = [](int n) { return (n % 2 == 0) ? n + 2 : n + 3; };
			const Vector3 origin = host.DocumentOrigin();
			auto line = [&](const Vector3& p, const Vector3& q, const Vector4& color) {
				renderer->AddDebugLine(p + origin, q + origin, color);
			};
			float lo = -2.5F;
			float hiX = float(stop(w)) - 0.5F;
			float hiY = float(stop(h)) - 0.5F;
			float hiZ = float(stop(d)) - 0.5F;

			if (MirrorEnabled(0)) {
				float px = edit.mirror.plane.x;
				Vector4 col = MakeVector4(1.0F, 0.35F, 0.35F, 0.25F);
				for (int i = -2; i <= stop(h); i += 2)
					line(MakeVector3(px, float(i) - 0.5F, lo),
					                       MakeVector3(px, float(i) - 0.5F, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					line(MakeVector3(px, lo, float(i) - 0.5F),
					                       MakeVector3(px, hiY, float(i) - 0.5F), col);
			}
			if (MirrorEnabled(1)) {
				float py = edit.mirror.plane.y;
				Vector4 col = MakeVector4(0.4F, 1.0F, 0.4F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					line(MakeVector3(float(i) - 0.5F, py, lo),
					                       MakeVector3(float(i) - 0.5F, py, hiZ), col);
				for (int i = -2; i <= stop(d); i += 2)
					line(MakeVector3(lo, py, float(i) - 0.5F),
					                       MakeVector3(hiX, py, float(i) - 0.5F), col);
			}
			if (MirrorEnabled(2)) {
				float pz = edit.mirror.plane.z;
				Vector4 col = MakeVector4(0.45F, 0.6F, 1.0F, 0.25F);
				for (int i = -2; i <= stop(w); i += 2)
					line(MakeVector3(float(i) - 0.5F, lo, pz),
					                       MakeVector3(float(i) - 0.5F, hiY, pz), col);
				for (int i = -2; i <= stop(h); i += 2)
					line(MakeVector3(lo, float(i) - 0.5F, pz),
					                       MakeVector3(hiX, float(i) - 0.5F, pz), col);
			}
		}
	} // namespace gui
} // namespace spades
