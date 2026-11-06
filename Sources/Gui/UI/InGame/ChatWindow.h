/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

 This file is part of ZeroSpades, a fork of OpenSpades.

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

#include <functional>
#include <string>
#include <vector>

#include <Core/Math.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/FieldWithHistory.h>
#include <Gui/UI/Widgets/ListView.h>

namespace spades {
	namespace gui {
		namespace ui {
			class Label;
			class TextViewer;
		}
	}
	namespace client {
		class ClientUI;
		class ClientUIHelper;
		class IFont;

		/** One row of the list shown while the user types a "/" word. */
		struct CommandFieldEntry {
			std::string name;
			std::string text;
			bool isServerCommand = false;
		};

		/** Shows the settings and server commands that match what the user typed after "/". */
		class CommandFieldConfigValueView : public gui::ui::UIElement {
			/** One displayed row: the first one of an entry also shows its name. */
			struct Row {
				size_t entry = 0;
				std::string text; // the part of the value shown in this row
				bool first = false;
				std::string head; // the part of the name that was already typed
				std::string tail;
				float headW = 0.0F;
			};

			std::vector<CommandFieldEntry> entries;
			size_t prefixLength;
			bool fillBounds;
			size_t extraCount; // matches that are not in `entries`

			// The layout is measured on the first frame and reused by every later one.
			bool laidOut = false;
			std::vector<Row> rows;
			std::string moreText; // empty when every entry is shown
			std::string emptyText; // translated once in BuildRows
			float nameColumnW = 0.0F;
			float boxW = 0.0F;

			void BuildRows(IFont& font, size_t maxRows);

		public:
			/**
			 * `prefixLength` is how many characters of each name the user already typed.
			 * With `fillBounds` the rows fill the bounds of the element (it replaces the chat
			* history), otherwise a framed list is drawn above the element, as wide as its
			* content and no wider than the element: a longer value continues on more rows.
			 */
			CommandFieldConfigValueView(gui::ui::UIManager* manager,
			                            std::vector<CommandFieldEntry> entries, size_t prefixLength,
			                            bool fillBounds, size_t extraCount = 0);
			void Render() override;
		};

		/** Chat input field with "/cvar" and server command completion and value preview. */
		class CommandField : public gui::ui::FieldWithHistory {
			Handle<CommandFieldConfigValueView> valueView;

		public:
			/** Called when Tab is pressed while no "/cvar" completion applies. */
			std::function<void()> tabPressed;

			/**
			 * When set, the owner shows the list of matching entries instead of the field
			 * showing its own floating one. `active` is false when nothing is to be listed.
			 */
			std::function<void(const std::vector<CommandFieldEntry>& entries, size_t prefixLength,
			                   bool active)>
				configListChanged;

			CommandField(gui::ui::UIManager* manager,
						 std::vector<gui::ui::CommandHistoryItem>* history);

			void OnChanged() override;
			void KeyDown(const std::string& key) override;
		};

		/** How a `ClientChatWindow` is laid out. */
		enum class ChatWindowMode {
			Centered, 	/** The composer alone, centered near the bottom of the screen. */
			Docked, 	/** The composer below a scrollable chat history, at the bottom left. */
			Peek 		/** Only the chat history, without input, shown while the peek key is held. */
		};

		/** The floating chat composer (global/team) shown while playing. */
		class ClientChatWindow : public gui::ui::UIElement {
			ClientUI* ui;             // weak
			ClientUIHelper* helper;   // weak
			bool isTeamChat;
			ChatWindowMode mode;

			gui::ui::TextViewer* viewer = nullptr; // weak; owned as a child
			// While a "/" word is typed, one of these replaces the history: the scrollable list of
			// matches, or the message shown when nothing matches.
			Handle<gui::ui::ListViewBase> configList;
			Handle<CommandFieldConfigValueView> configMessage;

			// What Tab does in the composer, and so which hint is shown next to the toggles.
			enum class TabHint { Switch, Autocomplete, None };

			// weak; owned as children. Null when the mode does not show them.
			gui::ui::Label* hintKeys = nullptr;   // docked: the keys of the composer, right aligned
			gui::ui::Label* hintSwitch = nullptr; // the Tab hint next to the toggles
			TabHint tabHint = TabHint::Switch;

			// Frame of the docked and peek views, relative to this window
			float frameX = 0.0F;
			float frameY = 0.0F;
			float frameW = 0.0F;
			float frameH = 0.0F;
			float separatorY = 0.0F;
			float headerSeparatorY = 0.0F;

			// The composer grows upwards while its text wraps onto more rows. These are the values
			// with a single-row composer.
			float composerBottom = 0.0F; // bottom edge of the composer, which stays in place
			float composerBaseH = 0.0F;
			float composerRowPitch = 0.0F; // height added by each further row of text

			// Docked: the history gives up the height the composer grows by.
			float historyBaseH = 0.0F;
			float separatorBaseY = 0.0F;

			// Centered: the frame behind the composer follows its top edge. Weak; owned as a child.
			gui::ui::Label* background = nullptr;

			void BuildPanel();
			void BuildHistory(float x, float y, float w, float h, float rowHeight,
			                  bool interactive);

			/**
			 * Creates the composer in `bounds`. Its bottom edge stays in place while it grows with
			 * its text. `lineHeight` is the distance between two rows of text.
			 */
			void BuildField(const AABB2& bounds, float lineHeight);

			void OnSetGlobal(gui::ui::UIElement& sender);
			void OnSetTeam(gui::ui::UIElement& sender);
			void OnFieldChanged(gui::ui::UIElement& sender);
			void OnCancel(gui::ui::UIElement& sender);
			void OnSay(gui::ui::UIElement& sender);
			void OnConfigListChanged(const std::vector<CommandFieldEntry>& entries,
			                         size_t prefixLength, bool active);
			void UpdateHints();
			void UpdateComposerLayout();

		protected:
			virtual void Close();

		public:
			// weak; owned as children. Null when the mode has no such control.
			CommandField* field = nullptr;
			gui::ui::Button* sayButton = nullptr;
			gui::ui::SimpleButton* teamButton = nullptr;
			gui::ui::SimpleButton* globalButton = nullptr;

			ClientChatWindow(ClientUI* ui, bool isTeamChat,
			                 ChatWindowMode mode = ChatWindowMode::Centered);

			/** Apply a local /setting value command; return false for server commands. */
			static bool CheckAndSetConfigVariable(const std::string& text);
			void UpdateState();

			/** Appends a line to the history of the docked and peek views. */
			void Record(const std::string& text, Vector4 color);

			/** Scrolls the history; positive moves down, like the mouse wheel. */
			void ScrollBy(float delta);

			bool GetIsTeamChat() const { return isTeamChat; }
			void SetIsTeamChat(bool value);

			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace client
} // namespace spades
