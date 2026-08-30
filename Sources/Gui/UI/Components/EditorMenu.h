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

#include <Core/Math.h>
#include <Core/RefCountedObject.h>
#include "IModalMenu.h"

namespace spades {
    namespace client {
        class IRenderer;
        class FontManager;
        class IAudioDevice;
    } // namespace client
    namespace gui {
        class SoftwareCursor;

        class IEditorMenuHost {
        public:
            virtual ~IEditorMenuHost() = default;
            virtual std::string GetMenuTitle() = 0;
            virtual std::string GetDocumentPath() = 0;
            virtual std::string GetDocumentExtension() = 0;
            virtual void SaveDocument(const std::string& path) = 0;
            virtual void RequestClose() = 0;
            virtual bool OnMenuEscape() { return false; }
        };

        class EditorMenu : public IModalMenu {
        public:
            EditorMenu(IEditorMenuHost& host, client::IRenderer& renderer,
                      client::FontManager& fontManager, SoftwareCursor& cursor,
                      client::IAudioDevice* audioDevice);

            // IModalMenu implementation
            bool IsActive() const override { return menuOpen || promptOpen; }
            void Open() override;
            void Close() override;
            bool KeyEvent(const std::string& key, bool down) override;
            void TextInputEvent(const std::string& text) override;
            bool AcceptsTextInput() const override { return promptOpen; }
            void Draw() override;

            // EditorMenu-specific
            void OpenTextPrompt(const std::string& title, const std::string& initial,
                                std::function<void(const std::string&)> onSubmit);
            AABB2 GetTextInputRect() const;

        private:
            IEditorMenuHost& host;
            Handle<client::IRenderer> renderer;
            Handle<client::FontManager> fontManager;
            SoftwareCursor& cursor;
            Handle<client::IAudioDevice> audioDevice;

            bool menuOpen = false;
            int selectedItem = 0;
            int prevSelectedItem = -1;

            bool promptOpen = false;
            std::string promptTitle;
            std::string promptText;
            std::function<void(const std::string&)> promptSubmit;

            void DrawMenu(float sw, float sh);
            void DrawPrompt(float sw, float sh);
            void SubmitPrompt();
            int MenuButtonAt(const Vector2& p) const;
        };
    } // namespace gui
} // namespace spades
