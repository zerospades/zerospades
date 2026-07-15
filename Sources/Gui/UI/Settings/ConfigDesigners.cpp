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

#include "ConfigDesigners.h"
#include "ConfigWidgets.h"
#include "CrosshairRenderer.h"
#include "Formatters.h"
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Settings.h>
#include <Core/Strings.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/Button.h>
#include <Gui/UI/Widgets/DrawUtils.h>
#include <Gui/UI/Widgets/Label.h>

// Crosshair cvars are registered by the AngelScript view-weapon skin; register
// them (with the same defaults) here too so the C++ settings preview works even
// once the AngelScript UI is gone.
DEFINE_SPADES_SETTING(cg_target, "1");
DEFINE_SPADES_SETTING(cg_targetLines, "1");
DEFINE_SPADES_SETTING(cg_targetColor, "0");
DEFINE_SPADES_SETTING(cg_targetColorR, "255");
DEFINE_SPADES_SETTING(cg_targetColorG, "255");
DEFINE_SPADES_SETTING(cg_targetColorB, "255");
DEFINE_SPADES_SETTING(cg_targetAlpha, "255");
DEFINE_SPADES_SETTING(cg_targetGap, "4");
DEFINE_SPADES_SETTING(cg_targetSizeHorizontal, "5");
DEFINE_SPADES_SETTING(cg_targetSizeVertical, "5");
DEFINE_SPADES_SETTING(cg_targetThickness, "1");
DEFINE_SPADES_SETTING(cg_targetTStyle, "0");
DEFINE_SPADES_SETTING(cg_targetDot, "0");
DEFINE_SPADES_SETTING(cg_targetDotColorR, "255");
DEFINE_SPADES_SETTING(cg_targetDotColorG, "255");
DEFINE_SPADES_SETTING(cg_targetDotColorB, "255");
DEFINE_SPADES_SETTING(cg_targetDotAlpha, "255");
DEFINE_SPADES_SETTING(cg_targetDotThickness, "1");
DEFINE_SPADES_SETTING(cg_targetOutline, "1");
DEFINE_SPADES_SETTING(cg_targetOutlineColorR, "0");
DEFINE_SPADES_SETTING(cg_targetOutlineColorG, "0");
DEFINE_SPADES_SETTING(cg_targetOutlineColorB, "0");
DEFINE_SPADES_SETTING(cg_targetOutlineAlpha, "255");
DEFINE_SPADES_SETTING(cg_targetOutlineThickness, "1");
DEFINE_SPADES_SETTING(cg_targetOutlineRoundedStyle, "0");
DEFINE_SPADES_SETTING(cg_targetDynamic, "1");
DEFINE_SPADES_SETTING(cg_targetDynamicSplitdist, "7");

DEFINE_SPADES_SETTING(cg_pngScope, "0");
DEFINE_SPADES_SETTING(cg_scopeLines, "1");
DEFINE_SPADES_SETTING(cg_scopeColor, "0");
DEFINE_SPADES_SETTING(cg_scopeColorR, "255");
DEFINE_SPADES_SETTING(cg_scopeColorG, "0");
DEFINE_SPADES_SETTING(cg_scopeColorB, "255");
DEFINE_SPADES_SETTING(cg_scopeAlpha, "255");
DEFINE_SPADES_SETTING(cg_scopeGap, "4");
DEFINE_SPADES_SETTING(cg_scopeSizeHorizontal, "5");
DEFINE_SPADES_SETTING(cg_scopeSizeVertical, "5");
DEFINE_SPADES_SETTING(cg_scopeThickness, "1");
DEFINE_SPADES_SETTING(cg_scopeTStyle, "0");
DEFINE_SPADES_SETTING(cg_scopeDot, "0");
DEFINE_SPADES_SETTING(cg_scopeDotColorR, "0");
DEFINE_SPADES_SETTING(cg_scopeDotColorG, "0");
DEFINE_SPADES_SETTING(cg_scopeDotColorB, "0");
DEFINE_SPADES_SETTING(cg_scopeDotAlpha, "255");
DEFINE_SPADES_SETTING(cg_scopeDotThickness, "1");
DEFINE_SPADES_SETTING(cg_scopeOutline, "1");
DEFINE_SPADES_SETTING(cg_scopeOutlineColorR, "0");
DEFINE_SPADES_SETTING(cg_scopeOutlineColorG, "0");
DEFINE_SPADES_SETTING(cg_scopeOutlineColorB, "0");
DEFINE_SPADES_SETTING(cg_scopeOutlineAlpha, "255");
DEFINE_SPADES_SETTING(cg_scopeOutlineThickness, "1");
DEFINE_SPADES_SETTING(cg_scopeOutlineRoundedStyle, "0");
DEFINE_SPADES_SETTING(cg_scopeDynamic, "1");
DEFINE_SPADES_SETTING(cg_scopeDynamicSplitdist, "7");

