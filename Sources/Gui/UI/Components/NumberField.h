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

#include <string>

#include <Core/Math.h>
#include <Gui/UI/Widgets/TextEntry.h>

#include "InlineTextField.h"

namespace spades {
	namespace client {
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * A number to set, as professional tools have them: dragged sideways
		 * it scrubs the value (Shift finer, Ctrl in steps), clicked it is typed
		 * (arithmetic too: "12*2"), Enter keeps it, Escape gives it up, Tab goes
		 * on to the next one.
		 *
		 * It holds the value only while it is being set: otherwise it shows
		 * what its owner hands it each frame. Its owner routes it input and
		 * acts on what each call returns.
		 */
		class NumberField {
		public:
			/** What a call came to, for the owner to act on. */
			enum class Result {
				None,
				Preview, // the value changed, not yet kept
				Commit,  // keep the value
				Cancel,  // put back what was there
				Next,    // kept, and on to the next field (Tab)
				Previous // kept, and back to the one before (Shift+Tab)
			};
			struct Format {
				int decimals = 2;
				std::string suffix;        // after the value: "°"
				float perPixel = 0.1F;     // what a pixel of scrubbing adds
				float step = 1.0F;         // what Ctrl scrubs in
				float minimum = -1.0e9F;
				float maximum = 1.0e9F;
			};

			NumberField(client::IRenderer& renderer, client::IFont& font, float textScale);

			void SetRect(const AABB2& rect);
			const AABB2& Rect() const { return box; }
			bool Contains(const Vector2& at) const;
			/** The axis it is for, written in its colour on its left: "X". */
			void SetLabel(const std::string& label, const Vector4& color);
			void SetFormat(const Format& format) { this->format = format; }
			/** A disabled field shows its value faded and takes nothing. */
			void SetEnabled(bool enabled) { this->enabled = enabled; }
			bool IsEnabled() const { return enabled; }
			/** What it shows while nothing is being done with it. */
			void SetValue(float value);
			/** The value as being set, while it is. */
			float Value() const { return value; }

			bool IsHeld() const { return held; }
			bool IsScrubbing() const { return scrubbing; }
			bool IsTyping() const { return text.IsOpen(); }

			/** A left press on it: a drag from it scrubs, a release types. */
			void Press(const Vector2& at);
			/** The pointer moved `dx` with the press held. */
			Result Drag(float dx, bool fine, bool inSteps);
			/** The press ends: a scrub is kept, a click starts typing. */
			Result Release(float time);
			/** Gives up a scrub (Escape), the value as it was. */
			Result CancelScrub();

			/** A key down while typing. */
			Result Key(const std::string& key, const TextEntry::Modifiers& modifiers);
			void Insert(const std::string& typed) { text.Insert(typed); }
			void Compose(const std::string& composing) { text.Compose(composing); }
			/** Starts typing, the value all selected (Tab arriving at it). */
			void BeginTyping(float time);
			/** Ends typing as a click elsewhere does: kept when what was typed is
			 *  a number, else given up. */
			Result Settle();
			/** Where it is typed in, for an input method's window. */
			AABB2 TextRect() const { return text.Rect(); }

			void Draw(float time, bool hover);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			float textScale;
			AABB2 box;
			std::string label;
			Vector4 labelColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			Format format;
			bool enabled = true;

			float value = 0.0F;
			float startValue = 0.0F; // when the press or the typing began
			bool held = false, scrubbing = false;
			float travel = 0.0F; // pixels dragged since the press

			InlineTextField text;
			bool typedValid = true;

			std::string Formatted(float v) const;
			float Clamped(float v) const;
			/** What was typed, worked out; false when it is not a number. */
			bool Typed(float& out) const;
		};
	} // namespace gui
} // namespace spades
