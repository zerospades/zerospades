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
			/** Items it opens beside it, in place of running anything. */
			std::vector<ContextMenuItem> submenu;

			ContextMenuItem(std::string caption, std::function<void()> run)
			    : caption(std::move(caption)), run(std::move(run)) {}
			/** An item opening `items` beside it: hovered, clicked, or Right. */
			ContextMenuItem(std::string caption, std::vector<ContextMenuItem> items)
			    : caption(std::move(caption)), submenu(std::move(items)) {}
			ContextMenuItem& Enabled(bool on) {
				enabled = on;
				return *this;
			}
			ContextMenuItem& Separated(bool on = true) {
				separated = on;
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
		 * Escape closes it, and the arrows and Enter choose from it. An item
		 * with a submenu opens it beside itself as the pointer comes onto it
		 * (or on a click, Right or Enter); Left or Escape closes it again.
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
			bool IsOpen() const { return !levels.empty(); }
			/** Whether it is open, for `owner`: a dropdown shows itself held
			 *  down while its own list is. */
			bool IsOpenFor(const void* owner) const { return IsOpen() && owner == openedBy; }

			/** A press of either button: one off the menu closes it. */
			void Press(const Vector2& at);
			/** A left release on the item the press was on runs it. */
			void Release(const Vector2& at);
			/** A key down while open: Up, Down, Left, Right, Enter, Escape; all
			 *  are taken. */
			void Key(const std::string& key);

			void Draw(const Vector2& cursor);

		private:
			client::IRenderer& renderer;
			client::IFont& font;
			ui::FeedbackSounds sounds;
			/** The menu, then each submenu open from the one before it. */
			struct Level {
				std::vector<ContextMenuItem> items;
				float x = 0.0F, y = 0.0F, width = 0.0F, height = 0.0F;
				int highlighted = -1;
				int from = -1; // the item of the level before that opened it
			};
			std::vector<Level> levels;
			const void* openedBy = nullptr;
			int pressedLevel = -1, pressed = -1;
			Vector2 lastCursor = MakeVector2(-1.0F, -1.0F);

			/** A level of `items` sized to them, its top left at `at` kept on
			 *  screen; to the left of `leftOf` instead when it runs off the right. */
			Level Lay(std::vector<ContextMenuItem> items, const Vector2& at, float leftOf) const;
			float ItemTop(const Level& level, int index) const;
			int ItemAt(const Level& level, const Vector2& at) const;
			/** The deepest level `at` is on, or -1. */
			int LevelAt(const Vector2& at) const;
			/** Opens item `index`'s submenu of level `depth`, closing deeper ones. */
			void OpenSubmenu(int depth, int index);
			/** Runs the item, or opens its submenu: `byKey` then chooses its
			 *  first item, as the arrows would. */
			void Run(int depth, int index, bool byKey);
			void Step(int direction);
		};
	} // namespace gui
} // namespace spades
