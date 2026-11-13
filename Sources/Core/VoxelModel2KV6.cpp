/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades.

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

#include "VoxelModel2KV6.h"

#include <cmath>
#include <cstring>
#include <algorithm>

#include "Debug.h"
#include "Exception.h"
#include "IStream.h"

namespace spades {
	namespace {
		// Object flags, one bit each.
		constexpr uint8_t kFlagLocked = 1u << 0;
		constexpr uint8_t kFlagHidden = 1u << 1;
		constexpr uint8_t kKnownFlags = kFlagLocked | kFlagHidden;

		// Clip flags, one bit each.
		constexpr uint8_t kClipNameLocked = 1u << 0;
		constexpr uint8_t kClipDurationLocked = 1u << 1;
		constexpr uint8_t kKnownClipFlags = kClipNameLocked | kClipDurationLocked;

		// The most text a metadata entry may hold: notes, never something to
		// stall a game loading a model.
		constexpr uint32_t kMaxMetadataBytes = 16u * 1024u * 1024u;

		template <class T> void ReadValue(IStream& stream, T& value, const char* what) {
			if (stream.Read(&value, sizeof(value)) < sizeof(value))
				SPRaise(".2kv6 file truncated: failed to read %s", what);
		}

		std::string ReadString(IStream& stream, std::size_t length, const char* what) {
			std::string out(length, '\0');
			if (length > 0 && stream.Read(&out[0], length) < length)
				SPRaise(".2kv6 file truncated: failed to read %s", what);
			return out;
		}

		std::string ReadShortString(IStream& stream, const char* what) {
			uint16_t length;
			ReadValue(stream, length, what);
			return ReadString(stream, length, what);
		}

		void WriteShortString(IStream& stream, const std::string& text, const char* what) {
			if (text.size() > UINT16_MAX)
				SPRaise("%s too long: %zu bytes", what, text.size());
			const uint16_t length = static_cast<uint16_t>(text.size());
			stream.Write(&length, sizeof(length));
			if (length > 0)
				stream.Write(text.data(), length);
		}

		TransformKeyframe ReadKeyframe(IStream& stream) {
			TransformKeyframe kf;
			float pos[3], rot[4], scale[3];
			ReadValue(stream, kf.time, "a keyframe's time");
			ReadValue(stream, pos, "a keyframe's position");
			ReadValue(stream, rot, "a keyframe's rotation");
			ReadValue(stream, scale, "a keyframe's scale");
			kf.position = MakeVector3(pos[0], pos[1], pos[2]);
			kf.rotation = MakeVector4(rot[0], rot[1], rot[2], rot[3]);
			kf.scale = MakeVector3(scale[0], scale[1], scale[2]);
			return kf;
		}

		void WriteKeyframe(IStream& stream, const TransformKeyframe& kf) {
			const float pos[3] = {kf.position.x, kf.position.y, kf.position.z};
			const float rot[4] = {kf.rotation.x, kf.rotation.y, kf.rotation.z, kf.rotation.w};
			const float scale[3] = {kf.scale.x, kf.scale.y, kf.scale.z};
			stream.Write(&kf.time, sizeof(kf.time));
			stream.Write(pos, sizeof(pos));
			stream.Write(rot, sizeof(rot));
			stream.Write(scale, sizeof(scale));
		}

		std::vector<MetadataEntry> ReadMetadata(IStream& stream) {
			uint16_t count;
			ReadValue(stream, count, "a metadata count");
			std::vector<MetadataEntry> table;
			table.reserve(count);
			for (uint16_t i = 0; i < count; i++) {
				MetadataEntry entry;
				entry.name = ReadShortString(stream, "a metadata name");
				uint32_t length;
				ReadValue(stream, length, "a metadata length");
				if (length > kMaxMetadataBytes)
					SPRaise("Metadata '%s' is %u bytes, more than the %u a .2kv6 holds",
					        entry.name.c_str(), length, kMaxMetadataBytes);
				entry.text = ReadString(stream, length, "metadata text");
				// What was written unchecked, by hand or by another program, is
				// refused rather than handed over.
				VoxelModel2KV6::Validate(entry);
				table.push_back(std::move(entry));
			}
			return table;
		}

