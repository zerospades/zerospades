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

#include <algorithm>
#include <cmath>

#include "ChatWindow.h"
#include <Client/ClientUI.h>
#include <Client/ClientUIHelper.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>
#include <Gui/UI/Widgets/MessageBox.h>
#include <Gui/UI/Widgets/TextViewer.h>

SPADES_SETTING(cg_killfeedHeight);

namespace spades {
	namespace client {
		using gui::ui::Button;
		using gui::ui::CommandHistoryItem;
		using gui::ui::Label;
		using gui::ui::ListViewBase;
		using gui::ui::ListViewModel;
		using gui::ui::MouseButton;
		using gui::ui::SetColorNP;
		using gui::ui::SimpleButton;
		using gui::ui::StringCommonPrefixLength;
		using gui::ui::TextViewer;
		using gui::ui::UIElement;
		using gui::ui::UIManager;

		namespace {
			/** The longest message the composer accepts, in characters. */
			constexpr int kMaxMessageLength = 90;

			/** Space between the centered composer and the frame behind it. */
			constexpr float kCenteredPad = 8.0F;

			/** The color a history line is drawn in: the recorded color, lightened. */
			Vector4 HistoryColor(Vector4 color) {
				color.x = Mix(color.x, 1.0F, 0.5F);
				color.y = Mix(color.y, 1.0F, 0.5F);
				color.z = Mix(color.z, 1.0F, 0.5F);
				color.w = 1.0F;
				return color;
			}

			struct ServerCommandInfo {
				const char* name;
				std::string description; // already translated
			};

			// Commands the server understands. The client only suggests them: they are sent as ordinary chat.
			std::vector<ServerCommandInfo> GetServerCommands() {
				return {
					{"help", _Tr("Client", "List the commands of the server")},
					{"commands", _Tr("Client", "Print all available commands")},
					{"time", _Tr("Client", "Shows the current time limit")},
					{"client", _Tr("Client", "Shows information about your client")},
					{"weapon", _Tr("Client", "Shows what weapon a given player is using")},
					{"intel", _Tr("Client", "Shows who's got the enemy intel")},
					{"accuracy", _Tr("Client", "Shows player's accuracy per weapon")},
					{"ratio", _Tr("Client", "Shows K/D ratio")},
					{"streak", _Tr("Client", "Shows how many kills in a row you got without dying")},
					{"ping", _Tr("Client", "Shows current server latency in milliseconds")},
					{"rules", _Tr("Client", "Show you the server rules")},
					{"medkit", _Tr("Client", "Use a medkit")},
					{"kill", _Tr("Client", "Kill yourself or a given player")},
					{"deaf", _Tr("Client", "Make you or a given player no longer receive chat messages")},
					{"unstick", _Tr("Client", "Unstick yourself or a given player")},
					{"airstrike", _Tr("Client", "Air support! Try it out just like that for more details")},
					{"apoc", _Tr("Client", "Summon an apocalypse on the enemy team")},
					{"squad", _Tr("Client", "Creates or joins a squad, letting you spawn with friends")},
					{"login", _Tr("Client", "Log in if you're staff or a trusted member of this server")},
					{"pm", _Tr("Client", "Send a private message to a given player")},
					{"admin", _Tr("Client", "Send a message to all admins currently online")},
					{"votekick", _Tr("Client", "Start votekick against a player")},
					{"cancel", _Tr("Client", "Cancels a votekick")},

					// may require staff perms
					{"heal", _Tr("Client", "Heal and refill yourself or a given player")},
					{"god", _Tr("Client", "Go into god mode")},
					{"fly", _Tr("Client", "Hold control and press space to fly")},
				};
			}

			/**
			 * Builds the entries that match `input` (the text typed after "/"): the server
			 * commands first, then the settings, up to `maxEntries` in total. When
			 * `totalMatches` is given it receives the number of matches, listed or not.
			 */
			std::vector<CommandFieldEntry> FindEntries(const std::string& input,
			                                           size_t maxEntries,
			                                           size_t* totalMatches = nullptr) {
				std::vector<CommandFieldEntry> result;
				size_t total = 0;

				// built on the first call, after the language is known, and reused afterwards
				static const std::vector<ServerCommandInfo> serverCommands = GetServerCommands();
				for (const ServerCommandInfo& cmd : serverCommands) {
					std::string name = cmd.name;
					if (StringCommonPrefixLength(input, name) != input.size())
						continue;

					total++;
					if (result.size() < maxEntries) {
						CommandFieldEntry e;
						e.name = name;
						e.text = cmd.description;
						e.isServerCommand = true;
						result.push_back(std::move(e));
					}
				}

				std::vector<std::string> names = Settings::GetInstance()->GetAllItemNames();
				for (const std::string& n : names) {
					if (StringCommonPrefixLength(input, n) != input.size())
						continue;

					// one lookup per match, reused for the unknown check and for the value
					Settings::ItemHandle item(n, nullptr);
					if (item.IsUnknown())
						continue;

					total++;
					if (result.size() < maxEntries) {
						CommandFieldEntry e;
						e.name = n;
						e.text = static_cast<std::string>(item);
						result.push_back(std::move(e));
					}
				}

				if (totalMatches)
					*totalMatches = total;
				return result;
			}