SPADES_SETTING(cg_hudSafezoneX);
SPADES_SETTING(cg_hudSafezoneY);

namespace spades {
	namespace gui {
		namespace {
			// Resets a config item to its registered default value.
			void ResetToDefault(Settings::ItemHandle& item) {
				item = std::string(item.GetDescriptor().defaultValue);
			}
			int RandInt(int a, int b) { return SampleRandomInt<int>(a, b); }
		} // namespace

		using ui::SetColorNP;

		// -- ConfigTarget --

		void ConfigTarget::OnResetPressed(ui::UIElement&) {
			ResetToDefault(cg_targetLines);
			ResetToDefault(cg_targetColor);
			ResetToDefault(cg_targetColorR);
			ResetToDefault(cg_targetColorG);
			ResetToDefault(cg_targetColorB);
			ResetToDefault(cg_targetAlpha);
			ResetToDefault(cg_targetGap);
			ResetToDefault(cg_targetSizeHorizontal);
			ResetToDefault(cg_targetSizeVertical);
			ResetToDefault(cg_targetThickness);
			ResetToDefault(cg_targetTStyle);
			ResetToDefault(cg_targetDot);
			ResetToDefault(cg_targetDotColorR);
			ResetToDefault(cg_targetDotColorG);
			ResetToDefault(cg_targetDotColorB);
			ResetToDefault(cg_targetDotAlpha);
			ResetToDefault(cg_targetDotThickness);
			ResetToDefault(cg_targetOutline);
			ResetToDefault(cg_targetOutlineColorR);
			ResetToDefault(cg_targetOutlineColorG);
			ResetToDefault(cg_targetOutlineColorB);
			ResetToDefault(cg_targetOutlineAlpha);
			ResetToDefault(cg_targetOutlineThickness);
			ResetToDefault(cg_targetOutlineRoundedStyle);
			ResetToDefault(cg_targetDynamic);
			ResetToDefault(cg_targetDynamicSplitdist);
		}

		void ConfigTarget::OnRandomizePressed(ui::UIElement&) {
			ResetToDefault(cg_targetColor);
			cg_targetColorR = RandInt(0, 255);
			cg_targetColorG = RandInt(0, 255);
			cg_targetColorB = RandInt(0, 255);
			cg_targetAlpha = RandInt(50, 255);
			cg_targetGap = RandInt(1, 10);
			cg_targetSizeHorizontal = RandInt(1, 10);
			cg_targetSizeVertical = static_cast<int>(cg_targetSizeHorizontal);
			cg_targetThickness = RandInt(1, 4);
			cg_targetDot = RandInt(0, 1);
			cg_targetDotColorR = RandInt(0, 255);
			cg_targetDotColorG = RandInt(0, 255);
			cg_targetDotColorB = RandInt(0, 255);
			cg_targetDotAlpha = RandInt(50, 255);
			cg_targetDotThickness = static_cast<int>(cg_targetThickness);
			cg_targetOutline = RandInt(0, 1);
			cg_targetOutlineColorR = RandInt(0, 255);
			cg_targetOutlineColorG = RandInt(0, 255);
			cg_targetOutlineColorB = RandInt(0, 255);
			cg_targetOutlineAlpha = RandInt(50, 255);
			cg_targetOutlineRoundedStyle = RandInt(0, 1);
		}

		void ConfigTarget::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			Vector2 center = pos + sz * 0.5F;

			TargetParam param;
			int targetType = cg_target;

			IntVector3 col;
			switch (static_cast<int>(cg_targetColor)) {
				case 1: col = IntVector3::Make(250, 50, 50); break; // red
				case 2: col = IntVector3::Make(50, 250, 50); break; // green
				case 3: col = IntVector3::Make(50, 50, 250); break; // blue
				case 4: col = IntVector3::Make(250, 250, 50); break; // yellow
				case 5: col = IntVector3::Make(50, 250, 250); break; // cyan
				case 6: col = IntVector3::Make(250, 50, 250); break; // pink
				default: // custom
					col.x = cg_targetColorR;
					col.y = cg_targetColorG;
					col.z = cg_targetColorB;
					break;
			}

			Vector4 color = ConvertColorRGBA(col);
			color.w = Clamp(static_cast<int>(cg_targetAlpha), 0, 255) / 255.0F;