		void CheckUniqueNames(const std::vector<MetadataEntry>& table) {
			for (std::size_t i = 0; i < table.size(); i++)
				for (std::size_t j = i + 1; j < table.size(); j++)
					if (table[i].name == table[j].name)
						SPRaise("Two metadata entries are named '%s'", table[i].name.c_str());
		}

		void WriteMetadata(IStream& stream, const std::vector<MetadataEntry>& table) {
			if (table.size() > UINT16_MAX)
				SPRaise("Too many metadata entries: %zu", table.size());
			CheckUniqueNames(table);
			const uint16_t count = static_cast<uint16_t>(table.size());
			stream.Write(&count, sizeof(count));
			for (const MetadataEntry& entry : table) {
				VoxelModel2KV6::Validate(entry);
				WriteShortString(stream, entry.name, "A metadata name");
				const uint32_t length = static_cast<uint32_t>(entry.text.size());
				stream.Write(&length, sizeof(length));
				if (length > 0)
					stream.Write(entry.text.data(), length);
			}
		}

		AnimationClip ReadClip(IStream& stream) {
			AnimationClip clip;
			clip.name = ReadShortString(stream, "a clip name");
			ReadValue(stream, clip.duration, "a clip duration");
			uint8_t flags;
			ReadValue(stream, flags, "clip flags");
			if ((flags & ~kKnownClipFlags) != 0)
				SPRaise("Unknown .2kv6 clip flags 0x%02x on '%s'", unsigned(flags), clip.name.c_str());
			clip.nameLocked = (flags & kClipNameLocked) != 0;
			clip.durationLocked = (flags & kClipDurationLocked) != 0;
			uint16_t tracks;
			ReadValue(stream, tracks, "a track count");
			for (uint16_t t = 0; t < tracks; t++) {
				AnimationTrack track;
				track.path = ReadShortString(stream, "a track path");
				uint16_t keys;
				ReadValue(stream, keys, "a keyframe count");
				for (uint16_t k = 0; k < keys; k++)
					track.keyframes.push_back(ReadKeyframe(stream));
				clip.tracks.push_back(std::move(track));
			}
			VoxelModel2KV6::Validate(clip);
			return clip;
		}

		void WriteClip(IStream& stream, const AnimationClip& clip) {
			VoxelModel2KV6::Validate(clip);
			WriteShortString(stream, clip.name, "A clip name");
			stream.Write(&clip.duration, sizeof(clip.duration));
			const uint8_t flags = uint8_t((clip.nameLocked ? kClipNameLocked : 0) |
			                              (clip.durationLocked ? kClipDurationLocked : 0));
			stream.Write(&flags, sizeof(flags));
			if (clip.tracks.size() > UINT16_MAX)
				SPRaise("Too many tracks in clip '%s'", clip.name.c_str());
			const uint16_t tracks = static_cast<uint16_t>(clip.tracks.size());
			stream.Write(&tracks, sizeof(tracks));
			for (const AnimationTrack& track : clip.tracks) {
				WriteShortString(stream, track.path, "A track path");
				if (track.keyframes.size() > UINT16_MAX)
					SPRaise("Too many keyframes in a track of '%s'", clip.name.c_str());
				const uint16_t keys = static_cast<uint16_t>(track.keyframes.size());
				stream.Write(&keys, sizeof(keys));
				for (const TransformKeyframe& kf : track.keyframes)
					WriteKeyframe(stream, kf);
			}
		}

