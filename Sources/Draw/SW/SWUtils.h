/*
 Copyright (c) 2013 yvt

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

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>

#include <Core/ConcurrentDispatch.h>
#include <Core/Debug.h>
#include <Core/Math.h>

namespace spades {
	namespace draw {
		int GetNumSWRendererThreads();

		template <class F> static void InvokeParallel(F f, unsigned int numThreads) {
			SPAssert(numThreads <= 32);
			std::array<std::unique_ptr<ConcurrentDispatch>, 32> disp;
			for (auto i = 1U; i < numThreads; i++) {
				auto ff = [i, &f]() { f(i); };
				disp[i] = std::unique_ptr<ConcurrentDispatch>(
				  static_cast<ConcurrentDispatch*>(new FunctionDispatch<decltype(ff)>(ff)));
				disp[i]->Start();
			}
			f(0);
			for (auto i = 1U; i < numThreads; i++)
				disp[i]->Join();
		}

		template <class F> static void InvokeParallel2(F f) {
			unsigned int numThreads = static_cast<unsigned int>(GetNumSWRendererThreads());
			numThreads = std::max(numThreads, 1U);
			numThreads = std::min(numThreads, 32U);

			std::array<std::unique_ptr<ConcurrentDispatch>, 32> disp;
			for (auto i = 1U; i < numThreads; i++) {
				auto ff = [i, &f, numThreads]() { f(i, numThreads); };
				disp[i] = std::unique_ptr<ConcurrentDispatch>(
				  static_cast<ConcurrentDispatch*>(new FunctionDispatch<decltype(ff)>(ff)));
				disp[i]->Start();
			}
			f(0, numThreads);
			for (auto i = 1U; i < numThreads; i++)
				disp[i]->Join();
		}

		static inline PURE int ToFixed8(float v) { return Clamp(static_cast<int>(v * 255.0F + 0.5F), 0, 255); }
		static inline PURE int ToFixedFactor8(float v) { return Clamp(static_cast<int>(v * 256.0F + 0.5F), 0, 256); }
		static inline PURE int ToIntClamped(float v, float limit) { return static_cast<int>(Clamp(v, -limit, limit)); }

		/**
		 * What a draw adds to a framebuffer pixel, for colours with premultiplied
		 * alpha: a pixel becomes `colour + destination * (1 - alpha)`.
		 */
		struct PremultipliedColor {
			unsigned int r, g, b, invA;
		};
		static inline PremultipliedColor MakePremultipliedColor(const Vector4& c) {
			PremultipliedColor s;
			s.r = (255U * static_cast<unsigned int>(ToFixedFactor8(c.x))) >> 8;
			s.g = (255U * static_cast<unsigned int>(ToFixedFactor8(c.y))) >> 8;
			s.b = (255U * static_cast<unsigned int>(ToFixedFactor8(c.z))) >> 8;
			s.invA = 256U - static_cast<unsigned int>(ToFixedFactor8(c.w));
			return s;
		}
		static inline void BlendPremultiplied(uint32_t& dest, const PremultipliedColor& s) {
			const uint32_t d = DarkenColor(dest, s.invA);
			const unsigned int r = std::min(s.r + ((d >> 16) & 0xFF), 255U);
			const unsigned int g = std::min(s.g + ((d >> 8) & 0xFF), 255U);
			const unsigned int b = std::min(s.b + (d & 0xFF), 255U);
			dest = (r << 16) | (g << 8) | b;
		}
	} // namespace draw
} // namespace spades