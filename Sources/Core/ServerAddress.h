/*
 Copyright (c) 2013 yvt, learn_more

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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#pragma once

#include <cstdint>
#include <string>

struct _ENetAddress;
typedef _ENetAddress ENetAddress;

namespace spades {
	enum class ProtocolVersion { Unknown = 0, v075 = 3, v076 = 4 };
	class ServerAddress {
		std::string mAddress;
		ProtocolVersion mVersion;

		static std::string StripProtocol(const std::string& address);

	public:
		explicit ServerAddress(std::string address = "",
		                       ProtocolVersion version = ProtocolVersion::Unknown);

		ENetAddress GetENetAddress() const;
		ProtocolVersion GetProtocolVersion() const { return mVersion; }
		std::string ToString(bool includeProtocol = true) const;

		/** Returns the resolved host as a dotted-decimal IPv4 string, e.g. "1.2.3.4". */
		std::string GetIPString() const;

		static uint32_t ParseIntegerAddress(const std::string& str);
	};
} // namespace spades