		VoxelObject LoadObject(IStream& stream, uint16_t version, int depth = 0) {
			if (depth > 64)
				SPRaise("Object hierarchy too deep (max 64 levels)");

			VoxelObject obj;

			// Read object name
			uint16_t nameLen;
			if (stream.Read(&nameLen, sizeof(nameLen)) < sizeof(nameLen))
				SPRaise(".2kv6 file truncated: failed to read name length at depth %d", depth);

			if (nameLen > 0) {
				std::string name(nameLen, '\0');
				if (stream.Read(&name[0], nameLen) < nameLen)
					SPRaise(".2kv6 file truncated: failed to read name at depth %d", depth);
				obj.name = name;
			}

			// Read local transform
			float posData[3], rotData[4], scaleData[3];

			if (stream.Read(posData, sizeof(posData)) < sizeof(posData))
				SPRaise(".2kv6 file truncated: failed to read position at depth %d", depth);
			obj.position = MakeVector3(posData[0], posData[1], posData[2]);

			if (stream.Read(rotData, sizeof(rotData)) < sizeof(rotData))
				SPRaise(".2kv6 file truncated: failed to read rotation at depth %d", depth);
			obj.rotation = MakeVector4(rotData[0], rotData[1], rotData[2], rotData[3]);

			if (stream.Read(scaleData, sizeof(scaleData)) < sizeof(scaleData))
				SPRaise(".2kv6 file truncated: failed to read scale at depth %d", depth);
			obj.scale = MakeVector3(scaleData[0], scaleData[1], scaleData[2]);

			uint8_t flags;
			if (stream.Read(&flags, sizeof(flags)) < sizeof(flags))
				SPRaise(".2kv6 file truncated: failed to read flags at depth %d", depth);
			if ((flags & ~kKnownFlags) != 0)
				SPRaise("Unknown .2kv6 object flags 0x%02x at depth %d", unsigned(flags), depth);
			obj.locked = (flags & kFlagLocked) != 0;
			obj.hidden = (flags & kFlagHidden) != 0;

			// Read hasModel flag
			uint8_t hasModel;
			if (stream.Read(&hasModel, sizeof(hasModel)) < sizeof(hasModel))
				SPRaise(".2kv6 file truncated: failed to read hasModel flag at depth %d", depth);

			// Load KV6 data if present
			if (hasModel != 0) {
				try {
					obj.model = VoxelModel::LoadKV6(stream);
				} catch (const std::exception& e) {
					SPRaise("Failed to load KV6 data at depth %d: %s", depth, e.what());
				}
			}

			// Version 1 gave every object keys of its own, which no file
			// carries: an animation now is the scene's, in its clips.
			if (version < 2) {
				uint16_t numKeyframes;
				ReadValue(stream, numKeyframes, "a keyframe count");
				if (numKeyframes != 0)
					SPRaise("Object '%s' has keyframes of its own: a .2kv6 animates its "
					        "objects only through the scene's clips",
					        obj.name.c_str());
			}

			// Read child objects
			uint16_t numChildren;
			if (stream.Read(&numChildren, sizeof(numChildren)) < sizeof(numChildren))
				SPRaise(".2kv6 file truncated: failed to read child count at depth %d", depth);

			obj.children.reserve(numChildren);
			for (uint16_t c = 0; c < numChildren; c++) {
				obj.children.push_back(LoadObject(stream, version, depth + 1));
			}

			return obj;
		}

		void SaveObject(IStream& stream, const VoxelObject& obj, int depth = 0) {
			if (depth > 64)
				SPRaise("Object hierarchy too deep (max 64 levels)");

			// Write object name
			size_t nameLength = obj.name.length();
			if (nameLength > UINT16_MAX)
				SPRaise("Object name too long: %zu characters", nameLength);
			uint16_t nameLen = static_cast<uint16_t>(nameLength);

			stream.Write(&nameLen, sizeof(nameLen));
			if (nameLen > 0)
				stream.Write(obj.name.data(), nameLen);

			// Write local transform
			float posData[3] = {obj.position.x, obj.position.y, obj.position.z};
			float rotData[4] = {obj.rotation.x, obj.rotation.y, obj.rotation.z, obj.rotation.w};
			float scaleData[3] = {obj.scale.x, obj.scale.y, obj.scale.z};

			stream.Write(posData, sizeof(posData));
			stream.Write(rotData, sizeof(rotData));
			stream.Write(scaleData, sizeof(scaleData));

			const uint8_t flags =
			  uint8_t((obj.locked ? kFlagLocked : 0) | (obj.hidden ? kFlagHidden : 0));
			stream.Write(&flags, sizeof(flags));

			// Write hasModel flag
			uint8_t hasModel = obj.model ? 1 : 0;
			stream.Write(&hasModel, sizeof(hasModel));

			// Write KV6 data if present
			if (obj.model) {
				obj.model->SaveKV6(stream);
			}

			// Write child objects
			uint16_t numChildren = static_cast<uint16_t>(obj.children.size());
			if (numChildren > UINT16_MAX)
				SPRaise("Too many children for object '%s': %zu (max %u)", obj.name.c_str(),
				        obj.children.size(), UINT16_MAX);

			stream.Write(&numChildren, sizeof(numChildren));

			for (const VoxelObject& child : obj.children) {
				SaveObject(stream, child, depth + 1);
			}
		}
	} // namespace

