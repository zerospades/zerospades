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

#include "EditorMenu.h"
#include "SoftwareCursor.h"

#include <Gui/OverlayPaint.h>
#include <Gui/UIWidgetPainter.h>

#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>
#include <Core/RefCountedObject.h>

namespace spades {
    namespace gui {
        namespace {
            const float kItemW = 260.0F;
            const float kItemH = 36.0F;
            const float kItemPitch = 44.0F;

            /** The start of the codepoint `i` is inside, so that nothing removed
             *  from a string ever leaves half a character behind. */
            size_t CodepointStart(const std::string& s, size_t i) {
                while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80)
                    i--;
                return i;
            }

            /**
             * `text` shortened to fit `width`, losing characters from the middle to
             * an ellipsis. The middle goes rather than the end because both ends of
             * a document name carry something: what it is called, and what type it
             * is and whether it has been saved.
             */
            std::string ElideToWidth(client::IFont& font, std::string text, float width) {
                if (font.Measure(text).x <= width)
                    return text;

                const std::string ellipsis = "...";
                auto elided = [&ellipsis](const std::string& t) {
                    size_t middle = CodepointStart(t, t.size() / 2);
                    return t.substr(0, middle) + ellipsis + t.substr(middle);
                };

                while (text.size() > 1 && font.Measure(elided(text)).x > width) {
                    size_t cut = CodepointStart(text, text.size() / 2);
                    size_t end = cut + 1;
                    while (end < text.size() &&
                           (static_cast<unsigned char>(text[end]) & 0xC0) == 0x80)
                        end++;
                    text.erase(cut, end - cut);
                }
                return elided(text);
            }
        } // namespace

        EditorMenu::EditorMenu(IEditorMenuHost& h, client::IRenderer& r, client::FontManager& fm,
                               SoftwareCursor& c, client::IAudioDevice* ad)
            : host(h), renderer(&r), fontManager(&fm), cursor(c), sounds(ad) {}

        void EditorMenu::Open() {
            RebuildItems();
            menuOpen = true;
            selectedItem = 0;
        }

        void EditorMenu::RebuildItems() {
            items.clear();
            items.push_back(EditorMenuItem{"Resume", [this] { menuOpen = false; }, true});
            for (EditorMenuItem& item : host.GetMenuItems())
                items.push_back(std::move(item));
        }

        void EditorMenu::Activate(int index) {
            if (index < 0 || index >= int(items.size()) || !items[index].enabled)
                return;
            // The command may open a prompt of its own, so close the menu first.
            std::function<void()> run = items[index].run;
            menuOpen = false;
            if (run)
                run();
        }
        void EditorMenu::Close() { menuOpen = false; }

        int EditorMenu::MenuButtonAt(const Vector2& p) const {
            float sw = renderer->ScreenWidth();
            float sh = renderer->ScreenHeight();
            float x = (sw - kItemW) * 0.5F;
            float y = sh * 0.5F - 110.0F + kItemPitch;
            for (int i = 0; i < int(items.size()); i++) {
                if (OverlayInRect(p, x, y, kItemW, kItemH))
                    return i;
                y += kItemPitch;
            }
            return -1;
        }

        void EditorMenu::DrawMenu(float sw, float sh) {
            client::IFont& font = fontManager->GetSmallGuiFont();
            OverlayColorNP(*renderer, MakeVector4(0.0F, 0.0F, 0.0F, 0.7F));
            OverlayFillRect(*renderer, 0, 0, sw, sh);

            float x = (sw - kItemW) * 0.5F;
            float y = sh * 0.5F - 110.0F;

            // The title names the document, so it is as long as a file name can be.
            std::string title = ElideToWidth(font, host.GetMenuTitle(), kItemW);
            Vector2 sz = font.Measure(title);
            font.Draw(title, MakeVector2(x + (kItemW - sz.x) * 0.5F, y), 1.0F,
                      MakeVector4(1, 1, 1, 1));
            y += kItemPitch;

            int hover = MenuButtonAt(cursor.GetPosition());
            if (hover >= 0)
                selectedItem = hover;   // mouse hover and keyboard nav share one selection

            if (selectedItem != prevSelectedItem && selectedItem >= 0) {
                sounds.Hover();
                prevSelectedItem = selectedItem;
            }

            for (int i = 0; i < int(items.size()); i++) {
                widgets::PaintButton(*renderer, font, MakeVector2(x, y),
                                     MakeVector2(kItemW, kItemH), items[i].caption,
                                     MakeVector2(0.5F, 0.5F), "", MakeVector2(1.0F, 0.5F),
                                     items[i].enabled, selectedItem == i, false, false);
                y += kItemPitch;
            }
        }

        bool EditorMenu::KeyEvent(const std::string& key, bool down) {
            if (menuOpen) {
                if (!down)
                    return true;
                if (key == "Escape") { menuOpen = false; return true; }
                int count = int(items.size());
                if (key == "Up") { selectedItem = (selectedItem + count - 1) % count; return true; }
                if (key == "Down") { selectedItem = (selectedItem + 1) % count; return true; }
                if (key == "Enter" || key == "LeftMouseButton") {
                    int b = (key == "LeftMouseButton") ? MenuButtonAt(cursor.GetPosition()) : selectedItem;
                    if (b >= 0 && items[b].enabled)
                        sounds.Activate();
                    Activate(b);
                    return true;
                }
                return true;   // swallow everything else while the menu is open (matches old behavior)
            }

            if (down && key == "Escape") {
                if (host.OnMenuEscape())
                    return true;   // host consumed it (e.g. active tool cancelled its own op)
                Open();
                return true;
            }

            return false;
        }

        void EditorMenu::Draw() {
            float sw = renderer->ScreenWidth();
            float sh = renderer->ScreenHeight();
            if (menuOpen)
                DrawMenu(sw, sh);
        }
    } // namespace gui
} // namespace spades
