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

#include "ScriptManager.h"
#include <string>

#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Math.h>
#include <Gui/UIWidgetPainter.h>

namespace spades {
	namespace {
		// Forward the bound `Renderer@`/`Font@` handles (passed as pointers) to the
		// shared C++ painters. A widget's `.as` Render() calls these, so the script
		// UI and the editor draw through the same code.
		void Wrap_PaintButton(client::IRenderer* r, client::IFont* f, const Vector2& pos,
		                      const Vector2& size, const std::string& caption, const Vector2& align,
		                      const std::string& hotKey, const Vector2& hotKeyAlign, bool enabled,
		                      bool hover, bool pressed, bool toggled) {
			if (r && f)
				gui::widgets::PaintButton(*r, *f, pos, size, caption, align, hotKey, hotKeyAlign,
				                          enabled, hover, pressed, toggled);
		}
		void Wrap_PaintSimpleButton(client::IRenderer* r, client::IFont* f, const Vector2& pos,
		                            const Vector2& size, const std::string& caption,
		                            const Vector2& align, const Vector4& textColor,
		                            const Vector4& disabledTextColor, bool enabled, bool hover,
		                            bool pressed, bool toggled) {
			if (r && f)
				gui::widgets::PaintSimpleButton(*r, *f, pos, size, caption, align, textColor,
				                                disabledTextColor, enabled, hover, pressed, toggled);
		}
		void Wrap_PaintCheckBox(client::IRenderer* r, client::IFont* f, const Vector2& pos,
		                        const Vector2& size, const std::string& caption, bool hover,
		                        bool pressed, bool toggled) {
			if (r && f)
				gui::widgets::PaintCheckBox(*r, *f, pos, size, caption, hover, pressed, toggled);
		}
		void Wrap_PaintRadioButton(client::IRenderer* r, client::IFont* f, const Vector2& pos,
		                           const Vector2& size, const std::string& caption, bool enabled,
		                           bool hover, bool pressed, bool toggled) {
			if (r && f)
				gui::widgets::PaintRadioButton(*r, *f, pos, size, caption, enabled, hover, pressed,
				                               toggled);
		}
	} // namespace

	// Exposes the shared widget painters to AngelScript as `spades::ui` free
	// functions. This is additive: the `Button` script type and its API are
	// untouched, so existing widgets and mod overrides keep working.
	class UIWidgetPainterRegistrar : public ScriptObjectRegistrar {
	public:
		UIWidgetPainterRegistrar() : ScriptObjectRegistrar("UIWidgetPainter") {}

		void Register(ScriptManager* manager, Phase phase) override {
			asIScriptEngine* eng = manager->GetEngine();
			int r;
			switch (phase) {
				case PhaseGlobalFunction:
					eng->SetDefaultNamespace("spades::ui");
					r = eng->RegisterGlobalFunction(
					  "void PaintButton(Renderer@, Font@, const Vector2&in, const Vector2&in, "
					  "const string&in, const Vector2&in, const string&in, const Vector2&in, "
					  "bool, bool, bool, bool)",
					  asFUNCTION(Wrap_PaintButton), asCALL_CDECL);
					manager->CheckError(r);
					r = eng->RegisterGlobalFunction(
					  "void PaintSimpleButton(Renderer@, Font@, const Vector2&in, const Vector2&in, "
					  "const string&in, const Vector2&in, const Vector4&in, const Vector4&in, "
					  "bool, bool, bool, bool)",
					  asFUNCTION(Wrap_PaintSimpleButton), asCALL_CDECL);
					manager->CheckError(r);
					r = eng->RegisterGlobalFunction(
					  "void PaintCheckBox(Renderer@, Font@, const Vector2&in, const Vector2&in, "
					  "const string&in, bool, bool, bool)",
					  asFUNCTION(Wrap_PaintCheckBox), asCALL_CDECL);
					manager->CheckError(r);
					r = eng->RegisterGlobalFunction(
					  "void PaintRadioButton(Renderer@, Font@, const Vector2&in, const Vector2&in, "
					  "const string&in, bool, bool, bool, bool)",
					  asFUNCTION(Wrap_PaintRadioButton), asCALL_CDECL);
					manager->CheckError(r);
					eng->SetDefaultNamespace("");
					break;
				default: break;
			}
		}
	};

	static UIWidgetPainterRegistrar registrar;
} // namespace spades