			/** One row of the scrollable entry list: the name with its typed prefix, then a part of the value. */
			class CommandFieldListRow : public UIElement {
				std::string name;
				bool isServerCommand;
				std::string valueLine; // the part of the value shown in this row
				bool first;            // only the first row of an entry shows its name
				size_t prefixLength;
				float valueX; // distance from the left edge of the row to the value column

				// measured once, on the first frame, instead of in every frame
				bool prepared = false;
				std::string head;
				std::string tail;
				float headW = 0.0F;

			public:
				CommandFieldListRow(UIManager* manager, const CommandFieldEntry& entry,
				                    const std::string& valueLine, bool first, size_t prefixLength,
				                    float valueX)
				    : UIElement(manager),
				      name(entry.name),
				      isServerCommand(entry.isServerCommand),
				      valueLine(valueLine),
				      first(first),
				      prefixLength(prefixLength),
				      valueX(valueX) {}

				void Render() override {
					client::IFont* font = GetFont();
					if (!font)
						return;
					Vector2 pos = GetScreenPosition();

					// same colors as `CommandFieldConfigValueView`
					const Vector4 matchColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
					const Vector4 nameColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
					const Vector4 valueColor = MakeVector4(0.75F, 0.75F, 1.0F, 1.0F);
					const Vector4 cmdMatchColor = MakeVector4(1.0F, 0.72F, 0.3F, 1.0F);
					const Vector4 cmdNameColor = MakeVector4(1.0F, 0.72F, 0.3F, 0.5F);
					const Vector4 cmdTextColor = MakeVector4(0.85F, 0.75F, 0.6F, 1.0F);

					const bool cmd = isServerCommand;

					if (first) {
						if (!prepared) {
							// the part of the name that was already typed is highlighted
							size_t split = std::min(prefixLength, name.size());
							head = name.substr(0, split);
							tail = name.substr(split);
							headW = std::ceil(font->Measure(head).x);
							prepared = true;
						}
						font->Draw(head, pos, 1.0F, cmd ? cmdMatchColor : matchColor);
						font->Draw(tail, MakeVector2(pos.x + headW, pos.y), 1.0F,
						                 cmd ? cmdNameColor : nameColor);
					}

					font->Draw(valueLine, MakeVector2(pos.x + valueX, pos.y), 1.0F,
					                 cmd ? cmdTextColor : valueColor);
				}
			};

			/**
			 * Supplies the rows of the entry list. The value column is aligned over all matches,
			 * and a value that does not fit continues on the following rows.
			 */
			class CommandFieldListModel : public ListViewModel {
				/** One displayed row: a part of the value of an entry. */
				struct Row {
					size_t entry;
					std::string text;
					bool first;
				};

				UIManager* manager; // weak
				std::vector<CommandFieldEntry> entries;
				size_t prefixLength;
				float valueX = 0.0F;
				std::vector<Row> rows;

			public:
				CommandFieldListModel(UIManager* manager, std::vector<CommandFieldEntry> list,
				                      size_t prefixLength, IFont& font, float columnGap,
				                      float width)
				    : manager(manager), entries(std::move(list)), prefixLength(prefixLength) {
					float maxNameLen = 0.0F;
					for (const CommandFieldEntry& e : entries)
						maxNameLen = std::max(maxNameLen, std::ceil(font.Measure(e.name).x));
					valueX = maxNameLen + columnGap;

					const float valueWidth = std::max(width - valueX, 40.0F);
					for (size_t i = 0; i < entries.size(); i++) {
						std::vector<std::string> lines =
							gui::ui::WrapText(font, entries[i].text, valueWidth);
						for (size_t l = 0; l < lines.size(); l++)
							rows.push_back(Row{i, lines[l], l == 0});
					}
				}

				int GetNumRows() override { return static_cast<int>(rows.size()); }

				Handle<UIElement> CreateElement(int row) override {
					const Row& r = rows[row];
					return Handle<CommandFieldListRow>::New(manager, entries[r.entry], r.text,
					                                        r.first, prefixLength, valueX)
					    .Cast<UIElement>();
				}
			};
		} // namespace

		// -- CommandFieldConfigValueView --

		CommandFieldConfigValueView::CommandFieldConfigValueView(UIManager* manager,
		                                                         std::vector<CommandFieldEntry> list,
		                                                         size_t prefixLength,
		                                                         bool fillBounds,
		                                                         size_t extraCount)
		    : UIElement(manager),
		      entries(std::move(list)),
		      prefixLength(prefixLength),
		      fillBounds(fillBounds),
		      extraCount(extraCount) {}

