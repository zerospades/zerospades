/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.
 based on code of pysnip (c) Mathias Kaerlev 2011-2012.

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

#include <cstdint>
#include <string>
#include <vector>

#include "Player.h"
#include <Core/CP437.h>
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Math.h>

namespace spades {
	namespace client {

		// Object IDs used in PacketTypeMoveObject
		enum { BLUE_FLAG = 0, GREEN_FLAG = 1, BLUE_BASE = 2, GREEN_BASE = 3 };

		enum PacketType {
			PacketTypePositionData = 0,    // C2S2P
			PacketTypeOrientationData = 1, // C2S2P
			PacketTypeWorldUpdate = 2,     // S2C
			PacketTypeInputData = 3,       // C2S2P
			PacketTypeWeaponInput = 4,     // C2S2P
			PacketTypeHitPacket = 5,       // C2S  (same wire value as SetHP — direction distinguishes them)
			PacketTypeSetHP = 5,           // S2C
			PacketTypeGrenadePacket = 6,   // C2S2P
			PacketTypeSetTool = 7,         // C2S2P
			PacketTypeSetColour = 8,       // C2S2P
			PacketTypeExistingPlayer = 9,  // C2S2P
			PacketTypeShortPlayerData = 10, // S2C
			PacketTypeMoveObject = 11,     // S2C
			PacketTypeCreatePlayer = 12,   // S2C
			PacketTypeBlockAction = 13,    // C2S2P
			PacketTypeBlockLine = 14,      // C2S2P
			PacketTypeStateData = 15,      // S2C
			PacketTypeKillAction = 16,     // S2C
			PacketTypeChatMessage = 17,    // C2S2P
			PacketTypeMapStart = 18,       // S2C
			PacketTypeMapChunk = 19,       // S2C
			PacketTypePlayerLeft = 20,     // S2P
			PacketTypeTerritoryCapture = 21, // S2P
			PacketTypeProgressBar = 22,    // S2P
			PacketTypeIntelCapture = 23,   // S2P
			PacketTypeIntelPickup = 24,    // S2P
			PacketTypeIntelDrop = 25,      // S2P
			PacketTypeRestock = 26,        // S2P
			PacketTypeFogColour = 27,      // S2C
			PacketTypeWeaponReload = 28,   // C2S2P
			PacketTypeChangeTeam = 29,     // C2S2P
			PacketTypeChangeWeapon = 30,   // C2S2P
			PacketTypeMapCached = 31,      // S2C  (same wire value as HandShakeInit — used in different protocol versions)
			PacketTypeHandShakeInit = 31,  // S2C
			PacketTypeHandShakeReturn = 32, // C2S
			PacketTypeVersionGet = 33,     // S2C
			PacketTypeVersionSend = 34,    // C2S
			PacketTypeExtensionInfo = 60,
			PacketTypePlayerProperties = 64,
			PacketTypeExtendedTeamplay = 66, // S2C2P, extension id 2 (`64 + extension id`)
		};

		/** Sub packet ids of `PacketTypeExtendedTeamplay`. Every extension packet
		 * carries one as its second byte, even when the extension needs only one. */
		enum ExtendedTeamplaySubPacketType {
			ExtendedTeamplaySubConfig = 0,  // S2C
			ExtendedTeamplaySubPing = 1,    // C2S2P
			ExtendedTeamplaySubESPMark = 2, // S2C
		};

		inline PlayerInput ParsePlayerInput(uint8_t bits) {
			PlayerInput inp;
			inp.moveForward  = (bits & (1 << 0)) != 0;
			inp.moveBackward = (bits & (1 << 1)) != 0;
			inp.moveLeft     = (bits & (1 << 2)) != 0;
			inp.moveRight    = (bits & (1 << 3)) != 0;
			inp.jump         = (bits & (1 << 4)) != 0;
			inp.crouch       = (bits & (1 << 5)) != 0;
			inp.sneak        = (bits & (1 << 6)) != 0;
			inp.sprint       = (bits & (1 << 7)) != 0;
			return inp;
		}

		inline WeaponInput ParseWeaponInput(uint8_t bits) {
			WeaponInput inp;
			inp.primary   = (bits & (1 << 0)) != 0;
			inp.secondary = (bits & (1 << 1)) != 0;
			return inp;
		}

		/**
		 * Reads fields from a raw packet byte buffer. The first byte (packet type) is
		 * consumed on construction; all Read* methods advance the internal cursor.
		 */
		class NetPacketReader {
			std::vector<char> data;
			size_t pos;

			static constexpr char UTFSign = -1;

			static std::string DecodeString(std::string s) {
				if (!s.empty() && s[0] == UTFSign)
					return s.substr(1);
				return CP437::Decode(s);
			}

		public:
			explicit NetPacketReader(std::vector<char> inData)
			    : data(std::move(inData)), pos(1) {}

			unsigned int GetTypeRaw() const {
				return static_cast<unsigned int>(static_cast<uint8_t>(data[0]));
			}
			PacketType GetType() const { return static_cast<PacketType>(GetTypeRaw()); }

			uint32_t ReadInt() {
				if (pos + 4 > data.size())
					SPRaise("Received packet truncated");
				uint32_t value = 0;
				value |= ((uint32_t)(uint8_t)data[pos++]);
				value |= ((uint32_t)(uint8_t)data[pos++]) << 8;
				value |= ((uint32_t)(uint8_t)data[pos++]) << 16;
				value |= ((uint32_t)(uint8_t)data[pos++]) << 24;
				return value;
			}

			uint16_t ReadShort() {
				if (pos + 2 > data.size())
					SPRaise("Received packet truncated");
				uint32_t value = 0;
				value |= ((uint32_t)(uint8_t)data[pos++]);
				value |= ((uint32_t)(uint8_t)data[pos++]) << 8;
				return (uint16_t)value;
			}

			uint8_t ReadByte() {
				if (pos >= data.size())
					SPRaise("Received packet truncated");
				return (uint8_t)data[pos++];
			}

			uint8_t Peek(size_t offset = 0) const {
				if (pos + offset >= data.size())
					SPRaise("Received packet truncated");
				return (uint8_t)data[pos + offset];
			}

			float ReadFloat() {
				union {
					float f;
					uint32_t v;
				};
				v = ReadInt();
				return f;
			}

			IntVector3 ReadIntColor() {
				IntVector3 col;
				col.z = ReadByte(); // B
				col.y = ReadByte(); // G
				col.x = ReadByte(); // R
				return col;
			}

			IntVector3 ReadIntVector3() {
				IntVector3 v;
				v.x = ReadInt();
				v.y = ReadInt();
				v.z = ReadInt();
				return v;
			}

			Vector3 ReadVector3() {
				Vector3 v;
				v.x = ReadFloat();
				v.y = ReadFloat();
				v.z = ReadFloat();
				return v;
			}

			std::size_t GetLength() const { return data.size(); }
			std::size_t GetPosition() const { return pos; }
			std::size_t GetNumRemainingBytes() const { return data.size() - pos; }
			const std::vector<char>& GetData() const { return data; }

			std::string ReadData(size_t siz) {
				if (pos + siz > data.size())
					SPRaise("Received packet truncated");
				std::string s(data.data() + pos, siz);
				pos += siz;
				return s;
			}

			std::string ReadRemainingData() {
				return std::string(data.data() + pos, data.size() - pos);
			}

			std::string ReadString(size_t siz) {
				return DecodeString(ReadData(siz).c_str());
			}

			std::string ReadRemainingString() {
				return DecodeString(ReadRemainingData().c_str());
			}

			void DumpDebug() const {
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
			}
		};

	} // namespace client
} // namespace spades
