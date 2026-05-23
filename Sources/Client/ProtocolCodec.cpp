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

#include "ProtocolCodec.h"

#include <Core/CP437.h>
#include <Core/Settings.h>

DEFINE_SPADES_SETTING(cg_unicode, "1");

namespace spades {
	namespace client {

		namespace {
			const char UTFSign = -1;
		} // namespace

		std::string EncodeString(std::string str) {
			auto str2 = CP437::Encode(str, -1);
			if (!cg_unicode)
				return str2; // ignore fallbacks

			// some fallbacks; always use UTF8
			if (str2.find(-1) != std::string::npos)
				str.insert(0, &UTFSign, 1);
			else
				str = str2;

			return str;
		}

		std::string DecodeString(std::string s) {
			if (s.size() > 0 && s[0] == UTFSign)
				return s.substr(1);

			return CP437::Decode(s);
		}

		PlayerInput ParsePlayerInput(uint8_t bits) {
			PlayerInput inp;
			inp.moveForward = (bits & (1 << 0)) != 0;
			inp.moveBackward = (bits & (1 << 1)) != 0;
			inp.moveLeft = (bits & (1 << 2)) != 0;
			inp.moveRight = (bits & (1 << 3)) != 0;
			inp.jump = (bits & (1 << 4)) != 0;
			inp.crouch = (bits & (1 << 5)) != 0;
			inp.sneak = (bits & (1 << 6)) != 0;
			inp.sprint = (bits & (1 << 7)) != 0;
			return inp;
		}
		WeaponInput ParseWeaponInput(uint8_t bits) {
			WeaponInput inp;
			inp.primary = ((bits & (1 << 0)) != 0);
			inp.secondary = ((bits & (1 << 1)) != 0);
			return inp;
		}

		// --- Plan 02 Task 1: simple/fixed-width + dual-ordinal packets ---
		//
		// Each body replicates the EXACT primitive call sequence of the cited NetClient
		// recv (HandleGamePacket) / send (Send*) code so the wire bytes are unchanged.

		// PositionData(0) — recv :784, send :1553-1554
		PositionDataPacket DecodePositionData(NetPacketReader& r) {
			PositionDataPacket p;
			p.position = r.ReadVector3();
			return p;
		}
		NetPacketWriter EncodePositionData(const PositionDataPacket& p) {
			NetPacketWriter w(PacketTypePositionData);
			w.WriteVector3(p.position);
			return w;
		}

		// OrientationData(1) — recv :790, send :1561-1562
		OrientationDataPacket DecodeOrientationData(NetPacketReader& r) {
			OrientationDataPacket p;
			p.orientation = r.ReadVector3();
			return p;
		}
		NetPacketWriter EncodeOrientationData(const OrientationDataPacket& p) {
			NetPacketWriter w(PacketTypeOrientationData);
			w.WriteVector3(p.orientation);
			return w;
		}

