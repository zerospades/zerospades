/*
 Copyright (c) 2013 yvt
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstdint>
#include <cstring> // memcpy (used by the ENet-packet reader ctor)
#include <string>
#include <vector>

#include <enet/enet.h>

#include "GameConstants.h"
#include "Player.h" // PlayerInput / WeaponInput
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Math.h>

namespace spades {
	namespace client {

		enum PacketType {
			PacketTypePositionData = 0,			// C2S2P
			PacketTypeOrientationData = 1,		// C2S2P
			PacketTypeWorldUpdate = 2,			// S2C
			PacketTypeInputData = 3,			// C2S2P
			PacketTypeWeaponInput = 4,			// C2S2P
			PacketTypeHitPacket = 5,			// C2S
			PacketTypeSetHP = 5,				// S2C
			PacketTypeGrenadePacket = 6,		// C2S2P
			PacketTypeSetTool = 7,				// C2S2P
			PacketTypeSetColour = 8,			// C2S2P
			PacketTypeExistingPlayer = 9,		// C2S2P
			PacketTypeShortPlayerData = 10,		// S2C
			PacketTypeMoveObject = 11,			// S2C
			PacketTypeCreatePlayer = 12,		// S2C
			PacketTypeBlockAction = 13,			// C2S2P
			PacketTypeBlockLine = 14,			// C2S2P
			PacketTypeStateData = 15,			// S2C
			PacketTypeKillAction = 16,			// S2C
			PacketTypeChatMessage = 17,			// C2S2P
			PacketTypeMapStart = 18,			// S2C
			PacketTypeMapChunk = 19,			// S2C
			PacketTypePlayerLeft = 20,			// S2P
			PacketTypeTerritoryCapture = 21,	// S2P
			PacketTypeProgressBar = 22,			// S2P
			PacketTypeIntelCapture = 23,		// S2P
			PacketTypeIntelPickup = 24,			// S2P
			PacketTypeIntelDrop = 25,			// S2P
			PacketTypeRestock = 26,				// S2P
			PacketTypeFogColour = 27,			// S2C
			PacketTypeWeaponReload = 28,		// C2S2P
			PacketTypeChangeTeam = 29,			// C2S2P
			PacketTypeChangeWeapon = 30,		// C2S2P
			PacketTypeMapCached = 31,			// S2C
			PacketTypeHandShakeInit = 31,		// S2C
			PacketTypeHandShakeReturn = 32,		// C2S
			PacketTypeVersionGet = 33,			// S2C
			PacketTypeVersionSend = 34,			// C2S
			PacketTypeExtensionInfo = 60,
			PacketTypePlayerProperties = 64,
		};

		// String CP437/UTF-8 framing helpers (depend on the cg_unicode setting).
		std::string EncodeString(std::string str);
		std::string DecodeString(std::string s);

		// Stateless input-bitfield helpers.
		PlayerInput ParsePlayerInput(uint8_t bits);
		WeaponInput ParseWeaponInput(uint8_t bits);

		class NetPacketReader {
			std::vector<char> data;
			size_t pos;

		public:
			NetPacketReader(ENetPacket* packet) {
				SPADES_MARK_FUNCTION();

				data.resize(packet->dataLength);
				memcpy(data.data(), packet->data, packet->dataLength);
				enet_packet_destroy(packet);
				pos = 1;
			}

			NetPacketReader(const std::vector<char> inData) {
				data = inData;
				pos = 1;
			}

			unsigned int GetTypeRaw() { return static_cast<unsigned int>(data[0]); }
			PacketType GetType() { return static_cast<PacketType>(GetTypeRaw()); }

			uint32_t ReadInt() {
				SPADES_MARK_FUNCTION();

				uint32_t value = 0;
				if (pos + 4 > data.size())
					SPRaise("Received packet truncated");

				value |= ((uint32_t)(uint8_t)data[pos++]);
				value |= ((uint32_t)(uint8_t)data[pos++]) << 8;
				value |= ((uint32_t)(uint8_t)data[pos++]) << 16;
				value |= ((uint32_t)(uint8_t)data[pos++]) << 24;
				return value;
			}

			uint16_t ReadShort() {
				SPADES_MARK_FUNCTION();

				uint32_t value = 0;
				if (pos + 2 > data.size())
					SPRaise("Received packet truncated");

				value |= ((uint32_t)(uint8_t)data[pos++]);
				value |= ((uint32_t)(uint8_t)data[pos++]) << 8;
				return (uint16_t)value;
			}

			uint8_t ReadByte() {
				SPADES_MARK_FUNCTION();

				if (pos >= data.size())
					SPRaise("Received packet truncated");

				return (uint8_t)data[pos++];
			}

			float ReadFloat() {
				SPADES_MARK_FUNCTION();
				union {
					float f;
					uint32_t v;
				};
				v = ReadInt();
				return f;
			}

			IntVector3 ReadIntColor() {
				SPADES_MARK_FUNCTION();
				IntVector3 col;
				col.z = ReadByte(); // B
				col.y = ReadByte(); // G
				col.x = ReadByte(); // R
				return col;
			}
			IntVector3 ReadIntVector3() {
				SPADES_MARK_FUNCTION();
				IntVector3 v;
				v.x = ReadInt();
				v.y = ReadInt();
				v.z = ReadInt();
				return v;
			}
			Vector3 ReadVector3() {
				SPADES_MARK_FUNCTION();
				Vector3 v;
				v.x = ReadFloat();
				v.y = ReadFloat();
				v.z = ReadFloat();
				return v;
			}

			std::size_t GetLength() { return data.size(); }
			std::size_t GetPosition() { return pos; }
			std::size_t GetNumRemainingBytes() { return data.size() - pos; }
			std::vector<char> GetData() { return data; }

			std::string ReadData(size_t siz) {
				if (pos + siz > data.size())
					SPRaise("Received packet truncated");

				std::string s = std::string(data.data() + pos, siz);
				pos += siz;
				return s;
			}
			std::string ReadRemainingData() {
				return std::string(data.data() + pos, data.size() - pos);
			}

			std::string ReadString(size_t siz) {
				SPADES_MARK_FUNCTION_DEBUG();
				// convert to C string once so that null-chars are removed
				return DecodeString(ReadData(siz).c_str());
			}
			std::string ReadRemainingString() {
				SPADES_MARK_FUNCTION_DEBUG();
				// convert to C string once so that null-chars are removed
				return DecodeString(ReadRemainingData().c_str());
			}

			void DumpDebug() {
#if 1
				char buf[512];
				std::string str;

				int bytes = (int)data.size();
				snprintf(buf, sizeof(buf), "Packet 0x%02x [len=%d]", (int)GetType(), bytes);
				str += buf;

				if (bytes > 64)
					bytes = 64;
				for (int i = 0; i < bytes; i++) {
					snprintf(buf, sizeof(buf), " %02x", (unsigned int)(unsigned char)data[i]);
					str += buf;
				}

				SPLog("%s", str.c_str());
#endif
			}
		};

		class NetPacketWriter {
			std::vector<char> data;

		public:
			NetPacketWriter(PacketType type) { data.push_back(type); }

			void WriteByte(uint8_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back(v);
			}
			void WriteShort(uint16_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back((char)(v));
				data.push_back((char)(v >> 8));
			}
			void WriteInt(uint32_t v) {
				SPADES_MARK_FUNCTION_DEBUG();
				data.push_back((char)(v));
				data.push_back((char)(v >> 8));
				data.push_back((char)(v >> 16));
				data.push_back((char)(v >> 24));
			}
			void WriteFloat(float v) {
				SPADES_MARK_FUNCTION_DEBUG();
				union {
					float f;
					uint32_t i;
				};
				f = v;
				WriteInt(i);
			}

			void WriteColor(IntVector3 v) {
				WriteByte((uint8_t)v.z); // B
				WriteByte((uint8_t)v.y); // G
				WriteByte((uint8_t)v.x); // R
			}
			void WriteIntVector3(IntVector3 v) {
				WriteInt((uint32_t)v.x);
				WriteInt((uint32_t)v.y);
				WriteInt((uint32_t)v.z);
			}
			void WriteVector3(const Vector3& v) {
				WriteFloat(v.x);
				WriteFloat(v.y);
				WriteFloat(v.z);
			}

			void WriteString(std::string str) {
				str = EncodeString(str);
				data.insert(data.end(), str.begin(), str.end());
			}

			void WriteString(const std::string& str, size_t fillLen) {
				WriteString(str.substr(0, fillLen));
				size_t sz = str.size();
				while (sz < fillLen) {
					WriteByte((uint8_t)0);
					sz++;
				}
			}

			std::size_t GetPosition() { return data.size(); }

			void Update(std::size_t position, std::uint8_t newValue) {
				SPADES_MARK_FUNCTION_DEBUG();

				if (position >= data.size()) {
					SPRaise("Invalid write (%d should be less than %d)",
						(int)position, (int)data.size());
				}

				data[position] = static_cast<char>(newValue);
			}

			void Update(std::size_t position, std::uint32_t newValue) {
				SPADES_MARK_FUNCTION_DEBUG();

				if (position + 4 > data.size()) {
					SPRaise("Invalid write (%d should be less than or equal to %d)",
							(int)(position + 4), (int)data.size());
				}

				// Assuming the target platform is little endian and supports
				// unaligned memory access...
				*reinterpret_cast<std::uint32_t*>(data.data() + position) = newValue;
			}

			ENetPacket* CreatePacket(int flag = ENET_PACKET_FLAG_RELIABLE) {
				return enet_packet_create(data.data(), data.size(), flag);
			}

			// Additive accessor: round-trip tests read the encoded bytes back to feed
			// a NetPacketReader. No behavior change to existing writer usage.
			std::vector<char> GetData() { return data; }
		};

		// --- Per-packet POD structs + Decode/Encode (added in Plans 02/03) ---
		//
		// Each version-agnostic, fixed-layout PacketType gets a POD struct holding
		// its decoded wire fields plus free Decode<Name>(NetPacketReader&)->struct and
		// Encode<Name>(const struct&)->NetPacketWriter functions (D-05). The codec is
		// PURE: no World/Player/NetClient state. Encode returns NetPacketWriter by value
		// (D-05; NRVO). Floats round-trip byte-exact (same bits). Colors are BGR
		// (WriteColor/ReadIntColor). For packets that exist in only one direction in
		// NetClient, the missing direction is added here FOR TESTING ONLY (D-12) —
		// NetClient's one-directional usage is unchanged. WorldUpdate(2)/ExistingPlayer(9)/
		// CreatePlayer(12)/StateData(15) are Plan 03; this header covers the rest.

		// --- Plan 02 Task 1: simple/fixed-width + dual-ordinal packets ---

		struct PositionDataPacket {
			Vector3 position;
		};
		PositionDataPacket DecodePositionData(NetPacketReader& r);
		NetPacketWriter EncodePositionData(const PositionDataPacket& p);

		struct OrientationDataPacket {
			Vector3 orientation;
		};
		OrientationDataPacket DecodeOrientationData(NetPacketReader& r);
		NetPacketWriter EncodeOrientationData(const OrientationDataPacket& p);

		// Raw input bits preserved for byte-exactness (ParsePlayerInput decodes them).
		struct InputDataPacket {
			uint8_t playerId;
			uint8_t bits;
		};
		InputDataPacket DecodeInputData(NetPacketReader& r);
		NetPacketWriter EncodeInputData(const InputDataPacket& p);

		struct WeaponInputPacket {
			uint8_t playerId;
			uint8_t bits;
		};
		WeaponInputPacket DecodeWeaponInput(NetPacketReader& r);
		NetPacketWriter EncodeWeaponInput(const WeaponInputPacket& p);

		// DUAL ORDINAL 5 (C2S). HitPacket and SetHP share PacketType 5 but are
		// distinct shapes; the caller (direction) disambiguates. Decode added for test.
		struct HitPacketPacket {
			uint8_t targetId;
			uint8_t hitType;
		};
		HitPacketPacket DecodeHitPacket(NetPacketReader& r);
		NetPacketWriter EncodeHitPacket(const HitPacketPacket& p);

		// DUAL ORDINAL 5 (S2C). Encode added for test.
		struct SetHPPacket {
			uint8_t hp;
			uint8_t type;
			Vector3 source;
		};
		SetHPPacket DecodeSetHP(NetPacketReader& r);
		NetPacketWriter EncodeSetHP(const SetHPPacket& p);

		struct GrenadePacket {
			uint8_t playerId;
			float fuse;
			Vector3 position;
			Vector3 velocity;
		};
		GrenadePacket DecodeGrenade(NetPacketReader& r);
		NetPacketWriter EncodeGrenade(const GrenadePacket& p);

		struct SetToolPacket {
			uint8_t playerId;
			uint8_t tool;
		};
		SetToolPacket DecodeSetTool(NetPacketReader& r);
		NetPacketWriter EncodeSetTool(const SetToolPacket& p);

		// color is BGR on the wire (WriteColor/ReadIntColor); IntVector3 = {x=R,y=G,z=B}.
		struct SetColourPacket {
			uint8_t playerId;
			IntVector3 color;
		};
		SetColourPacket DecodeSetColour(NetPacketReader& r);
		NetPacketWriter EncodeSetColour(const SetColourPacket& p);

		// [ASSUMED] spec-derived layout — no in-repo decode oracle (RESEARCH A1).
		// NetClient raises "Unexpected" on receipt (:953) and never encodes it; this
		// is a self-consistent round-trip from the AoS 0.75 spec [id,team,weapon].
		struct ShortPlayerDataPacket {
			uint8_t playerId;
			uint8_t team;
			uint8_t weapon;
		};
		ShortPlayerDataPacket DecodeShortPlayerData(NetPacketReader& r);
		NetPacketWriter EncodeShortPlayerData(const ShortPlayerDataPacket& p);

		struct BlockActionPacket {
			uint8_t playerId;
			uint8_t action;
			IntVector3 position;
		};
		BlockActionPacket DecodeBlockAction(NetPacketReader& r);
		NetPacketWriter EncodeBlockAction(const BlockActionPacket& p);

		struct BlockLinePacket {
			uint8_t playerId;
			IntVector3 start;
			IntVector3 end;
		};
		BlockLinePacket DecodeBlockLine(NetPacketReader& r);
		NetPacketWriter EncodeBlockLine(const BlockLinePacket& p);

		// S2C. Encode added for test.
		struct KillActionPacket {
			uint8_t victimId;
			uint8_t killerId;
			uint8_t killType;
			uint8_t respawnTime;
		};
		KillActionPacket DecodeKillAction(NetPacketReader& r);
		NetPacketWriter EncodeKillAction(const KillActionPacket& p);

		struct WeaponReloadPacket {
			uint8_t playerId;
			uint8_t clip;
			uint8_t reserve;
		};
		WeaponReloadPacket DecodeWeaponReload(NetPacketReader& r);
		NetPacketWriter EncodeWeaponReload(const WeaponReloadPacket& p);

		struct ChangeTeamPacket {
			uint8_t playerId;
			uint8_t team;
		};
		ChangeTeamPacket DecodeChangeTeam(NetPacketReader& r);
		NetPacketWriter EncodeChangeTeam(const ChangeTeamPacket& p);

		struct ChangeWeaponPacket {
			uint8_t playerId;
			uint8_t weapon;
		};
		ChangeWeaponPacket DecodeChangeWeapon(NetPacketReader& r);
		NetPacketWriter EncodeChangeWeapon(const ChangeWeaponPacket& p);

		// DUAL ORDINAL 31 (C2S). MapCached and HandShakeInit share PacketType 31.
		// Decode added for test.
		struct MapCachedPacket {
			uint8_t cached;
		};
		MapCachedPacket DecodeMapCached(NetPacketReader& r);
		NetPacketWriter EncodeMapCached(const MapCachedPacket& p);

		// DUAL ORDINAL 31 (S2C). Encode added for test.
		struct HandShakeInitPacket {
			uint32_t challenge;
		};
		HandShakeInitPacket DecodeHandShakeInit(NetPacketReader& r);
		NetPacketWriter EncodeHandShakeInit(const HandShakeInitPacket& p);

		// C2S. Decode added for test.
		struct HandShakeReturnPacket {
			uint32_t challenge;
		};
		HandShakeReturnPacket DecodeHandShakeReturn(NetPacketReader& r);
		NetPacketWriter EncodeHandShakeReturn(const HandShakeReturnPacket& p);

		// S2C. Encode added for test. All 8 bytes round-trip.
		struct PlayerPropertiesPacket {
			uint8_t subId;
			uint8_t playerId;
			uint8_t hp;
			uint8_t blocks;
			uint8_t grenades;
			uint8_t clip;
			uint8_t reserve;
			uint8_t score;
		};
		PlayerPropertiesPacket DecodePlayerProperties(NetPacketReader& r);
		NetPacketWriter EncodePlayerProperties(const PlayerPropertiesPacket& p);

		// --- Plan 02 Task 2: game-mode-coupled, string, list, signed-field packets ---

		// S2C. Encode added for test.
		struct MoveObjectPacket {
			uint8_t type;
			uint8_t state;
			Vector3 position;
		};
		MoveObjectPacket DecodeMoveObject(NetPacketReader& r);
		NetPacketWriter EncodeMoveObject(const MoveObjectPacket& p);

		// message round-trips via CP437/UTF-8 framing (cg_unicode pinned in tests).
		struct ChatMessagePacket {
			uint8_t playerId;
			uint8_t type;
			std::string message;
		};
		ChatMessagePacket DecodeChatMessage(NetPacketReader& r);
		NetPacketWriter EncodeChatMessage(const ChatMessagePacket& p);

		// S2C. Encode added for test. Bytes are version-independent (just mapSize); the
		// v3/v4 difference is a NetClient SIDE-EFFECT (SendMapCached), not a wire layout
		// change (D-13) — so a single round-trip here covers both protocol versions.
		struct MapStartPacket {
			uint32_t mapSize;
		};
		MapStartPacket DecodeMapStart(NetPacketReader& r);
		NetPacketWriter EncodeMapStart(const MapStartPacket& p);

		// Raw remaining bytes — no string normalization. Both directions added for test.
		struct MapChunkPacket {
			std::string data;
		};
		MapChunkPacket DecodeMapChunk(NetPacketReader& r);
		NetPacketWriter EncodeMapChunk(const MapChunkPacket& p);

		// S2P. Encode added for test.
		struct PlayerLeftPacket {
			uint8_t playerId;
		};
		PlayerLeftPacket DecodePlayerLeft(NetPacketReader& r);
		NetPacketWriter EncodePlayerLeft(const PlayerLeftPacket& p);

		// S2P. Encode added for test.
		struct TerritoryCapturePacket {
			uint8_t territoryId;
			uint8_t winning;
			uint8_t state;
		};
		TerritoryCapturePacket DecodeTerritoryCapture(NetPacketReader& r);
		NetPacketWriter EncodeTerritoryCapture(const TerritoryCapturePacket& p);

		// S2P. Encode added for test. rate is SIGNED ((int8_t)ReadByte() at :1322).
		struct ProgressBarPacket {
			uint8_t territoryId;
			uint8_t capturingTeam;
			int8_t rate;
			float progress;
		};
		ProgressBarPacket DecodeProgressBar(NetPacketReader& r);
		NetPacketWriter EncodeProgressBar(const ProgressBarPacket& p);

		// S2P. Encode added for test.
		struct IntelCapturePacket {
			uint8_t playerId;
			uint8_t winning;
		};
		IntelCapturePacket DecodeIntelCapture(NetPacketReader& r);
		NetPacketWriter EncodeIntelCapture(const IntelCapturePacket& p);

		// S2P. Encode added for test.
		struct IntelPickupPacket {
			uint8_t playerId;
		};
		IntelPickupPacket DecodeIntelPickup(NetPacketReader& r);
		NetPacketWriter EncodeIntelPickup(const IntelPickupPacket& p);

		// S2P. Encode added for test.
		struct IntelDropPacket {
			uint8_t playerId;
			Vector3 position;
		};
		IntelDropPacket DecodeIntelDrop(NetPacketReader& r);
		NetPacketWriter EncodeIntelDrop(const IntelDropPacket& p);

		// S2P. Encode added for test.
		struct RestockPacket {
			uint8_t playerId;
		};
		RestockPacket DecodeRestock(NetPacketReader& r);
		NetPacketWriter EncodeRestock(const RestockPacket& p);

		// S2C. recv skips alpha then reads BGR color (:1424-1428). Encode added for test.
		struct FogColourPacket {
			uint8_t alpha;
			IntVector3 color;
		};
		FogColourPacket DecodeFogColour(NetPacketReader& r);
		NetPacketWriter EncodeFogColour(const FogColourPacket& p);

		// Handshake. Empty list = simple variant; non-empty = enhanced. Both dirs for test.
		struct VersionGetPacket {
			std::vector<uint8_t> propertyIds;
		};
		VersionGetPacket DecodeVersionGet(NetPacketReader& r);
		NetPacketWriter EncodeVersionGet(const VersionGetPacket& p);

		// C2S. Decode added for test. tag is 'o' (:1756).
		struct VersionSendPacket {
			uint8_t tag;
			uint8_t major;
			uint8_t minor;
			uint8_t patch;
			std::string osInfo;
		};
		VersionSendPacket DecodeVersionSend(NetPacketReader& r);
		NetPacketWriter EncodeVersionSend(const VersionSendPacket& p);

		// count byte then count×{id,version} (:1769-1774).
		struct ExtensionInfoEntry {
			uint8_t id;
			uint8_t version;
		};
		struct ExtensionInfoPacket {
			std::vector<ExtensionInfoEntry> extensions;
		};
		ExtensionInfoPacket DecodeExtensionInfo(NetPacketReader& r);
		NetPacketWriter EncodeExtensionInfo(const ExtensionInfoPacket& p);

	} // namespace client
} // namespace spades