			// draw preview background
			if (static_cast<bool>(cg_target)) {
				float luminosity = color.x + color.y + color.z;
				float opacity = 1.0F - luminosity;
				if (static_cast<bool>(cg_targetOutline) && targetType == 2)
					SetColorNP(r, MakeVector4(0.6F, 0.6F, 0.6F, 0.9F));
				else
					SetColorNP(r, MakeVector4(opacity, opacity, opacity, 0.6F));
			} else {
				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.6F));
			}
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			// draw preview border
			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

			// draw target
			if (targetType == 1) { // draw default target
				Handle<client::IImage> sightImage = r.RegisterImage("Gfx/Sight.tga");
				Vector2 imgSize = MakeVector2(sightImage->GetWidth(), sightImage->GetHeight());
				SetColorNP(r, color);
				r.DrawImage(sightImage, center - (imgSize * 0.5F));
			} else if (targetType == 2) { // draw custom target
				param.lineColor = color;
				param.drawLines = static_cast<bool>(cg_targetLines);
				param.useTStyle = static_cast<bool>(cg_targetTStyle);
				param.lineGap = Clamp(static_cast<float>(cg_targetGap), -10.0F, 10.0F);
				param.lineLength.x = Clamp(static_cast<float>(cg_targetSizeHorizontal), 0.0F, 10.0F);
				param.lineLength.y = Clamp(static_cast<float>(cg_targetSizeVertical), 0.0F, 10.0F);
				param.lineThickness = Clamp(static_cast<float>(cg_targetThickness), 1.0F, 4.0F);

				param.drawDot = static_cast<bool>(cg_targetDot);
				col.x = cg_targetDotColorR;
				col.y = cg_targetDotColorG;
				col.z = cg_targetDotColorB;
				color = ConvertColorRGBA(col);
				color.w = Clamp(static_cast<int>(cg_targetDotAlpha), 0, 255) / 255.0F;
				param.dotColor = color;
				param.dotThickness = Clamp(static_cast<float>(cg_targetDotThickness), 1.0F, 4.0F);

				param.drawOutline = static_cast<bool>(cg_targetOutline);
				param.useRoundedStyle = static_cast<bool>(cg_targetOutlineRoundedStyle);
				col.x = cg_targetOutlineColorR;
				col.y = cg_targetOutlineColorG;
				col.z = cg_targetOutlineColorB;
				color = ConvertColorRGBA(col);
				color.w = Clamp(static_cast<int>(cg_targetOutlineAlpha), 0, 255) / 255.0F;
				param.outlineColor = color;
				param.outlineThickness = Clamp(static_cast<float>(cg_targetOutlineThickness), 1.0F, 4.0F);

				DrawTarget(r, center, param);
			} else {
				client::IFont* font = GetFont();
				if (!font)
					return;
				std::string text = _Tr("Preferences", "No Preview Available.");
				Vector2 txtPos = pos + (sz - font->Measure(text)) * 0.5F;
				font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.5F));
			}
		}

		// -- ConfigScope --

		void ConfigScope::OnResetPressed(ui::UIElement&) {
			ResetToDefault(cg_scopeLines);
			ResetToDefault(cg_scopeColor);
			ResetToDefault(cg_scopeColorR);
			ResetToDefault(cg_scopeColorG);
			ResetToDefault(cg_scopeColorB);
			ResetToDefault(cg_scopeAlpha);
			ResetToDefault(cg_scopeGap);
			ResetToDefault(cg_scopeSizeHorizontal);
			ResetToDefault(cg_scopeSizeVertical);
			ResetToDefault(cg_scopeThickness);
			ResetToDefault(cg_scopeTStyle);
			ResetToDefault(cg_scopeDot);
			ResetToDefault(cg_scopeDotColorR);
			ResetToDefault(cg_scopeDotColorG);
			ResetToDefault(cg_scopeDotColorB);
			ResetToDefault(cg_scopeDotAlpha);
			ResetToDefault(cg_scopeDotThickness);
			ResetToDefault(cg_scopeOutline);
			ResetToDefault(cg_scopeOutlineColorR);
			ResetToDefault(cg_scopeOutlineColorG);
			ResetToDefault(cg_scopeOutlineColorB);
			ResetToDefault(cg_scopeOutlineAlpha);
			ResetToDefault(cg_scopeOutlineThickness);
			ResetToDefault(cg_scopeOutlineRoundedStyle);
			ResetToDefault(cg_scopeDynamic);
			ResetToDefault(cg_scopeDynamicSplitdist);
		}

		void ConfigScope::OnRandomizePressed(ui::UIElement&) {
			ResetToDefault(cg_scopeColor);
			cg_scopeColorR = RandInt(0, 255);
			cg_scopeColorG = RandInt(0, 255);
			cg_scopeColorB = RandInt(0, 255);
			cg_scopeAlpha = RandInt(50, 255);
			cg_scopeGap = RandInt(1, 10);
			cg_scopeSizeHorizontal = RandInt(1, 10);
			cg_scopeSizeVertical = static_cast<int>(cg_scopeSizeHorizontal);
			cg_scopeThickness = RandInt(1, 4);
			cg_scopeDot = RandInt(0, 1);
			cg_scopeDotColorR = RandInt(0, 255);
			cg_scopeDotColorG = RandInt(0, 255);
			cg_scopeDotColorB = RandInt(0, 255);
			cg_scopeDotAlpha = RandInt(50, 255);
			cg_scopeDotThickness = static_cast<int>(cg_scopeThickness);
			cg_scopeOutline = RandInt(0, 1);
			cg_scopeOutlineColorR = RandInt(0, 255);
			cg_scopeOutlineColorG = RandInt(0, 255);
			cg_scopeOutlineColorB = RandInt(0, 255);
			cg_scopeOutlineAlpha = RandInt(50, 255);
			cg_scopeOutlineRoundedStyle = RandInt(0, 1);
		}

		void ConfigScope::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;
			Vector2 center = pos + sz * 0.5F;

			TargetParam param;
			int scopeType = cg_pngScope;

			IntVector3 col;
			col.x = cg_scopeColorR;
			col.y = cg_scopeColorG;
			col.z = cg_scopeColorB;

			Vector4 color = ConvertColorRGBA(col);
			color.w = Clamp(static_cast<int>(cg_scopeAlpha), 0, 255) / 255.0F;

			// draw preview background
			if (scopeType >= 2) {
				float luminosity = color.x + color.y + color.z;
				float opacity = 1.0F - luminosity;
				if (static_cast<bool>(cg_scopeOutline) && scopeType == 3)
					SetColorNP(r, MakeVector4(0.6F, 0.6F, 0.6F, 0.9F));
				else
					SetColorNP(r, MakeVector4(opacity, opacity, opacity, 0.6F));
			} else if (scopeType < 2) {
				SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.6F));
			}
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, sz.y));

			// draw preview border
			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.1F));
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + sz.x, pos.y + sz.y);

			// draw target
			if (scopeType == 2) { // draw dot png scope
				Handle<client::IImage> dotSightImage = r.RegisterImage("Gfx/DotSight.tga");
				Vector2 imgSize =
				    MakeVector2(dotSightImage->GetWidth(), dotSightImage->GetHeight());
				SetColorNP(r, color);
				r.DrawImage(dotSightImage, center - (imgSize * 0.5F));
			} else if (scopeType == 3) { // draw custom target scope
				param.lineColor = color;
				param.drawLines = static_cast<bool>(cg_scopeLines);
				param.useTStyle = static_cast<bool>(cg_scopeTStyle);
				param.lineGap = Clamp(static_cast<float>(cg_scopeGap), -10.0F, 10.0F);
				param.lineLength.x = Clamp(static_cast<float>(cg_scopeSizeHorizontal), 0.0F, 10.0F);
				param.lineLength.y = Clamp(static_cast<float>(cg_scopeSizeVertical), 0.0F, 10.0F);
				param.lineThickness = Clamp(static_cast<float>(cg_scopeThickness), 1.0F, 4.0F);

				param.drawDot = static_cast<bool>(cg_scopeDot);
				col.x = cg_scopeDotColorR;
				col.y = cg_scopeDotColorG;
				col.z = cg_scopeDotColorB;
				color = ConvertColorRGBA(col);
				color.w = Clamp(static_cast<int>(cg_scopeDotAlpha), 0, 255) / 255.0F;
				param.dotColor = color;
				param.dotThickness = Clamp(static_cast<float>(cg_scopeDotThickness), 1.0F, 4.0F);

				param.drawOutline = static_cast<bool>(cg_scopeOutline);
				param.useRoundedStyle = static_cast<bool>(cg_scopeOutlineRoundedStyle);
				col.x = cg_scopeOutlineColorR;
				col.y = cg_scopeOutlineColorG;
				col.z = cg_scopeOutlineColorB;
				color = ConvertColorRGBA(col);
				color.w = Clamp(static_cast<int>(cg_scopeOutlineAlpha), 0, 255) / 255.0F;
				param.outlineColor = color;
				param.outlineThickness = Clamp(static_cast<float>(cg_scopeOutlineThickness), 1.0F, 4.0F);

				DrawTarget(r, center, param);
			} else {
				client::IFont* font = GetFont();
				if (!font)
					return;
				std::string text = _Tr("Preferences", "No Preview Available.");
				Vector2 txtPos = pos + (sz - font->Measure(text)) * 0.5F;
				font->Draw(text, txtPos, 1.0F, MakeVector4(1.0F, 1.0F, 1.0F, 0.5F));
			}
		}

		// -- ConfigHUDEdit --

		ConfigHUDEdit::ConfigHUDEdit(ui::UIManager* manager) : ui::UIElement(manager) {
			float sw = GetManager().screenWidth;
			float sh = GetManager().screenHeight;
			SetBounds(AABB2(0.0F, 0.0F, sw, sh));

			float panelW = 280.0F;
			float panelH = 180.0F;
			float panelX = (sw - panelW) * 0.5F;
			float panelY = (sh - panelH) * 0.5F;

			{
				Handle<ui::Label> bg = Handle<ui::Label>::New(manager);
				bg->backgroundColor = MakeVector4(0.0F, 0.0F, 0.0F, 0.8F);
				bg->SetBounds(AABB2(panelX - 8.0F, panelY - 8.0F, panelW + 16.0F, panelH + 16.0F));
				AddChild(bg.GetPointerOrNull());
			}

			{
				Handle<ui::Label> label = Handle<ui::Label>::New(manager);
				label->text = _Tr("Preferences", "Horizontal Alignment");
				label->alignment = MakeVector2(0.5F, 0.5F);
				label->SetBounds(AABB2(panelX, panelY, panelW, 24.0F));
				AddChild(label.GetPointerOrNull());

				Handle<ConfigSlider> sliderX = Handle<ConfigSlider>::New(
				    manager, "cg_hudSafezoneX", 0.2F, 1.0F, 0.01F, NumberFormatter(2, ""));
				sliderX->SetBounds(AABB2(panelX, panelY + 24.0F, panelW, 24.0F));
				AddChild(sliderX.GetPointerOrNull());
			}

			{
				Handle<ui::Label> label = Handle<ui::Label>::New(manager);
				label->text = _Tr("Preferences", "Vertical Alignment");
				label->alignment = MakeVector2(0.5F, 0.5F);
				label->SetBounds(AABB2(panelX, panelY + 64.0F, panelW, 24.0F));
				AddChild(label.GetPointerOrNull());

				Handle<ConfigSlider> sliderY = Handle<ConfigSlider>::New(
				    manager, "cg_hudSafezoneY", 0.2F, 1.0F, 0.01F, NumberFormatter(2, ""));
				sliderY->SetBounds(AABB2(panelX, panelY + 88.0F, panelW, 24.0F));
				AddChild(sliderY.GetPointerOrNull());
			}

			{
				Handle<ui::Button> doneBtn = Handle<ui::Button>::New(manager);
				doneBtn->caption = _Tr("Preferences", "OK");
				doneBtn->SetBounds(
				    AABB2(panelX + panelW - 80.0F, panelY + panelH - 24.0F, 80.0F, 24.0F));
				doneBtn->activated = [this](ui::UIElement& s) { OnDoneClicked(s); };
				AddChild(doneBtn.GetPointerOrNull());
			}
		}

		void ConfigHUDEdit::OnDoneClicked(ui::UIElement&) {
			if (OnDone)
				OnDone(*this);
		}

		void ConfigHUDEdit::HotKey(const std::string& key) {
			if (key == "Escape")
				OnDoneClicked(*this);
			else
				ui::UIElement::HotKey(key);
		}

		void ConfigHUDEdit::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			float sw = GetManager().screenWidth;
			float sh = GetManager().screenHeight;

			float ratio = sw / sh;
			float safeZoneXMin = 0.75F;
			if (ratio < 0.26F)
				safeZoneXMin = 0.28F;
			else if (ratio < 0.56F)
				safeZoneXMin = 0.475F;

			float safeZoneX = Clamp(static_cast<float>(cg_hudSafezoneX), safeZoneXMin, 1.0F);
			float safeZoneY = Clamp(static_cast<float>(cg_hudSafezoneY), 0.85F, 1.0F);
			float x = (sw - 16.0F) * safeZoneX;
			float y = (sh - 16.0F) * safeZoneY;

			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.5F));
			r.DrawFilledRect(0, 0, sw, sh - y);
			r.DrawFilledRect(0, y, sw, sh);
			r.DrawFilledRect(0, sh - y, sw - x, y);
			r.DrawFilledRect(x, sh - y, sw, y);

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.9F));
			r.DrawOutlinedRect(x, y, sw - x, sh - y);

			ui::UIElement::Render();
		}
	} // namespace gui
} // namespace spades
