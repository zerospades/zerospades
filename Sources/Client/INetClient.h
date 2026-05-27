/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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
#include <memory>
#include <string>

#include "GameConstants.h"
#include "Player.h"
#include <Core/Math.h>

namespace spades {
	namespace client {
		struct GameProperties;
		struct PlayerInput;
		struct WeaponInput;
		class Grenade;

		enum NetClientStatus {
			NetClientStatusNotConnected = 0,
			NetClientStatusConnecting,
			NetClientStatusReceivingMap,
			NetClientStatusConnected
		};

		/**
		 * Common interface shared by NetClient (live network) and DemoNetClient (demo playback).
		 * Client holds a single INetClient* and no longer needs to branch on isDemoMode for any
		 * method that is present here.
		 */
		class INetClient {
		public:
			virtual ~INetClient() = default;

			// ── Status ──────────────────────────────────────────────────────
			virtual NetClientStatus GetStatus() = 0;
			virtual std::string GetStatusString() = 0;
			virtual float GetMapReceivingProgress() = 0;
			virtual const std::shared_ptr<GameProperties>& GetGameProperties() = 0;

			// ── Event loop ──────────────────────────────────────────────────
			// dt is the frame delta-time in seconds. NetClient uses it to pick a poll timeout;
			// DemoNetClient uses it to advance playback time.
			virtual void DoEvents(float dt) = 0;

			// ── Network stats (stubs return 0 in demo mode) ─────────────────
			virtual int GetPing() = 0;
			virtual float GetPacketLoss() = 0;
			virtual float GetPacketThrottle() = 0;
			virtual double GetDownlinkBps() = 0;
			virtual double GetUplinkBps() = 0;

			// ── Connection ──────────────────────────────────────────────────
			virtual void Disconnect() = 0;

			// ── Send methods (no-ops in demo mode) ──────────────────────────
			virtual void SendJoin(int team, WeaponType, std::string name, int score) = 0;
			virtual void SendPosition(Vector3) = 0;
			virtual void SendOrientation(Vector3) = 0;
			virtual void SendPlayerInput(PlayerInput) = 0;
			virtual void SendWeaponInput(WeaponInput) = 0;
			virtual void SendHit(int targetPlayerId, HitType) = 0;
			virtual void SendGrenade(const Grenade&) = 0;
			virtual void SendTool() = 0;
			virtual void SendHeldBlockColor() = 0;
			virtual void SendBlockAction(IntVector3, BlockActionType) = 0;
			virtual void SendBlockLine(IntVector3 v1, IntVector3 v2) = 0;
			virtual void SendChat(std::string, bool global) = 0;
			virtual void SendReload() = 0;
			virtual void SendTeamChange(int team) = 0;
			virtual void SendWeaponChange(WeaponType) = 0;

		};

	} // namespace client
} // namespace spades
