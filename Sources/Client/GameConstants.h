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

#define FALL_SLOW_DOWN 0.24F
#define FALL_DAMAGE_VELOCITY 0.58F
#define FALL_DAMAGE_SCALAR 4096
#define BOUNCE_SOUND_THRESHOLD 0.1F
#define TC_CAPTURE_DISTANCE 16.0F
#define TC_CAPTURE_RATE 0.05F
#define NEUTRAL_TEAM 2

#define FOG_DISTANCE 128
#define FOG_DISTANCE_SQ (FOG_DISTANCE * FOG_DISTANCE)

#define TRACER_CULL_DIST (FOG_DISTANCE + 10)
#define TRACER_CULL_DIST_SQ (TRACER_CULL_DIST * TRACER_CULL_DIST)

#define PARTICLE_BOUNCE_DIST 16
#define PARTICLE_BOUNCE_DIST_SQ (PARTICLE_BOUNCE_DIST * PARTICLE_BOUNCE_DIST)

#define CASING_BOUNCE_DIST 40
#define CASING_BOUNCE_DIST_SQ (CASING_BOUNCE_DIST * CASING_BOUNCE_DIST)

#define MELEE_DISTANCE 3
#define MAX_BLOCK_DISTANCE 3
#define MAX_DIG_DISTANCE 3

#define CHATPREFIX_BIG	   "C% "
#define CHATPREFIX_NOTICE  "N% "
#define CHATPREFIX_WARNING "%% "
#define CHATPREFIX_ERROR   "!% "

enum ChatType {
	ChatTypeAll = 0,
	ChatTypeTeam,
	ChatTypeSystem,

	// only through MessageTypes extension
	ChatTypeBig,
	ChatTypeInfo,
	ChatTypeWarning,
	ChatTypeError,

	// only through the ExtendedTeamplay extension: a line the server sends to one
	// player alone, the sender carried by the packet's own player id
	ChatTypeDirect
};

enum WeaponType { RIFLE_WEAPON, SMG_WEAPON, SHOTGUN_WEAPON };

enum BlockActionType {
	BlockActionCreate,
	BlockActionTool, // gun and spade
	BlockActionDig,
	BlockActionGrenade
};

// "Hit Packet" and weapon damage query
enum HitType {
	HitTypeTorso,
	HitTypeHead,
	HitTypeArms,
	HitTypeLegs,
	HitTypeMelee,
	HitTypeBlock, // used for block damage query
};

enum HurtType { HurtTypeFall = 0, HurtTypeWeapon };

enum KillType {
	KillTypeWeapon = 0,
	KillTypeHeadshot,
	KillTypeMelee,
	KillTypeGrenade,
	KillTypeFall,
	KillTypeTeamChange,
	KillTypeClassChange
};

// Flags to be used in a raycast.
enum hitTag_t { hit_None = 0, hit_Head = 1, hit_Torso = 2, hit_Legs = 4, hit_Arms = 8 };

static inline hitTag_t& operator|=(hitTag_t& left, const hitTag_t& right) {
	left = static_cast<hitTag_t>(static_cast<int>(left) | static_cast<int>(right));
	return left;
}