	std::vector<VoxelObject> VoxelModel2KV6::Load(IStream& stream) {
		return LoadScene(stream).objects;
	}

	void VoxelModel2KV6::Save(IStream& stream, const std::vector<VoxelObject>& objects) {
		SceneFile scene;
		scene.objects = objects;
		SaveScene(stream, scene);
	}

	VoxelModel2KV6::SceneFile VoxelModel2KV6::LoadScene(IStream& stream) {
		SPADES_MARK_FUNCTION();

		// Read and verify file header (8 bytes total)
		KV6FileHeader header;
		if (stream.Read(&header, sizeof(header)) < sizeof(header))
			SPRaise(".2kv6 file truncated: failed to read header (need 8 bytes)");

		// Verify magic signature
		if (std::strncmp(header.magic, MAGIC, 4) != 0)
			SPRaise("Invalid .2kv6 file: magic signature mismatch (not a .2kv6 file)");

		// Check version compatibility
		if (header.version < 1 || header.version > FORMAT_VERSION)
			SPRaise("Unsupported .2kv6 format version: %u (this reader supports 1 to %u)",
			        header.version, FORMAT_VERSION);

		SceneFile scene;
		scene.objects.reserve(header.numRootObjects);
		for (uint16_t i = 0; i < header.numRootObjects; i++)
			scene.objects.push_back(LoadObject(stream, header.version, 0));

		if (header.version >= 2) {
			scene.metadata = ReadMetadata(stream);
			CheckUniqueNames(scene.metadata);
			uint16_t clips;
			ReadValue(stream, clips, "a clip count");
			for (uint16_t c = 0; c < clips; c++)
				scene.clips.push_back(ReadClip(stream));
			for (std::size_t i = 0; i < scene.clips.size(); i++)
				for (std::size_t j = i + 1; j < scene.clips.size(); j++)
					if (scene.clips[i].name == scene.clips[j].name)
						SPRaise("Two clips are named '%s'", scene.clips[i].name.c_str());
		}
		return scene;
	}

	void VoxelModel2KV6::SaveScene(IStream& stream, const SceneFile& scene) {
		SPADES_MARK_FUNCTION();

		// A scene with no objects is written as such: an editor may empty one,
		// and it must be able to save and reopen it.
		if (scene.objects.size() > UINT16_MAX)
			SPRaise("Too many root objects in .2kv6 scene: %zu (max %u)", scene.objects.size(),
			        UINT16_MAX);
		if (scene.clips.size() > UINT16_MAX)
			SPRaise("Too many clips in .2kv6 scene: %zu", scene.clips.size());
		for (std::size_t i = 0; i < scene.clips.size(); i++)
			for (std::size_t j = i + 1; j < scene.clips.size(); j++)
				if (scene.clips[i].name == scene.clips[j].name)
					SPRaise("Two clips are named '%s'", scene.clips[i].name.c_str());

		// Write file header (8 bytes total)
		KV6FileHeader header;
		std::strncpy(header.magic, MAGIC, 4);
		header.version = FORMAT_VERSION;
		header.numRootObjects = static_cast<uint16_t>(scene.objects.size());
		stream.Write(&header, sizeof(header));

		// Recursively write objects
		for (const VoxelObject& obj : scene.objects)
			SaveObject(stream, obj, 0);

		WriteMetadata(stream, scene.metadata);
		const uint16_t clips = static_cast<uint16_t>(scene.clips.size());
		stream.Write(&clips, sizeof(clips));
		for (const AnimationClip& clip : scene.clips)
			WriteClip(stream, clip);

		stream.Flush();
	}

