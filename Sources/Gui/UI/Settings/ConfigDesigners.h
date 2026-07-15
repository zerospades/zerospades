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

#include <Gui/UI/Framework/UIElement.h>

namespace spades {
	namespace gui {
		/** Live crosshair preview + Reset/Randomize handlers for the settings screen. */
		class ConfigTarget : public ui::UIElement {
		public:
			ConfigTarget(ui::UIManager* manager) : ui::UIElement(manager) {}

			void OnResetPressed(ui::UIElement& sender);
			void OnRandomizePressed(ui::UIElement& sender);
			void Render() override;
		};

		/** Live scope-crosshair preview + Reset/Randomize handlers. */
		class ConfigScope : public ui::UIElement {
		public:
			ConfigScope(ui::UIManager* manager) : ui::UIElement(manager) {}

			void OnResetPressed(ui::UIElement& sender);
			void OnRandomizePressed(ui::UIElement& sender);
			void Render() override;
		};

		/** Full-screen HUD safe-zone editor with two sliders and a live overlay. */
		class ConfigHUDEdit : public ui::UIElement {
			void OnDoneClicked(ui::UIElement& sender);

		public:
			ui::EventHandler OnDone;

			ConfigHUDEdit(ui::UIManager* manager);

			void HotKey(const std::string& key) override;
			void Render() override;
		};
	} // namespace gui
} // namespace spades