		void CommandFieldConfigValueView::BuildRows(IFont& font, size_t maxRows) {
			const float pad = 8.0F;
			const float columnGap = 20.0F;

			rows.clear();
			moreText.clear();
			laidOut = true;

			// The element is the widest the box may be: the field for the floating list, the history
			// for the list that replaces it.
			const float widestBoxW = size.x;
			boxW = widestBoxW;

			if (entries.empty()) {
				// the "no match" message
				nameColumnW = 0.0F;
				emptyText = _Tr("Client", "No matching commands or settings");
				if (!fillBounds)
					boxW = std::min(boxW, std::ceil(font.Measure(emptyText).x) + pad * 2.0F);
				return;
			}

			float maxNameLen = 0.0F;
			float maxValueLen = 0.0F;
			for (const CommandFieldEntry& e : entries) {
				maxNameLen = std::max(maxNameLen, std::ceil(font.Measure(e.name).x));
				maxValueLen = std::max(maxValueLen, std::ceil(font.Measure(e.text).x));
			}
			nameColumnW = maxNameLen + columnGap;

			// A value stays on one line unless it is wider than the box. Nothing is ever cut.
			float contentW = fillBounds ? boxW : boxW - pad * 2.0F;
			if (!fillBounds) {
				// The floating box fits its content, like the console list. It only grows to the
				// width of the field when a value is too long for it, and then the value continues
				// on the next rows.
				contentW = std::min(contentW, nameColumnW + maxValueLen);
				boxW = contentW + pad * 2.0F;
			}
			const float valueW = std::max(contentW - nameColumnW, 40.0F);

			auto fill = [&](size_t limit) {
				rows.clear();
				for (size_t i = 0; i < entries.size(); i++) {
					std::vector<std::string> lines =
					    gui::ui::WrapText(font, entries[i].text, valueW);
					// whole entries only, except that the first one is always shown
					if (!rows.empty() && rows.size() + lines.size() > limit)
						break;

					for (size_t l = 0; l < lines.size(); l++) {
						Row row;
						row.entry = i;
						row.text = lines[l];
						row.first = (l == 0);
						if (row.first) {
							size_t split = std::min(prefixLength, entries[i].name.size());
							row.head = entries[i].name.substr(0, split);
							row.tail = entries[i].name.substr(split);
							row.headW = std::ceil(font.Measure(row.head).x);
						}
						rows.push_back(std::move(row));
					}
				}
				if (rows.size() > limit)
					rows.resize(limit);
			};
			// the entries that are not shown, plus the matches that were never listed
			auto countHidden = [&]() {
				size_t shown = rows.empty() ? 0 : rows.back().entry + 1;
				return entries.size() - shown + extraCount;
			};

			fill(maxRows);
			size_t hidden = countHidden();
			if (hidden > 0 && maxRows > 1) {
				// the last row is kept for the "more" line
				fill(maxRows - 1);
				hidden = countHidden();
				moreText = _Tr("Client", "+{0} more", hidden);

				// the floating box must be wide enough for it
				if (!fillBounds)
					boxW = std::min(widestBoxW,
					                std::max(boxW, std::ceil(font.Measure(moreText).x) + pad * 2.0F));
			}
		}

		void CommandFieldConfigValueView::Render() {
			client::IFont* font = GetFont();
			if (!font)
				return;
			client::IRenderer& r = GetManager().GetRenderer();

			const float pad = 8.0F;
			const float rowHeight = 22.0F;

			Vector2 pos = GetScreenPosition();

			if (!laidOut) {
				// The floating list grows upwards from the field, so only the rows that fit
				// above it are kept.
				float room = fillBounds ? size.y : pos.y - 1.0F - pad - 4.0F;
				BuildRows(*font,
				          static_cast<size_t>(std::max(1.0F, std::floor(room / rowHeight))));
			}

			// the "no match" message takes one line
			const float contentH =
			    static_cast<float>(std::max<size_t>(rows.size(), 1) + (moreText.empty() ? 0 : 1)) *
			    rowHeight;

			float textX, textY;
			if (fillBounds) {
				// rows sit at the bottom, next to the composer, like the history lines
				textX = pos.x;
				textY = pos.y + size.y - contentH;
			} else {
				// framed list right above the field, as wide as its content
				const float h = contentH + pad;
				const float top = pos.y - 1.0F - h;
				const float left = pos.x;

				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.9F));
				r.DrawFilledRect(left + 1, top + 1, left + boxW - 1, top + h - 1);
				SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
				r.DrawOutlinedRect(left, top, left + boxW, top + h);

				textX = left + pad;
				textY = top + pad * 0.5F;
			}

			const Vector4 matchColor = MakeVector4(1.0F, 1.0F, 1.0F, 1.0F);
			const Vector4 nameColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
			const Vector4 valueColor = MakeVector4(0.75F, 0.75F, 1.0F, 1.0F);
			const Vector4 cmdMatchColor = MakeVector4(1.0F, 0.72F, 0.3F, 1.0F);
			const Vector4 cmdNameColor = MakeVector4(1.0F, 0.72F, 0.3F, 0.55F);
			const Vector4 cmdTextColor = MakeVector4(0.85F, 0.75F, 0.6F, 1.0F);

