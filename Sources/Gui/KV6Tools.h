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

namespace spades {
	/**
	 * `--kv6-tools`: reads and edits KV6 models from the command line, with no
	 * window, for scripts and for agents that cannot drive the editor. Runs
	 * the command in `args` (what follows `--kv6-tools`) and returns the exit
	 * code: 0 done, 1 the command failed, 2 it was not understood.
	 */
	int RunKV6Tools(int argc, char** argv);
} // namespace spades