		// InputData(3) — recv :841-842, send :1585-1586 (raw bits, ParsePlayerInput separate)
		InputDataPacket DecodeInputData(NetPacketReader& r) {
			InputDataPacket p;
			p.playerId = r.ReadByte();
			p.bits = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeInputData(const InputDataPacket& p) {
			NetPacketWriter w(PacketTypeInputData);
			w.WriteByte(p.playerId);
			w.WriteByte(p.bits);
			return w;
		}

		// WeaponInput(4) — recv :858-859, send :1601-1602
		WeaponInputPacket DecodeWeaponInput(NetPacketReader& r) {
			WeaponInputPacket p;
			p.playerId = r.ReadByte();
			p.bits = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeWeaponInput(const WeaponInputPacket& p) {
			NetPacketWriter w(PacketTypeWeaponInput);
			w.WriteByte(p.playerId);
			w.WriteByte(p.bits);
			return w;
		}

		// HitPacket(5, C2S) — send :1609-1618 (targetId, hitType 0..4). Decode added for test.
		HitPacketPacket DecodeHitPacket(NetPacketReader& r) {
			HitPacketPacket p;
			p.targetId = r.ReadByte();
			p.hitType = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeHitPacket(const HitPacketPacket& p) {
			NetPacketWriter w(PacketTypeHitPacket);
			w.WriteByte(p.targetId);
			w.WriteByte(p.hitType);
			return w;
		}

		// SetHP(5, S2C) — recv :869-871. Encode added for test.
		SetHPPacket DecodeSetHP(NetPacketReader& r) {
			SetHPPacket p;
			p.hp = r.ReadByte();
			p.type = r.ReadByte();
			p.source = r.ReadVector3();
			return p;
		}
		NetPacketWriter EncodeSetHP(const SetHPPacket& p) {
			NetPacketWriter w(PacketTypeSetHP);
			w.WriteByte(p.hp);
			w.WriteByte(p.type);
			w.WriteVector3(p.source);
			return w;
		}

		// GrenadePacket(6) — recv :878-881, send :1626-1629
		GrenadePacket DecodeGrenade(NetPacketReader& r) {
			GrenadePacket p;
			p.playerId = r.ReadByte();
			p.fuse = r.ReadFloat();
			p.position = r.ReadVector3();
			p.velocity = r.ReadVector3();
			return p;
		}
		NetPacketWriter EncodeGrenade(const GrenadePacket& p) {
			NetPacketWriter w(PacketTypeGrenadePacket);
			w.WriteByte(p.playerId);
			w.WriteFloat(p.fuse);
			w.WriteVector3(p.position);
			w.WriteVector3(p.velocity);
			return w;
		}

		// SetTool(7) — recv :887-888, send :1637-1645
		SetToolPacket DecodeSetTool(NetPacketReader& r) {
			SetToolPacket p;
			p.playerId = r.ReadByte();
			p.tool = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeSetTool(const SetToolPacket& p) {
			NetPacketWriter w(PacketTypeSetTool);
			w.WriteByte(p.playerId);
			w.WriteByte(p.tool);
			return w;
		}

		// SetColour(8) — recv :899-900, send :1653-1654. BGR via WriteColor/ReadIntColor.
		SetColourPacket DecodeSetColour(NetPacketReader& r) {
			SetColourPacket p;
			p.playerId = r.ReadByte();
			p.color = r.ReadIntColor();
			return p;
		}
		NetPacketWriter EncodeSetColour(const SetColourPacket& p) {
			NetPacketWriter w(PacketTypeSetColour);
			w.WriteByte(p.playerId);
			w.WriteColor(p.color);
			return w;
		}

		// ShortPlayerData(10) — [ASSUMED] spec-derived layout — no in-repo decode oracle
		// (RESEARCH A1). NetClient :953 raises "Unexpected" and never encodes it; this is a
		// self-consistent codec round-trip only, from the AoS 0.75 spec [id,team,weapon].
		ShortPlayerDataPacket DecodeShortPlayerData(NetPacketReader& r) {
			ShortPlayerDataPacket p;
			p.playerId = r.ReadByte();
			p.team = r.ReadByte();
			p.weapon = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeShortPlayerData(const ShortPlayerDataPacket& p) {
			NetPacketWriter w(PacketTypeShortPlayerData);
			w.WriteByte(p.playerId);
			w.WriteByte(p.team);
			w.WriteByte(p.weapon);
			return w;
		}

		// BlockAction(13) — recv :1054-1056, send :1662-1670
		BlockActionPacket DecodeBlockAction(NetPacketReader& r) {
			BlockActionPacket p;
			p.playerId = r.ReadByte();
			p.action = r.ReadByte();
			p.position = r.ReadIntVector3();
			return p;
		}
		NetPacketWriter EncodeBlockAction(const BlockActionPacket& p) {
			NetPacketWriter w(PacketTypeBlockAction);
			w.WriteByte(p.playerId);
			w.WriteByte(p.action);
			w.WriteIntVector3(p.position);
			return w;
		}

		// BlockLine(14) — recv :1092-1096, send :1678-1680
		BlockLinePacket DecodeBlockLine(NetPacketReader& r) {
			BlockLinePacket p;
			p.playerId = r.ReadByte();
			p.start = r.ReadIntVector3();
			p.end = r.ReadIntVector3();
			return p;
		}
		NetPacketWriter EncodeBlockLine(const BlockLinePacket& p) {
			NetPacketWriter w(PacketTypeBlockLine);
			w.WriteByte(p.playerId);
			w.WriteIntVector3(p.start);
			w.WriteIntVector3(p.end);
			return w;
		}

		// KillAction(16, S2C) — recv :1195-1198. Encode added for test.
		KillActionPacket DecodeKillAction(NetPacketReader& r) {
			KillActionPacket p;
			p.victimId = r.ReadByte();
			p.killerId = r.ReadByte();
			p.killType = r.ReadByte();
			p.respawnTime = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeKillAction(const KillActionPacket& p) {
			NetPacketWriter w(PacketTypeKillAction);
			w.WriteByte(p.victimId);
			w.WriteByte(p.killerId);
			w.WriteByte(p.killType);
			w.WriteByte(p.respawnTime);
			return w;
		}

		// WeaponReload(28) — recv :1431-1437 (full 3-byte form), send :1699-1701
		WeaponReloadPacket DecodeWeaponReload(NetPacketReader& r) {
			WeaponReloadPacket p;
			p.playerId = r.ReadByte();
			p.clip = r.ReadByte();
			p.reserve = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeWeaponReload(const WeaponReloadPacket& p) {
			NetPacketWriter w(PacketTypeWeaponReload);
			w.WriteByte(p.playerId);
			w.WriteByte(p.clip);
			w.WriteByte(p.reserve);
			return w;
		}

		// ChangeTeam(29) — recv :1441-1442, send :1709-1710
		ChangeTeamPacket DecodeChangeTeam(NetPacketReader& r) {
			ChangeTeamPacket p;
			p.playerId = r.ReadByte();
			p.team = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeChangeTeam(const ChangeTeamPacket& p) {
			NetPacketWriter w(PacketTypeChangeTeam);
			w.WriteByte(p.playerId);
			w.WriteByte(p.team);
			return w;
		}

		// ChangeWeapon(30) — recv :1452-1453, send :1718-1719
		ChangeWeaponPacket DecodeChangeWeapon(NetPacketReader& r) {
			ChangeWeaponPacket p;
			p.playerId = r.ReadByte();
			p.weapon = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeChangeWeapon(const ChangeWeaponPacket& p) {
			NetPacketWriter w(PacketTypeChangeWeapon);
			w.WriteByte(p.playerId);
			w.WriteByte(p.weapon);
			return w;
		}

		// MapCached(31, C2S) — send :1731-1732. Decode added for test.
		MapCachedPacket DecodeMapCached(NetPacketReader& r) {
			MapCachedPacket p;
			p.cached = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeMapCached(const MapCachedPacket& p) {
			NetPacketWriter w(PacketTypeMapCached);
			w.WriteByte(p.cached);
			return w;
		}

		// HandShakeInit(31, S2C) — recv :736 (challenge=ReadInt). Encode added for test.
		HandShakeInitPacket DecodeHandShakeInit(NetPacketReader& r) {
			HandShakeInitPacket p;
			p.challenge = r.ReadInt();
			return p;
		}
		NetPacketWriter EncodeHandShakeInit(const HandShakeInitPacket& p) {
			NetPacketWriter w(PacketTypeHandShakeInit);
			w.WriteInt(p.challenge);
			return w;
		}

		// HandShakeReturn(32, C2S) — send :1739-1740. Decode added for test.
		HandShakeReturnPacket DecodeHandShakeReturn(NetPacketReader& r) {
			HandShakeReturnPacket p;
			p.challenge = r.ReadInt();
			return p;
		}
		NetPacketWriter EncodeHandShakeReturn(const HandShakeReturnPacket& p) {
			NetPacketWriter w(PacketTypeHandShakeReturn);
			w.WriteInt(p.challenge);
			return w;
		}

		// PlayerProperties(64, S2C) — recv :1468-1475 (8 bytes). Encode added for test.
		PlayerPropertiesPacket DecodePlayerProperties(NetPacketReader& r) {
			PlayerPropertiesPacket p;
			p.subId = r.ReadByte();
			p.playerId = r.ReadByte();
			p.hp = r.ReadByte();
			p.blocks = r.ReadByte();
			p.grenades = r.ReadByte();
			p.clip = r.ReadByte();
			p.reserve = r.ReadByte();
			p.score = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodePlayerProperties(const PlayerPropertiesPacket& p) {
			NetPacketWriter w(PacketTypePlayerProperties);
			w.WriteByte(p.subId);
			w.WriteByte(p.playerId);
			w.WriteByte(p.hp);
			w.WriteByte(p.blocks);
			w.WriteByte(p.grenades);
			w.WriteByte(p.clip);
			w.WriteByte(p.reserve);
			w.WriteByte(p.score);
			return w;
		}

		// --- Plan 02 Task 2: game-mode-coupled, string, list, signed-field packets ---

		// MoveObject(11, S2C) — recv :959-961. Encode added for test.
		MoveObjectPacket DecodeMoveObject(NetPacketReader& r) {
			MoveObjectPacket p;
			p.type = r.ReadByte();
			p.state = r.ReadByte();
			p.position = r.ReadVector3();
			return p;
		}
		NetPacketWriter EncodeMoveObject(const MoveObjectPacket& p) {
			NetPacketWriter w(PacketTypeMoveObject);
			w.WriteByte(p.type);
			w.WriteByte(p.state);
			w.WriteVector3(p.position);
			return w;
		}

		// ChatMessage(17) — recv :1226-1228, send :1688-1691 (WriteString + trailing NUL;
		// ReadRemainingString strips at .c_str()). cg_unicode-framed string.
		ChatMessagePacket DecodeChatMessage(NetPacketReader& r) {
			ChatMessagePacket p;
			p.playerId = r.ReadByte();
			p.type = r.ReadByte();
			p.message = r.ReadRemainingString();
			return p;
		}
		NetPacketWriter EncodeChatMessage(const ChatMessagePacket& p) {
			NetPacketWriter w(PacketTypeChatMessage);
			w.WriteByte(p.playerId);
			w.WriteByte(p.type);
			w.WriteString(p.message);
			w.WriteByte((uint8_t)0); // trailing NUL (matches SendChat :1691)
			return w;
		}

		// MapStart(18, S2C) — recv :1264 (mapSize=ReadInt). Encode added for test. The v4
		// SendMapCached side-effect is a NetClient behavior, NOT a wire-layout change (D-13),
		// so one round-trip of the single uint32 covers both protocol versions.
		MapStartPacket DecodeMapStart(NetPacketReader& r) {
			MapStartPacket p;
			p.mapSize = r.ReadInt();
			return p;
		}
		NetPacketWriter EncodeMapStart(const MapStartPacket& p) {
			NetPacketWriter w(PacketTypeMapStart);
			w.WriteInt(p.mapSize);
			return w;
		}

		// MapChunk(19) — raw remaining bytes (consumed in DoEvents, not HandleGamePacket).
		// Both directions added for test. No string normalization.
		MapChunkPacket DecodeMapChunk(NetPacketReader& r) {
			MapChunkPacket p;
			p.data = r.ReadRemainingData();
			return p;
		}
		NetPacketWriter EncodeMapChunk(const MapChunkPacket& p) {
			NetPacketWriter w(PacketTypeMapChunk);
			for (char c : p.data)
				w.WriteByte((uint8_t)c);
			return w;
		}

		// PlayerLeft(20, S2P) — recv :1275. Encode added for test.
		PlayerLeftPacket DecodePlayerLeft(NetPacketReader& r) {
			PlayerLeftPacket p;
			p.playerId = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodePlayerLeft(const PlayerLeftPacket& p) {
			NetPacketWriter w(PacketTypePlayerLeft);
			w.WriteByte(p.playerId);
			return w;
		}

		// TerritoryCapture(21, S2P) — recv :1285-1287. Encode added for test.
		TerritoryCapturePacket DecodeTerritoryCapture(NetPacketReader& r) {
			TerritoryCapturePacket p;
			p.territoryId = r.ReadByte();
			p.winning = r.ReadByte();
			p.state = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeTerritoryCapture(const TerritoryCapturePacket& p) {
			NetPacketWriter w(PacketTypeTerritoryCapture);
			w.WriteByte(p.territoryId);
			w.WriteByte(p.winning);
			w.WriteByte(p.state);
			return w;
		}

		// ProgressBar(22, S2P) — recv :1320-1323. rate is SIGNED ((int8_t)ReadByte()).
		// Encode added for test.
		ProgressBarPacket DecodeProgressBar(NetPacketReader& r) {
			ProgressBarPacket p;
			p.territoryId = r.ReadByte();
			p.capturingTeam = r.ReadByte();
			p.rate = (int8_t)r.ReadByte();
			p.progress = r.ReadFloat();
			return p;
		}
		NetPacketWriter EncodeProgressBar(const ProgressBarPacket& p) {
			NetPacketWriter w(PacketTypeProgressBar);
			w.WriteByte(p.territoryId);
			w.WriteByte(p.capturingTeam);
			w.WriteByte((uint8_t)p.rate);
			w.WriteFloat(p.progress);
			return w;
		}

		// IntelCapture(23, S2P) — recv :1364 (playerId), :1376 (winning). Encode for test.
		IntelCapturePacket DecodeIntelCapture(NetPacketReader& r) {
			IntelCapturePacket p;
			p.playerId = r.ReadByte();
			p.winning = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeIntelCapture(const IntelCapturePacket& p) {
			NetPacketWriter w(PacketTypeIntelCapture);
			w.WriteByte(p.playerId);
			w.WriteByte(p.winning);
			return w;
		}

		// IntelPickup(24, S2P) — recv :1392. Encode added for test.
		IntelPickupPacket DecodeIntelPickup(NetPacketReader& r) {
			IntelPickupPacket p;
			p.playerId = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeIntelPickup(const IntelPickupPacket& p) {
			NetPacketWriter w(PacketTypeIntelPickup);
			w.WriteByte(p.playerId);
			return w;
		}

		// IntelDrop(25, S2P) — recv :1411 (playerId), :1416 (pos). Encode added for test.
		IntelDropPacket DecodeIntelDrop(NetPacketReader& r) {
			IntelDropPacket p;
			p.playerId = r.ReadByte();
			p.position = r.ReadVector3();
			return p;
		}
		NetPacketWriter EncodeIntelDrop(const IntelDropPacket& p) {
			NetPacketWriter w(PacketTypeIntelDrop);
			w.WriteByte(p.playerId);
			w.WriteVector3(p.position);
			return w;
		}

		// Restock(26, S2P) — recv :1420 (playerId skipped). Encode added for test.
		RestockPacket DecodeRestock(NetPacketReader& r) {
			RestockPacket p;
			p.playerId = r.ReadByte();
			return p;
		}
		NetPacketWriter EncodeRestock(const RestockPacket& p) {
			NetPacketWriter w(PacketTypeRestock);
			w.WriteByte(p.playerId);
			return w;
		}

		// FogColour(27, S2C) — recv :1426 (alpha skipped), :1427 (BGR color). Encode for test.
		FogColourPacket DecodeFogColour(NetPacketReader& r) {
			FogColourPacket p;
			p.alpha = r.ReadByte();
			p.color = r.ReadIntColor();
			return p;
		}
		NetPacketWriter EncodeFogColour(const FogColourPacket& p) {
			NetPacketWriter w(PacketTypeFogColour);
			w.WriteByte(p.alpha);
			w.WriteColor(p.color);
			return w;
		}

		// VersionGet(33) — recv :738-744 (empty = simple; else list of property ids to end).
		// Both directions added for test.
		VersionGetPacket DecodeVersionGet(NetPacketReader& r) {
			VersionGetPacket p;
			while (r.GetNumRemainingBytes())
				p.propertyIds.push_back(r.ReadByte());
			return p;
		}
		NetPacketWriter EncodeVersionGet(const VersionGetPacket& p) {
			NetPacketWriter w(PacketTypeVersionGet);
			for (uint8_t id : p.propertyIds)
				w.WriteByte(id);
			return w;
		}

		// VersionSend(34, C2S) — send :1756-1760 (tag 'o', major, minor, patch, osInfo).
		// Decode added for test.
		VersionSendPacket DecodeVersionSend(NetPacketReader& r) {
			VersionSendPacket p;
			p.tag = r.ReadByte();
			p.major = r.ReadByte();
			p.minor = r.ReadByte();
			p.patch = r.ReadByte();
			p.osInfo = r.ReadRemainingString();
			return p;
		}
		NetPacketWriter EncodeVersionSend(const VersionSendPacket& p) {
			NetPacketWriter w(PacketTypeVersionSend);
			w.WriteByte(p.tag);
			w.WriteByte(p.major);
			w.WriteByte(p.minor);
			w.WriteByte(p.patch);
			w.WriteString(p.osInfo);
			return w;
		}

		// ExtensionInfo(60) — recv :756-759 (count then count×{id,version}), send :1770-1773.
		ExtensionInfoPacket DecodeExtensionInfo(NetPacketReader& r) {
			ExtensionInfoPacket p;
			int count = r.ReadByte();
			for (int i = 0; i < count; i++) {
				ExtensionInfoEntry e;
				e.id = r.ReadByte();
				e.version = r.ReadByte();
				p.extensions.push_back(e);
			}
			return p;
		}
		NetPacketWriter EncodeExtensionInfo(const ExtensionInfoPacket& p) {
			NetPacketWriter w(PacketTypeExtensionInfo);
			w.WriteByte((uint8_t)p.extensions.size());
			for (const auto& e : p.extensions) {
				w.WriteByte(e.id);
				w.WriteByte(e.version);
			}
			return w;
		}

		// --- Plan 03: the four HARD packets ---

		// WorldUpdate(2, S2C) — recv :797-836. VERSION-BRANCHED (D-08, D-13).
		// v3: 24 B/entry, index implicit (idx=i). v4: 25 B/entry, leading ReadByte index.
		// Entry count mirrors NetClient :804 EXACTLY: GetLength()/bytesPerEntry. GetLength()
		// includes the 1-byte type tag, but integer division drops the +1 (e.g. (1+N*24)/24
		// == N), so the count equals the number of entries the writer emitted. The codec does
		// NOT replicate the idx range-check SPRaise, NaN asserts, RepositionPlayer, or the
		// savedPlayerPos/Front writes — those stay in NetClient (D-09, Plan 04).
		WorldUpdatePacket DecodeWorldUpdate(NetPacketReader& r, int protocolVersion) {
			WorldUpdatePacket p;
			int bytesPerEntry = 24;
			if (protocolVersion == 4)
				bytesPerEntry++;

			int entries = static_cast<int>(r.GetLength() / bytesPerEntry);
			for (int i = 0; i < entries; i++) {
				WorldUpdateEntry e;
				e.index = (protocolVersion == 4) ? r.ReadByte() : (uint8_t)i;
				e.position = r.ReadVector3();
				e.front = r.ReadVector3();
				p.entries.push_back(e);
			}
			return p;
		}
		NetPacketWriter EncodeWorldUpdate(const WorldUpdatePacket& p, int protocolVersion) {
			NetPacketWriter w(PacketTypeWorldUpdate);
			for (const auto& e : p.entries) {
				if (protocolVersion == 4)
					w.WriteByte(e.index); // 0.76 leading per-entry index byte
				w.WriteVector3(e.position);
				w.WriteVector3(e.front);
			}
			return w;
		}

		// StateData(15, S2C) — recv :1113-1192. DISCRIMINATED CTF/TC (D-10).
		// Reproduces the conditional intel-slot layout EXACTLY, including the team-2-first
		// read order (:1154-1166) and the carrierId + ReadData(11) skip per carried-intel
		// team. Encode emits carrierId + 11 zero bytes for a carried team so Decode's
		// ReadData(11) lands correctly and does not over-read into basePos (T-3-03). The
		// codec does NOT build World teams or the CTF/TC game-mode object (NetClient, Plan 04).
		StateDataPacket DecodeStateData(NetPacketReader& r) {
			StateDataPacket p;
			p.playerId = r.ReadByte();
			p.fogColor = r.ReadIntColor();
			p.teamColor[0] = r.ReadIntColor();
			p.teamColor[1] = r.ReadIntColor();
			p.teamName[0] = r.ReadString(10);
			p.teamName[1] = r.ReadString(10);
			p.mode = r.ReadByte();
			if (p.mode == 0) { // CTF (CTFGameMode::m_CTF)
				p.ctfTeam1Score = r.ReadByte();
				p.ctfTeam2Score = r.ReadByte();
				p.ctfCaptureLimit = r.ReadByte();
				p.ctfIntelFlags = r.ReadByte();
				bool team1HasIntel = (p.ctfIntelFlags & 1) != 0;
				bool team2HasIntel = (p.ctfIntelFlags & 2) != 0;

				// team-2 intel slot is read FIRST (matches NetClient :1154-1159)
				if (team2HasIntel) {
					p.ctfTeam2CarrierId = r.ReadByte();
					r.ReadData(11);
				} else {
					p.ctfTeam1FlagPos = r.ReadVector3();
				}

				// then the team-1 intel slot (:1161-1166)
				if (team1HasIntel) {
					p.ctfTeam1CarrierId = r.ReadByte();
					r.ReadData(11);
				} else {
					p.ctfTeam2FlagPos = r.ReadVector3();
				}

				p.ctfTeam1BasePos = r.ReadVector3();
				p.ctfTeam2BasePos = r.ReadVector3();
			} else { // TC
				int trNum = r.ReadByte();
				for (int i = 0; i < trNum; i++) {
					StateDataPacket::Territory t;
					t.pos = r.ReadVector3();
					t.state = r.ReadByte();
					p.tcTerritories.push_back(t);
				}
			}
			return p;
		}
		NetPacketWriter EncodeStateData(const StateDataPacket& p) {
			NetPacketWriter w(PacketTypeStateData);
			w.WriteByte(p.playerId);
			w.WriteColor(p.fogColor);
			w.WriteColor(p.teamColor[0]);
			w.WriteColor(p.teamColor[1]);
			w.WriteString(p.teamName[0], 10);
			w.WriteString(p.teamName[1], 10);
			w.WriteByte(p.mode);
			if (p.mode == 0) { // CTF
				w.WriteByte(p.ctfTeam1Score);
				w.WriteByte(p.ctfTeam2Score);
				w.WriteByte(p.ctfCaptureLimit);
				w.WriteByte(p.ctfIntelFlags);
				bool team1HasIntel = (p.ctfIntelFlags & 1) != 0;
				bool team2HasIntel = (p.ctfIntelFlags & 2) != 0;

				// team-2 intel slot FIRST — carrierId + 11 zero bytes (the ReadData(11) skip)
				if (team2HasIntel) {
					w.WriteByte(p.ctfTeam2CarrierId);
					for (int i = 0; i < 11; i++)
						w.WriteByte((uint8_t)0);
				} else {
					w.WriteVector3(p.ctfTeam1FlagPos);
				}

				// then the team-1 intel slot
				if (team1HasIntel) {
					w.WriteByte(p.ctfTeam1CarrierId);
					for (int i = 0; i < 11; i++)
						w.WriteByte((uint8_t)0);
				} else {
					w.WriteVector3(p.ctfTeam2FlagPos);
				}

				w.WriteVector3(p.ctfTeam1BasePos);
				w.WriteVector3(p.ctfTeam2BasePos);
			} else { // TC
				w.WriteByte((uint8_t)p.tcTerritories.size());
				for (const auto& t : p.tcTerritories) {
					w.WriteVector3(t.pos);
					w.WriteByte(t.state);
				}
			}
			return w;
		}

		// ExistingPlayer(9, S2C recv shape) — recv :910-916. The codec returns the decoded
		// wire fields ONLY: it does NOT run the weapon/tool validation SPRaise, construct a
		// Player, read savedPlayerPos, or write savedPlayerTeam (those stay in NetClient,
		// Plan 04 — D-09). name via ReadRemainingString (variable-length recv shape, Pitfall
		// 7); SendJoin's fixed-16 WriteString(name,16) is a NetClient detail not modeled here.
		ExistingPlayerPacket DecodeExistingPlayer(NetPacketReader& r) {
			ExistingPlayerPacket p;
			p.playerId = r.ReadByte();
			p.team = r.ReadByte();
			p.weapon = r.ReadByte();
			p.tool = r.ReadByte();
			p.score = r.ReadInt();
			p.color = r.ReadIntColor(); // BGR
			p.name = r.ReadRemainingString();
			return p;
		}
		NetPacketWriter EncodeExistingPlayer(const ExistingPlayerPacket& p) {
			NetPacketWriter w(PacketTypeExistingPlayer);
			w.WriteByte(p.playerId);
			w.WriteByte(p.team);
			w.WriteByte(p.weapon);
			w.WriteByte(p.tool);
			w.WriteInt(p.score);
			w.WriteColor(p.color);
			w.WriteString(p.name); // variable-length recv shape (Pitfall 7)
			return w;
		}

		// CreatePlayer(12, S2C) — recv :997-1001. The recv pos.z-=2.4 spawn adjustment is
		// NetClient state and is NOT applied here — the codec returns the RAW wire position
		// (D-09). Player construction / block-color override / callbacks stay in NetClient.
		CreatePlayerPacket DecodeCreatePlayer(NetPacketReader& r) {
			CreatePlayerPacket p;
			p.playerId = r.ReadByte();
			p.weapon = r.ReadByte();
			p.team = r.ReadByte();
			p.position = r.ReadVector3();
			p.name = r.ReadRemainingString();
			return p;
		}
		NetPacketWriter EncodeCreatePlayer(const CreatePlayerPacket& p) {
			NetPacketWriter w(PacketTypeCreatePlayer);
			w.WriteByte(p.playerId);
			w.WriteByte(p.weapon);
			w.WriteByte(p.team);
			w.WriteVector3(p.position);
			w.WriteString(p.name);
			return w;
		}

	} // namespace client
} // namespace spades