	void VoxelModel2KV6::Validate(const MetadataEntry& entry) {
		if (entry.name.empty() || entry.name.size() > 255)
			SPRaise("A metadata name is 1 to 255 bytes long");
		const std::string& d = entry.text;
		const char* name = entry.name.c_str();
		if (d.size() > kMaxMetadataBytes)
			SPRaise("Metadata '%s' is %zu bytes, more than the %u a .2kv6 holds", name, d.size(),
			        kMaxMetadataBytes);
		// UTF-8, every sequence well formed, and no NUL anywhere.
		for (std::size_t i = 0; i < d.size();) {
			const unsigned char c = static_cast<unsigned char>(d[i]);
			if (c == 0)
				SPRaise("Metadata '%s' holds a NUL byte: text holds none", name);
			// How many continuation bytes the lead byte announces.
			const int more = c < 0x80          ? 0
			                 : (c >> 5) == 0x6  ? 1
			                 : (c >> 4) == 0xE  ? 2
			                 : (c >> 3) == 0x1E ? 3
			                                    : -1;
			if (more < 0 || i + std::size_t(more) >= d.size())
				SPRaise("Metadata '%s' is not UTF-8 text", name);
			for (int k = 1; k <= more; k++)
				if ((static_cast<unsigned char>(d[i + k]) & 0xC0) != 0x80)
					SPRaise("Metadata '%s' is not UTF-8 text", name);
			i += std::size_t(more) + 1;
		}
	}

	void VoxelModel2KV6::Validate(const AnimationClip& clip) {
		if (clip.name.empty() || clip.name.size() > 255)
			SPRaise("A clip's name is 1 to 255 bytes long");
		if (!(clip.duration > 0.0F) || !std::isfinite(clip.duration))
			SPRaise("Clip '%s' lasts no time: its duration is above zero", clip.name.c_str());
		for (const AnimationTrack& track : clip.tracks) {
			if (track.path.empty())
				SPRaise("A track of clip '%s' names no object", clip.name.c_str());
			for (const TransformKeyframe& kf : track.keyframes)
				if (!(kf.time >= 0.0F && kf.time <= clip.duration))
					SPRaise("A key of '%s' in clip '%s' lies outside its %g seconds",
					        track.path.c_str(), clip.name.c_str(), double(clip.duration));
		}
	}

	VoxelObject VoxelModel2KV6::CreateObject(const std::string& name, int sizeXYZ) {
		VoxelObject obj;
		obj.name = name;
		obj.model = Handle<VoxelModel>::New(sizeXYZ, sizeXYZ, sizeXYZ);
		obj.model->SetSolid(sizeXYZ / 2, sizeXYZ / 2, sizeXYZ / 2, 0xFFFFFF);
		float c = float(sizeXYZ / 2);
		obj.model->SetOrigin(MakeVector3(-c, -c, -c));
		return obj;
	}

	bool VoxelModel2KV6::DeleteObject(VoxelObject& parent, size_t childIndex) {
		if (childIndex >= parent.children.size())
			return false;
		parent.children.erase(parent.children.begin() + childIndex);
		return true;
	}

	VoxelObject* VoxelModel2KV6::FindObjectByName(std::vector<VoxelObject>& scene,
	                                               const std::string& name) {
		// Breadth-first search for named object
		std::vector<VoxelObject*> queue;
		for (auto& obj : scene)
			queue.push_back(&obj);

		while (!queue.empty()) {
			VoxelObject* current = queue.front();
			queue.erase(queue.begin());

			if (current->name == name)
				return current;

			for (auto& child : current->children)
				queue.push_back(&child);
		}

		return nullptr;
	}

	Matrix4 LocalTransform(const Vector3& position, const Vector4& rotation, const Vector3& scale) {
		return Matrix4::Translate(position) * Quaternion(rotation).ToRotationMatrix() *
		       Matrix4::Scale(scale);
	}

