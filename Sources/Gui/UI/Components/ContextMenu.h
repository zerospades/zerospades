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

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Framework/FeedbackSounds.h>

namespace spades {
	namespace client {
		class IAudioDevice;
		class IFont;
		class IRenderer;
	} // namespace client
	namespace gui {
		/**
		 * A command of a ContextMenu, built as it reads:
		 *
		 *     ContextMenuItem("Rename", rename).Enabled(!locked).Shortcut("F2")
		 */
		struct ContextMenuItem {
			std::string caption;
			std::function<void()> run;
			/** False draws it greyed out, and it does nothing. */
			bool enabled = true;
			/** A line above it, setting it apart from the items before. */
			bool separated = false;
			/** The key that does the same, shown on its right: "F2". */
			std::string shortcut;
			/** Marked as the one in effect (a mode of several). */
			bool checked = false;

			ContextMenuItem(std::string caption, std::function<void()> run)
			    : caption(std::move(caption)), run(std::move(run)) {}
			ContextMenuItem& Enabled(bool on) {
				enabled = on;
				return *this;
			}
			ContextMenuItem& Separated() {
				separated = true;
				return *this;
			}
			ContextMenuItem& Shortcut(std::string key) {
				shortcut = std::move(key);
				return *this;
			}
			ContextMenuItem& Checked(bool on) {
				checked = on;
				return *this;
			}
		};

		/**
		 * A menu of commands popped up over everything but modal dialogs: at
		 * the cursor by a right press, or under a control that drops one down
		 * (a mode selector). While it is open it owns the pointer and the
		 * keyboard: a click on an item runs it, a click anywhere else or
		 * Escape closes it, and the arrows and Enter choose from it.
		 *
		 * EditorUI owns the one every editor shares; its view routes input to
		 * it first while it is open, and draws it last.
		 */
		class ContextMenu {
		public:
			ContextMenu(client::IRenderer& renderer, client::IFont& font,
			            client::IAudioDevice* audioDevice);

			/** Opens at `at` (kept on screen) with `items`, for `owner` (what
			 *  opened it, to ask IsOpenFor; any address). */
			void Open(const Vector2& at, std::vector<ContextMenuItem> items,
			          const void* owner = nullptr);
			/** Opens under `anchor`, as a dropdown from it, at least as wide. */
			void OpenBelow(const AABB2& anchor, std::vector<ContextMenuItem> items,
			               const void* owner = nullptr);
			void Close();
			bool IsOpen() const { return open; }
			/** Whether it is open, for `owner`: a dropdown shows itself held
			 *  down while its own list is. */
			bool IsOpenFor(const void* owner) const { return open && owner == openedBy; }

			/** A press of either button: one off the menu closes it. */
			void Press(const Vector2& at);
			/** A left release on the item the press was on runs it. */
			void Release(const Vector2& at);
			/** A key down while open: Up, Down, Enter, Escape; all are taken. */
			void Key(const std::string& key);

			void Draw(const Vector2& cursor);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			ui::FeedbackSounds sounds;
			std::vector<ContextMenuItem> items;
			bool open = false;
			const void* openedBy = nullptr;
			float x = 0.0F, y = 0.0F, width = 0.0F, height = 0.0F;
			int highlighted = -1;
			int pressed = -1;
			Vector2 lastCursor = MakeVector2(-1.0F, -1.0F);

			float ItemTop(int index) const;
			int ItemAt(const Vector2& at) const;
			void Run(int index);
			void Step(int direction);
		};
	} // namespace gui
} // namespace spades
