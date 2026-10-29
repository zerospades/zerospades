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

// The Vulkan Memory Allocator, a third-party single-header library kept exactly as
// it ships (vk_mem_alloc.h), included through here so that its own code isn't held
// to this project's warnings: its interface leaves some pointers without the
// nullability its others carry, and its implementation leaves some fields to their
// default initializers. Including it directly brings those warnings back.

#pragma once

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#include <Draw/Vulkan/vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