	namespace {
		// The rotation of an orthonormal, right-handed frame, built from its
		// largest part so a half turn never divides by zero.
		Vector4 RotationOf(const Vector3& a, const Vector3& b, const Vector3& c) {
			const float r00 = a.x, r10 = a.y, r20 = a.z, r01 = b.x, r11 = b.y, r21 = b.z;
			const float r02 = c.x, r12 = c.y, r22 = c.z;
			const float trace = r00 + r11 + r22;
			Vector4 q;
			if (trace > 0.0F) {
				const float s = std::sqrt(trace + 1.0F) * 2.0F;
				q = MakeVector4((r21 - r12) / s, (r02 - r20) / s, (r10 - r01) / s, 0.25F * s);
			} else if (r00 > r11 && r00 > r22) {
				const float s = std::sqrt(1.0F + r00 - r11 - r22) * 2.0F;
				q = MakeVector4(0.25F * s, (r01 + r10) / s, (r02 + r20) / s, (r21 - r12) / s);
			} else if (r11 > r22) {
				const float s = std::sqrt(1.0F + r11 - r00 - r22) * 2.0F;
				q = MakeVector4((r01 + r10) / s, 0.25F * s, (r12 + r21) / s, (r02 - r20) / s);
			} else {
				const float s = std::sqrt(1.0F + r22 - r00 - r11) * 2.0F;
				q = MakeVector4((r02 + r20) / s, (r12 + r21) / s, 0.25F * s, (r10 - r01) / s);
			}
			return q.Normalize();
		}
	} // namespace

	namespace {
		Vector4 TurnAbout(int axis, float degrees) {
			const float half = degrees * float(M_PI) / 360.0F;
			const float s = std::sin(half);
			return MakeVector4(axis == 0 ? s : 0.0F, axis == 1 ? s : 0.0F, axis == 2 ? s : 0.0F,
			                   std::cos(half));
		}
	} // namespace

	Vector4 RotationFromEuler(const Vector3& degrees) {
		const Quaternion x(TurnAbout(0, degrees.x)), y(TurnAbout(1, degrees.y)),
		  z(TurnAbout(2, degrees.z));
		return (z * (y * x)).Normalize().v;
	}

	Vector3 EulerFromRotation(const Vector4& q) {
		const float x = q.x, y = q.y, z = q.z, w = q.w;
		const float r00 = 1 - 2 * (y * y + z * z), r10 = 2 * (x * y + w * z);
		const float r20 = 2 * (x * z - w * y), r21 = 2 * (y * z + w * x);
		const float r22 = 1 - 2 * (x * x + y * y), r11 = 1 - 2 * (x * x + z * z);
		const float r12 = 2 * (y * z - w * x);
		const float toDeg = 180.0F / float(M_PI);
		const float sy = std::max(-1.0F, std::min(1.0F, -r20));
		if (std::fabs(sy) > 0.9999F) // looking straight along Y: X takes it all
			return MakeVector3(std::atan2(-r12, r11) * toDeg, std::asin(sy) * toDeg, 0.0F);
		return MakeVector3(std::atan2(r21, r22) * toDeg, std::asin(sy) * toDeg,
		                   std::atan2(r10, r00) * toDeg);
	}

	bool DecomposeTransform(const Matrix4& m, Vector3& position, Vector4& rotation,
	                        Vector3& scale) {
		Vector3 axis[3] = {m.GetAxis(0), m.GetAxis(1), m.GetAxis(2)};
		float s[3];
		bool exact = true;
		for (int k = 0; k < 3; k++) {
			s[k] = axis[k].GetLength();
			if (s[k] > 1.0e-6F)
				axis[k] = axis[k] * (1.0F / s[k]);
			else
				exact = false; // flattened along it: no turn says which way it faced
		}
		// The axes of a turned and scaled frame stay square to one another; a
		// shear tilts them, which nothing here can hold.
		constexpr float kSquare = 1.0e-3F;
		for (int k = 0; k < 3 && exact; k++) {
			if (std::fabs(Vector3::Dot(axis[k], axis[(k + 1) % 3])) > kSquare)
				exact = false;
		}
		if (Vector3::Dot(Vector3::Cross(axis[0], axis[1]), axis[2]) < 0.0F) {
			s[0] = -s[0]; // mirrored: one axis carries the reflection
			axis[0] = axis[0] * -1.0F;
		}
		position = m.GetOrigin();
		rotation = RotationOf(axis[0], axis[1], axis[2]);
		scale = MakeVector3(s[0], s[1], s[2]);
		return exact;
	}

} // namespace spades