			if (entries.empty()) {
				font->Draw(emptyText, MakeVector2(textX, textY), 1.0F, nameColor);
				return;
			}

			const float valueX = textX + nameColumnW;

			for (size_t i = 0; i < rows.size(); i++) {
				const Row& row = rows[i];
				const bool cmd = entries[row.entry].isServerCommand;
				const float y = textY + static_cast<float>(i) * rowHeight;

				if (row.first) {
					font->Draw(row.head, MakeVector2(textX, y), 1.0F,
					                 cmd ? cmdMatchColor : matchColor);
					font->Draw(row.tail, MakeVector2(textX + row.headW, y), 1.0F,
					                 cmd ? cmdNameColor : nameColor);
				}
				font->Draw(row.text, MakeVector2(valueX, y), 1.0F,
				                 cmd ? cmdTextColor : valueColor);
			}

			if (!moreText.empty()) {
				const float y = textY + static_cast<float>(rows.size()) * rowHeight;
				font->Draw(moreText, MakeVector2(textX, y), 1.0F,
				           MakeVector4(1.0F, 1.0F, 1.0F, 0.4F));
			}
		}

		// -- CommandField --

		CommandField::CommandField(UIManager* manager, std::vector<CommandHistoryItem>* history)
		    : gui::ui::FieldWithHistory(manager, history) {}

		void CommandField::OnChanged() {
			gui::ui::FieldWithHistory::OnChanged();

			if (valueView)
				valueView->SetParent(nullptr);

			std::vector<CommandFieldEntry> entries;
			size_t prefixLength = 0;
			size_t extraCount = 0;
			bool active = false;

			const std::string& text = GetText();
			if (text.substr(0, 1) == "/" && text.substr(1, 1) != " ") {
				size_t wsPos = text.find(' ');
				int whitespace = (wsPos == std::string::npos) ? static_cast<int>(text.size())
				                                              : static_cast<int>(wsPos);

				std::string candidates = text.substr(1, whitespace - 1);
				if (candidates.size() > 0) {
					prefixLength = candidates.size();
					// an owner with a scrollable list shows every match, the floating list is limited
					size_t totalMatches = 0;
					entries = FindEntries(candidates, configListChanged ? 1000 : 16, &totalMatches);
					extraCount = totalMatches - entries.size();
					active = true;
				}
			}

			// the owner draws the list in its own layout
			if (configListChanged) {
				configListChanged(entries, prefixLength, active);
				return;
			}

			if (active) {
				valueView = Handle<CommandFieldConfigValueView>::New(
				    &GetManager(), std::move(entries), prefixLength, false, extraCount);
				valueView->SetBounds(AABB2(0.0F, 0.0F, size.x, 0.0F));
				valueView->SetParent(this);
			}
		}

		void CommandField::KeyDown(const std::string& key) {
			if (key == "Tab") {
				const std::string& text = GetText();
				if (text.substr(0, 1) == "/") {
					if (GetSelectionLength() == 0 &&
					    GetSelectionStart() == static_cast<int>(text.size()) &&
					    text.find(' ') == std::string::npos) {
						// completion over the server commands and the settings
						std::string input = text.substr(1);
						std::vector<CommandFieldEntry> matches = FindEntries(input, 1000);
						std::string commonPart;
						bool foundOne = false;
						for (const CommandFieldEntry& e : matches) {
							if (!foundOne) {
								commonPart = e.name;
								foundOne = true;
							}

							size_t commonLen = StringCommonPrefixLength(commonPart, e.name);
							commonPart = commonPart.substr(0, commonLen);
						}

						if (commonPart.size() > input.size()) {
							SetText("/" + commonPart);
							Select(static_cast<int>(GetText().size()), 0);
							// refresh the list: the typed prefix is now the completed name
							OnChanged();
						}
					}
				} else if (tabPressed) {
					// not a command: Tab switches the recipient
					tabPressed();
				}
			} else {
				gui::ui::FieldWithHistory::KeyDown(key);
			}
		}

		// -- ClientChatWindow --

		ClientChatWindow::ClientChatWindow(ClientUI* ui, bool isTeamChat, ChatWindowMode mode)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      isTeamChat(isTeamChat),
		      mode(mode) {
			UIManager* manager = &GetManager();

			// The history measures text while it is built, which happens before this window
			// is attached to the root and could inherit the font from it.
			SetFont(manager->GetRootElement().GetFont());

			if (mode != ChatWindowMode::Centered) {
				BuildPanel();
				return;
			}

			float sw = manager->screenWidth;
			float sh = manager->screenHeight;

			float winW = sw * 0.7F, winH = 66.0F;
			float winX = (sw - winW) * 0.5F;
			float winY = (sh - winH) - 20.0F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.5F);
				label->SetBounds(AABB2(winX - kCenteredPad, winY - kCenteredPad,
				                       winW + kCenteredPad * 2.0F, winH + kCenteredPad * 2.0F));
				AddChild(label.GetPointerOrNull());
				background = label.GetPointerOrNull();
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Send");
				button->hotKeyText = _Tr("Client", "[Enter]");
				button->SetBounds(AABB2(winX + winW - 244.0F, winY + 36.0F, 120.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSay(s); };
				AddChild(button.GetPointerOrNull());
				sayButton = button.GetPointerOrNull();
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("Client", "Cancel");
				button->hotKeyText = _Tr("Client", "[Esc]");
				button->SetBounds(AABB2(winX + winW - 120.0F, winY + 36.0F, 120.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnCancel(s); };
				AddChild(button.GetPointerOrNull());
			}

			// a long message continues on the next row, which is as tall as the text
			IFont* fieldFont = GetFont();
			const float rowPitch = fieldFont ? fieldFont->Measure("A").y : 22.0F;
			BuildField(AABB2(winX, winY, winW, 30.0F), rowPitch);

			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == false);
				button->caption = _Tr("Client", "Global");
				button->SetBounds(AABB2(winX, winY + 36.0F, 70.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSetGlobal(s); };
				AddChild(button.GetPointerOrNull());
				globalButton = button.GetPointerOrNull();
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == true);
				button->caption = _Tr("Client", "Team");
				button->SetBounds(AABB2(winX + 70.0F + 1.0F, winY + 36.0F, 70.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnSetTeam(s); };
				AddChild(button.GetPointerOrNull());
				teamButton = button.GetPointerOrNull();
			}

			{
				std::string switchHint = _Tr("Client", "[Tab] Switch");
				IFont* hintFont = GetFont();
				float switchW = hintFont ? std::ceil(hintFont->Measure(switchHint).x) : 100.0F;

				Handle<Label> label = Handle<Label>::New(manager);
				label->text = switchHint;
				label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
				label->SetBounds(AABB2(winX + 70.0F * 2.0F + 1.0F + 8.0F, winY + 36.0F + 5.0F,
				                       switchW + 2.0F, 20.0F));
				AddChild(label.GetPointerOrNull());
				hintSwitch = label.GetPointerOrNull();
			}

			UpdateState();
		}

		void ClientChatWindow::BuildPanel() {
			UIManager* manager = &GetManager();
			const float sw = manager->screenWidth;
			const float sh = manager->screenHeight;
			const bool docked = (mode == ChatWindowMode::Docked);

			// Share of the screen height taken by the history. The peek view is the extended
			// one: it has no composer to make room for, so it shows more of the history.
			const float dockedHeightRatio = 0.45F;
			const float peekHeightRatio = 0.65F;

			const float margin = 8.0F;    // distance from the screen edge, as in the HUD chat
			const float pad = 8.0F;       // space between the frame and its contents
			const float gap = 6.0F;       // space between the parts of the panel
			const float rowH = 30.0F;     // height of the composer row
			const float headerH = 26.0F;  // height of the channel toggles above the history

			float panelW = std::min(sw - margin * 2.0F, Clamp(sw * 0.38F, 520.0F, 760.0F));
			float contentW = panelW - pad * 2.0F;
			float contentX = margin + pad;

			// whole rows only, so the last row of the history is never cut in half
			client::IFont* font = GetFont();
			float rowHeight = 22.0F;
			float composerY = sh - 20.0F - rowH;
			float historyBottom = composerY - gap;

			// stay clear of the killfeed in the top left corner
			float topLimit = std::min(sh * 0.5F, sh * (float)cg_killfeedHeight * 0.01F + 48.0F);
			float chromeAbove = pad + (docked ? headerH + gap : 0.0F);
			float maxHistoryH = std::max(100.0F, historyBottom - chromeAbove - topLimit);
			float wanted = sh * (docked ? dockedHeightRatio : peekHeightRatio);
			float historyH = std::floor(Clamp(wanted, 100.0F, maxHistoryH) / rowHeight) * rowHeight;
			float historyTop = historyBottom - historyH;
			float headerY = historyTop - gap - headerH;

			frameX = margin;
			frameY = (docked ? headerY : historyTop) - pad;
			frameW = panelW;
			frameH = (docked ? (sh - 12.0F) : (historyBottom + pad)) - frameY;
			headerSeparatorY = historyTop - gap * 0.5F;
			separatorY = historyBottom + gap * 0.5F;

			{
				Handle<Label> label = Handle<Label>::New(manager);
				label->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.8F);
				label->SetBounds(AABB2(frameX, frameY, frameW, frameH));
				AddChild(label.GetPointerOrNull());
			}

			BuildHistory(contentX, historyTop, contentW, historyH, rowHeight, docked);

			if (!docked)
				return;

			// channel toggles
			const float toggleW = 70.0F;
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == false);
				button->caption = _Tr("Client", "Global");
				button->SetBounds(AABB2(contentX, headerY, toggleW, headerH));
				button->activated = [this](UIElement& s) { OnSetGlobal(s); };
				AddChild(button.GetPointerOrNull());
				globalButton = button.GetPointerOrNull();
			}
			{
				Handle<SimpleButton> button = Handle<SimpleButton>::New(manager);
				button->toggle = true;
				button->toggled = (isTeamChat == true);
				button->caption = _Tr("Client", "Team");
				button->SetBounds(AABB2(contentX + toggleW + 1.0F, headerY, toggleW, headerH));
				button->activated = [this](UIElement& s) { OnSetTeam(s); };
				AddChild(button.GetPointerOrNull());
				teamButton = button.GetPointerOrNull();
			}

			// draw hints
			if (font) {
				const float toggleEnd = contentX + toggleW * 2.0F + 1.0F;
				const float hintGap = 8.0F;

				// right aligned: the keys of the composer. The label spans all the room right of the
				// toggles, so a longer text grows to the left and only `text` has to change.
				{
					const float left = toggleEnd + hintGap;
					Handle<Label> label = Handle<Label>::New(manager);
					label->text = _Tr("Client", "[Enter] Send  [Esc] Cancel");
					label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
					label->alignment = MakeVector2(1.0F, 0.0F);
					label->SetBounds(AABB2(left, headerY + 4.0F, contentX + contentW - left, 20.0F));
					AddChild(label.GetPointerOrNull());
					hintKeys = label.GetPointerOrNull();
				}

				// next to the toggles it controls
				std::string switchHint = _Tr("Client", "[Tab] Switch");
				float switchW = std::ceil(font->Measure(switchHint).x);
				{
					Handle<Label> label = Handle<Label>::New(manager);
					label->text = switchHint;
					label->textColor = MakeVector4(1.0F, 1.0F, 1.0F, 0.5F);
					label->SetBounds(AABB2(toggleEnd + hintGap, headerY + 4.0F, switchW + 2.0F, 20.0F));
					AddChild(label.GetPointerOrNull());
					hintSwitch = label.GetPointerOrNull();
				}
			}

			// composer
			historyBaseH = historyH;
			separatorBaseY = separatorY;
			BuildField(AABB2(contentX, composerY, contentW, rowH), rowHeight);

			UpdateState();
		}

		void ClientChatWindow::BuildField(const AABB2& bounds, float lineHeight) {
			Handle<CommandField> f = Handle<CommandField>::New(&GetManager(), &ui->GetChatHistory());
			field = f.GetPointerOrNull();
			field->SetBounds(bounds);
			field->placeholder = _Tr("Client", "Type a message, use / for commands.");
			field->maxLength = kMaxMessageLength;
			field->removeNewlines = true;
			// A long message continues on the next row, so the window does not have to be
			// wider. It is only visual: the message is still sent as one line.
			field->wrapLines = true;
			field->lineHeight = lineHeight;
			field->changed = [this](UIElement& s) { OnFieldChanged(s); };
			field->tabPressed = [this]() { SetIsTeamChat(!isTeamChat); };

			// Only a window with a history has a place for the list of matches, which takes the
			// place of the history. Without one, the field shows its own list.
			if (viewer) {
				field->configListChanged = [this](const std::vector<CommandFieldEntry>& entries,
				                                  size_t prefixLength, bool active) {
					OnConfigListChanged(entries, prefixLength, active);
				};
			}
			AddChild(field);

			composerBottom = bounds.max.y;
			composerBaseH = bounds.max.y - bounds.min.y;
			composerRowPitch = lineHeight;
		}

		void ClientChatWindow::BuildHistory(float x, float y, float w, float h, float rowHeight,
		                                    bool interactive) {
			Handle<TextViewer> v = Handle<TextViewer>::New(&GetManager());
			AddChild(v.GetPointerOrNull());
			viewer = v.GetPointerOrNull();

			// A click on the history must not take the keyboard from the composer, and the
			// peek view has no mouse at all. Scrolling, links and selecting text do not need
			// the focus; copying is handled in `HotKey`.
			viewer->acceptsFocus = false;
			viewer->SetSelectionAlwaysVisible(interactive);
			viewer->SetScrollBarVisible(interactive);
			viewer->rowHeight = rowHeight;
			viewer->bottomAligned = true;
			viewer->SetBounds(AABB2(x, y, w, h));

			// Links are drawn in both views, but only the docked one has a mouse to click them
			// with. Every link goes through a confirmation dialog.
			viewer->parseLinks = true;
			if (interactive) {
				viewer->linkActivated = [this](const std::string& url) {
					gui::ConfirmOpenLink(this, url);
				};
			}

			// replay the most recent lines; older ones stay in the chat log window
			const size_t maxReplayed = 150;
			const auto& records = ui->chatLogRecords;
			size_t first = records.size() > maxReplayed ? records.size() - maxReplayed : 0;
			for (size_t i = first; i < records.size(); i++)
				viewer->AddLine(records[i].first, false, HistoryColor(records[i].second));
			viewer->Layout();
			viewer->ScrollToEnd();
		}

		void ClientChatWindow::Record(const std::string& text, Vector4 color) {
			if (viewer)
				viewer->AddLine(text, true, HistoryColor(color));
		}

		void ClientChatWindow::OnConfigListChanged(const std::vector<CommandFieldEntry>& entries,
		                                           size_t prefixLength, bool active) {
			if (configList)
				configList->SetParent(nullptr);
			if (configMessage)
				configMessage->SetParent(nullptr);
			if (!viewer)
				return;

			client::IFont* font = GetFont();

			// the list takes the place of the history while a "/" word is being typed
			viewer->visible = !(active && font);
			if (!active || !font)
				return;

			if (entries.empty()) {
				// nothing matches: say so, in the place of the history
				configMessage = Handle<CommandFieldConfigValueView>::New(&GetManager(), entries,
				                                                         prefixLength, true);
				configMessage->SetBounds(viewer->GetBounds());
				AddChild(configMessage.GetPointerOrNull());
				return;
			}

			const float columnGap = 20.0F;

			configList = Handle<ListViewBase>::New(&GetManager());
			AddChild(configList.GetPointerOrNull());
			// same rows as the history, so the list takes the place of its lines
			configList->rowHeight = viewer->rowHeight;
			configList->bottomAligned = true;

			// Long values continue on the following rows. They are wrapped for the width left
			// by the scroll bar, which is only removed afterwards, so a row is never too wide.
			Handle<CommandFieldListModel> model = Handle<CommandFieldListModel>::New(
			    &GetManager(), entries, prefixLength, *font, columnGap,
			    viewer->size.x - configList->scrollBarWidth);

			// the scroll bar only takes room when some rows do not fit
			const int fitting =
			    static_cast<int>(std::floor(viewer->size.y / configList->rowHeight));
			if (model->GetNumRows() <= fitting)
				configList->scrollBarWidth = 0.0F;
			configList->SetBounds(viewer->GetBounds());
			configList->SetModel(model.GetPointerOrNull());
		}

		void ClientChatWindow::ScrollBy(float delta) {
			// while the entry list replaces the history, the wheel scrolls the list
			if (configList && viewer && !viewer->visible)
				configList->MouseWheel(delta);
			else if (viewer)
				viewer->MouseWheel(delta);
		}

		void ClientChatWindow::UpdateState() {
			if (field && sayButton)
				sayButton->enable = field->GetText().size() > 0;
			UpdateHints();
		}

		void ClientChatWindow::UpdateComposerLayout() {
			const bool docked = (mode == ChatWindowMode::Docked);
			if (!field || (docked && !viewer))
				return;

			// Each extra row of text makes the composer taller. The docked history gives up that
			// height, so it keeps showing whole rows. At least one history row is left.
			const int rows = std::max(1, field->GetNumTextRows());
			float extra = static_cast<float>(rows - 1) * composerRowPitch;
			if (docked)
				extra = std::min(extra, std::max(0.0F, historyBaseH - viewer->rowHeight));

			const float fieldH = composerBaseH + extra;
			if (std::fabs(field->size.y - fieldH) < 0.5F)
				return;

			// A history that was at its end must stay there when it gets shorter, or its newest
			// lines would be cut off.
			const bool atEnd = docked && viewer->GetTopRowIndex() >= viewer->GetMaxTopRowIndex();

			field->SetBounds(AABB2(field->position.x, composerBottom - fieldH, field->size.x, fieldH));

			if (docked) {
				viewer->SetBounds(AABB2(viewer->position.x, viewer->position.y, viewer->size.x,
				                        historyBaseH - extra));
				if (atEnd) {
					viewer->ScrollToEnd();
					viewer->Layout();
				}
				separatorY = separatorBaseY - extra;
			} else if (background) {
				// the frame keeps its bottom edge and its distance to the composer
				const float bottom = background->position.y + background->size.y;
				const float top = composerBottom - fieldH - kCenteredPad;
				background->SetBounds(
				    AABB2(background->position.x, top, background->size.x, bottom - top));
			}
		}

		void ClientChatWindow::UpdateHints() {
			if (!field || !hintSwitch)
				return;

			// Same rule as `CommandField::KeyDown`: Tab completes the name of a "/" word, and does
			// nothing once the value is being typed. Otherwise it switches the channel.
			TabHint hint = TabHint::Switch;
			const std::string& text = field->GetText();
			if (!text.empty() && text[0] == '/') {
				if (text.find(' ') == std::string::npos)
					hint = TabHint::Autocomplete;
				else
					hint = TabHint::None;
			}

			if (hint == tabHint)
				return;
			tabHint = hint;

			if (hintKeys) {
				// docked: the keys label is right aligned, so only its text changes
				if (hint == TabHint::Autocomplete)
					hintKeys->text = _Tr("Client", "[Tab] Autocomplete  [Enter] Send  [Esc] Cancel");
				else
					hintKeys->text = _Tr("Client", "[Enter] Send  [Esc] Cancel");
				hintSwitch->visible = (hint == TabHint::Switch);
				return;
			}

			// centered: the keys are on the buttons, so only the Tab hint changes
			hintSwitch->visible = (hint != TabHint::None);
			if (hint == TabHint::None)
				return;

			IFont* font = GetFont();
			if (!font)
				return;
			if (hint == TabHint::Autocomplete)
				hintSwitch->text = _Tr("Client", "[Tab] Autocomplete");
			else
				hintSwitch->text = _Tr("Client", "[Tab] Switch");
			const AABB2 b = hintSwitch->GetBounds();
			const float width = std::ceil(font->Measure(hintSwitch->text).x) + 2.0F;
			hintSwitch->SetBounds(AABB2(b.min.x, b.min.y, width, b.max.y - b.min.y));
		}

		void ClientChatWindow::SetIsTeamChat(bool value) {
			if (isTeamChat == value)
				return;
			isTeamChat = value;
			if (teamButton)
				teamButton->toggled = isTeamChat;
			if (globalButton)
				globalButton->toggled = !isTeamChat;
			UpdateState();
		}

		void ClientChatWindow::OnSetGlobal(UIElement&) { SetIsTeamChat(false); }
		void ClientChatWindow::OnSetTeam(UIElement&) { SetIsTeamChat(true); }
		void ClientChatWindow::OnFieldChanged(UIElement&) {
			// before the entry list is laid out over the history, which copies its bounds
			UpdateComposerLayout();
			UpdateState();
		}

		void ClientChatWindow::Close() { ui->SetActiveUI(nullptr); }

		void ClientChatWindow::OnCancel(UIElement&) {
			field->Cancelled();
			Close();
		}

		bool ClientChatWindow::CheckAndSetConfigVariable(const std::string& text) {
			if (text.substr(0, 1) != "/")
				return false;
			size_t idxPos = text.find(' ');
			int idx = (idxPos == std::string::npos) ? -1 : static_cast<int>(idxPos);
			if (idx < 2)
				return false;

			// find variable
			std::string varname = text.substr(1, idx - 1);
			std::vector<std::string> vars = Settings::GetInstance()->GetAllItemNames();

			for (const std::string& v : vars) {
				if (v.size() == varname.size() &&
				    StringCommonPrefixLength(v, varname) == v.size()) {
					// match
					std::string val = text.substr(idx + 1);
					Settings::ItemHandle item(v, nullptr);
					item = val;
					return true;
				}
			}

			return false;
		}

		void ClientChatWindow::OnSay(UIElement&) {
			field->CommandSent();
			if (!CheckAndSetConfigVariable(field->GetText())) {
				if (isTeamChat)
					helper->SayTeam(field->GetText());
				else
					helper->SayGlobal(field->GetText());
			}

			Close();
		}

		void ClientChatWindow::Render() {
			UIElement::Render();

			if (mode == ChatWindowMode::Centered)
				return;

			UIManager& manager = GetManager();

			// A dialog opened from a link disables this window, which drops the focus. Give
			// it back to the composer once the window is usable again.
			if (mode == ChatWindowMode::Docked && field && IsEnabled() &&
			    manager.GetActiveElement() == nullptr)
				manager.SetActiveElement(field);

			client::IRenderer& r = manager.GetRenderer();
			Vector2 pos = GetScreenPosition();

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawOutlinedRect(pos.x + frameX, pos.y + frameY, pos.x + frameX + frameW,
			                   pos.y + frameY + frameH);
			if (mode == ChatWindowMode::Docked) {
				r.DrawImage(nullptr, AABB2(pos.x + frameX + 8.0F, pos.y + headerSeparatorY,
				                           frameW - 16.0F, 1.0F));
				r.DrawImage(nullptr,
				            AABB2(pos.x + frameX + 8.0F, pos.y + separatorY, frameW - 16.0F, 1.0F));
			}
		}

		void ClientChatWindow::HotKey(const std::string& key) {
			if (!field) {
				UIElement::HotKey(key);
				return;
			}

			// The composer keeps the keyboard focus, so its own Ctrl+C has already run and
			// copied nothing when nothing is selected in it. Copy the history selection then.
			UIManager& manager = GetManager();
			if (IsEnabled() && viewer && (manager.isControlPressed || manager.isMetaPressed) &&
			    key == "C" && viewer->HasSelection() && field->GetSelectionLength() == 0) {
				manager.Copy(viewer->GetSelectedText());
				return;
			}

			if (IsEnabled() && key == "Escape") {
				OnCancel(*this);
			} else if (IsEnabled() && key == "Enter") {
				if (field->GetText().size() > 0)
					OnSay(*this);
			} else {
				UIElement::HotKey(key);
			}
		}
	} // namespace client
} // namespace spades
