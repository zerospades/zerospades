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

#include <cstdlib>

#include "ConfigViewTabs.h"
#include "StartupScreenUI.h"
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IImage.h>
#include <Client/IRenderer.h>
#include <Core/Strings.h>
#include <Gui/StartupScreenHelper.h>
#include <Gui/UI/Framework/UIManager.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::CheckBox;
		using ui::Field;
		using ui::RadioButton;
		using ui::TextViewer;
		using ui::UIElement;
		using ui::UIManager;

		// -- StartupScreenGraphicsTab --

		StartupScreenGraphicsTab::StartupScreenGraphicsTab(StartupScreenUI* ui, Vector2 size)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      r_renderer("r_renderer", nullptr),
		      r_fullscreen("r_fullscreen", nullptr) {
			UIManager* manager = &GetManager();
			float mainWidth = size.x - 220.0F;

			{
				Handle<TextViewer> e = Handle<TextViewer>::New(manager);
				e->SetBounds(AABB2(mainWidth + 10.0F, 0.0F, size.x - mainWidth - 10.0F, size.y));
				e->SetFont(&ui->GetFontManager().GetGuiFont());
				e->SetText(_Tr("StartupScreen", "Graphics Settings"));
				AddChild(e.GetPointerOrNull());
				helpView = e.GetPointerOrNull();
			}

			AddConfigLabel(*this, 0.0F, 0.0F, 24.0F, _Tr("StartupScreen", "Resolution"));
			{
				Handle<StartupScreenGraphicsDisplayResolutionEditor> e =
				    Handle<StartupScreenGraphicsDisplayResolutionEditor>::New(ui);
				e->SetBounds(AABB2(160.0F, 0.0F, 124.0F, 24.0F));
				AddChild(e.GetPointerOrNull());
				resEdit = e.GetPointerOrNull();
			}

			{
				Handle<CheckBox> e = Handle<CheckBox>::New(manager);
				e->caption = _Tr("StartupScreen", "Fullscreen Mode");
				float capSize = ui->GetFontManager().GetGuiFont().Measure(e->caption).x;
				e->SetBounds(AABB2(290.0F, 0.0F, capSize + 18.0F, 24.0F));
				WatchHelp(e.GetPointerOrNull(),
				          [this](const std::string& s) { HandleHelpText(s); },
				          _Tr("StartupScreen",
				              "By running in fullscreen mode OpenSpades occupies the "
				              "screen, making it easier for you to concentrate on playing the "
				              "game."));
				e->activated = [this](UIElement& s) { OnFullscreenCheck(s); };
				AddChild(e.GetPointerOrNull());
				fullscreenCheck = e.GetPointerOrNull();
			}

			AddConfigLabel(*this, 0.0F, 30.0F, 24.0F, _Tr("StartupScreen", "Renderer"));
			{
				Handle<RadioButton> e = Handle<RadioButton>::New(manager);
				e->caption = _Tr("StartupScreen", "OpenGL");
				e->SetBounds(AABB2(160.0F, 30.0F, 140.0F, 24.0F));
				e->groupName = "driver";
				WatchHelp(e.GetPointerOrNull(),
				          [this](const std::string& s) { HandleHelpText(s); },
				          _Tr("StartupScreen",
				              "OpenGL renderer uses your computer's graphics "
				              "accelerator to generate the game screen."));
				e->activated = [this](UIElement& s) { OnDriverOpenGL(s); };
				AddChild(e.GetPointerOrNull());
				driverOpenGL = e.GetPointerOrNull();
			}
			{
				Handle<RadioButton> e = Handle<RadioButton>::New(manager);
				e->caption = _Tr("StartupScreen", "Software");
				e->SetBounds(AABB2(310.0F, 30.0F, 140.0F, 24.0F));
				e->groupName = "driver";
				WatchHelp(e.GetPointerOrNull(),
				          [this](const std::string& s) { HandleHelpText(s); },
				          _Tr("StartupScreen",
				              "Software renderer uses CPU to generate the game "
				              "screen. Its quality and performance might be inferior to OpenGL "
				              "renderer, but it works even with an unsupported GPU."));
				e->activated = [this](UIElement& s) { OnDriverSoftware(s); };
				AddChild(e.GetPointerOrNull());
				driverSoftware = e.GetPointerOrNull();
			}

			{
				Handle<StartupScreenConfigView> cfg =
				    Handle<StartupScreenConfigView>::New(manager);

				// TODO: Add r_temporalAA when it's more complete
				cfg->AddRow(
				    Handle<StartupScreenConfigSelectItemEditor>::New(
				        ui,
				        Handle<StartupScreenGraphicsAntialiasConfig>::New(ui)
				            .Cast<StartupScreenGenericConfig>(),
				        "0|2|4|fxaa",
				        _Tr("StartupScreen",
				            "Antialiasing:Enables a technique to improve the appearance of "
				            "high-contrast edges.\n\n"
				            "MSAA: Performs antialiasing by generating an intermediate "
				            "high-resolution image. "
				            "Looks best, but doesn't cope with some settings.\n\n"
				            "FXAA: Performs antialiasing by smoothing artifacts out as a "
				            "post-process.|"
				            "Off|MSAA 2x|4x|FXAA"))
				        .Cast<UIElement>());

				cfg->AddRow(Handle<StartupScreenConfigCheckItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "r_radiosity")
				                    .Cast<StartupScreenGenericConfig>(),
				                "0", "1", _Tr("StartupScreen", "Global Illumination"),
				                _Tr("StartupScreen",
				                    "Enables a physically based simulation of light path for "
				                    "more realistic lighting."))
				                .Cast<UIElement>());

				cfg->AddRow(Handle<StartupScreenConfigCheckItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "r_hdr")
				                    .Cast<StartupScreenGenericConfig>(),
				                "0", "1", _Tr("StartupScreen", "Linear HDR Rendering"),
				                _Tr("StartupScreen",
				                    "Uses a number representation which allows wider dynamic "
				                    "range during rendering process. "
				                    "Additionally, this allows color calculation whose value is "
				                    "in linear correspondence with actual energy, "
				                    "that is, physically accurate blending can be achieved."))
				                .Cast<UIElement>());

				cfg->AddRow(Handle<StartupScreenConfigSelectItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "r_vsync")
				                    .Cast<StartupScreenGenericConfig>(),
				                "0|1|-1",
				                _Tr("StartupScreen",
				                    "Vertical Sync:Synchronizes screen updates to a monitor's "
				                    "refresh rate.|"
				                    "Off|"
				                    "On|"
				                    "Adaptive"))
				                .Cast<UIElement>());

				{
					Handle<StartupScreenComplexConfig> cplx =
					    Handle<StartupScreenComplexConfig>::New();
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_cameraBlur")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Camera Blur"),
					                    _Tr("StartupScreen",
					                        "Blurs the screen when you turn quickly."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_bloom")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Lens Scattering Filter"),
					                    _Tr("StartupScreen",
					                        "Simulates light being scattered by dust on the "
					                        "camera lens."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_lensFlare")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Lens Flare"),
					                    _Tr("StartupScreen", "The Sun causes lens flare."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_lensFlareDynamic")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1",
					                    _Tr("StartupScreen", "Flares for Dynamic Lights."),
					                    _Tr("StartupScreen",
					                        "Enables lens flare for light sources other than "
					                        "the Sun."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_colorCorrection")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Color Correction"),
					                    _Tr("StartupScreen",
					                        "Applies cinematic color correction to make the "
					                        "image look better."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_depthOfField")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Depth of Field"),
					                    _Tr("StartupScreen", "Blurs out-of-focus objects."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_ssao")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1",
					                    _Tr("StartupScreen", "Screen Space Ambient Occlusion"),
					                    _Tr("StartupScreen",
					                        "Simulates soft shadows that occur between nearby "
					                        "objects."))
					                    .Cast<UIElement>());

					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Low"), "0|0|0|0|0|0|0"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Medium"), "1|0|1|0|1|0|0"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "High"), "1|1|1|1|1|1|0"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Ultra"), "1|1|1|1|1|1|1"));

					cfg->AddRow(Handle<StartupScreenConfigComplexItemEditor>::New(
					                ui, cplx, _Tr("StartupScreen", "Post-process"),
					                _Tr("StartupScreen",
					                    "Post-process modifies the image to make it look better "
					                    "and "
					                    "more realistic."))
					                .Cast<UIElement>());
				}

				cfg->AddRow(Handle<StartupScreenConfigSelectItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "r_softParticles")
				                    .Cast<StartupScreenGenericConfig>(),
				                "0|1|2",
				                _Tr("StartupScreen",
				                    "Particle Quality|"
				                    "Low:Artifact occurs when a particle intersects other "
				                    "objects.|"
				                    "Medium:Particle intersects objects smoothly.|"
				                    "High:Particle intersects objects smoothly, and some objects "
				                    "casts "
				                    "their shadow to particles."))
				                .Cast<UIElement>());

				cfg->AddRow(
				    Handle<StartupScreenConfigSelectItemEditor>::New(
				        ui,
				        Handle<StartupScreenConfig>::New(ui, "r_fogShadow")
				            .Cast<StartupScreenGenericConfig>(),
				        "0|1|2",
				        _Tr("StartupScreen", "Volumetric Fog") + "|" +
				            _Tr("StartupScreen", "None") + ":" +
				            _Tr("StartupScreen", "Disables the volumetric fog effect.") + "|" +
				            _Tr("StartupScreen", "Level 1") + ":" +
				            _Tr("StartupScreen", "Applies local illumination to the fog.") + "|" +
				            _Tr("StartupScreen", "Level 2") + ":" +
				            _Tr("StartupScreen",
				                "Applies both local illumination and global illumination to the "
				                "fog.") +
				            "\n\n" +
				            _Tr("StartupScreen", "Warning: '{0}' must be enabled.",
				                _Tr("StartupScreen", "Global Illumination")))
				        .Cast<UIElement>());

				{
					Handle<StartupScreenComplexConfig> cplx =
					    Handle<StartupScreenComplexConfig>::New();
					// r_mapSoftShadow is currently no-op
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_dlights")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Dynamic Lights"),
					                    _Tr("StartupScreen",
					                        "Gives some objects an ability to emit light to give "
					                        "them "
					                        "an energy-emitting impression."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_modelShadows")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1", _Tr("StartupScreen", "Shadows"),
					                    _Tr("StartupScreen", "Non-static object casts a shadow."))
					                    .Cast<UIElement>());
					cplx->AddEditor(Handle<StartupScreenConfigCheckItemEditor>::New(
					                    ui,
					                    Handle<StartupScreenConfig>::New(ui, "r_physicalLighting")
					                        .Cast<StartupScreenGenericConfig>(),
					                    "0", "1",
					                    _Tr("StartupScreen", "Physically Based Lighting"),
					                    _Tr("StartupScreen",
					                        "Uses more accurate approximation techniques to "
					                        "decide the brightness of objects."))
					                    .Cast<UIElement>());

					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Low"), "1|0|0"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Medium"), "1|1|0"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "High"), "1|1|1"));

					cfg->AddRow(Handle<StartupScreenConfigComplexItemEditor>::New(
					                ui, cplx, _Tr("StartupScreen", "Direct Lights"),
					                _Tr("StartupScreen",
					                    "Controls how light encounting a material and atmosphere "
					                    "directly "
					                    "affects its appearance."))
					                .Cast<UIElement>());
				}

				{
					Handle<StartupScreenComplexConfig> cplx =
					    Handle<StartupScreenComplexConfig>::New();

					cplx->AddEditor(
					    Handle<StartupScreenConfigSelectItemEditor>::New(
					        ui,
					        Handle<StartupScreenConfig>::New(ui, "r_water")
					            .Cast<StartupScreenGenericConfig>(),
					        "0|1|2|3",
					        _Tr("StartupScreen",
					            "Water Shader|"
					            "None:Water is rendered in the same way that normal blocks are "
					            "done.|"
					            "Level 1:Refraction and the reflected Sun are simulated.|"
					            "Level 2:Waving water is simulated as well as reflection and "
					            "refraction.|"
					            "Level 3:Reflections and refractions are rendered at the highest "
					            "quality using screen-space techniques."))
					        .Cast<UIElement>());

					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Low"), "0"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Med"), "1"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "High"), "2"));
					cplx->AddPreset(Handle<StartupScreenComplexConfigPreset>::New(
					    _Tr("StartupScreen", "Ultra"), "3"));

					cfg->AddRow(Handle<StartupScreenConfigComplexItemEditor>::New(
					                ui, cplx, _Tr("StartupScreen", "Shader Effects"),
					                _Tr("StartupScreen", "Special effects."))
					                .Cast<UIElement>());
				}

				cfg->Finalize();
				cfg->SetHelpTextHandler([this](const std::string& s) { HandleHelpText(s); });
				cfg->SetBounds(AABB2(0.0F, 60.0F, mainWidth, size.y - 60.0F));
				AddChild(cfg.GetPointerOrNull());
				configViewGL = cfg.GetPointerOrNull();
			}

			{
				Handle<StartupScreenConfigView> cfg =
				    Handle<StartupScreenConfigView>::New(manager);

				cfg->AddRow(Handle<StartupScreenConfigSelectItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "r_swUndersampling")
				                    .Cast<StartupScreenGenericConfig>(),
				                "0|1|2",
				                _Tr("StartupScreen",
				                    "Fast Mode:Reduces the image resolution to make the "
				                    "rendering faster.|"
				                    "Off|2x|4x"))
				                .Cast<UIElement>());

				cfg->Finalize();
				cfg->SetHelpTextHandler([this](const std::string& s) { HandleHelpText(s); });
				cfg->SetBounds(AABB2(0.0F, 60.0F, mainWidth, size.y - 60.0F));
				AddChild(cfg.GetPointerOrNull());
				configViewSoftware = cfg.GetPointerOrNull();
			}
		}

		void StartupScreenGraphicsTab::HandleHelpText(const std::string& text) {
			helpView->SetText(text);
		}

		void StartupScreenGraphicsTab::OnDriverOpenGL(UIElement&) {
			r_renderer = std::string("gl");
			LoadConfig();
		}

		void StartupScreenGraphicsTab::OnDriverSoftware(UIElement&) {
			r_renderer = std::string("sw");
			LoadConfig();
		}

		void StartupScreenGraphicsTab::OnFullscreenCheck(UIElement&) {
			r_fullscreen = fullscreenCheck->toggled ? 1 : 0;
		}

		void StartupScreenGraphicsTab::LoadConfig() {
			resEdit->LoadConfig();
			if (static_cast<std::string>(r_renderer) == "sw") {
				driverSoftware->Check();
				configViewGL->visible = false;
				configViewSoftware->visible = true;
			} else {
				driverOpenGL->Check();
				configViewGL->visible = true;
				configViewSoftware->visible = false;
			}
			fullscreenCheck->toggled = static_cast<int>(r_fullscreen) != 0;
			driverOpenGL->enable = helper->CheckConfigCapability("r_renderer", "gl").empty();
			driverSoftware->enable = helper->CheckConfigCapability("r_renderer", "sw").empty();
			configViewGL->LoadConfig();
			configViewSoftware->LoadConfig();
		}

		// -- StartupScreenComboBoxDropdownButton --

		void StartupScreenComboBoxDropdownButton::Render() {
			SimpleButton::Render();

			client::IRenderer& r = GetManager().GetRenderer();
			Handle<client::IImage> img = r.RegisterImage("Gfx/UI/ScrollArrow.png");
			AABB2 bnd = GetScreenBounds();
			Vector2 p = (bnd.min + bnd.max) * 0.5F + MakeVector2(-8.0F, 8.0F);
			r.DrawImage(img, AABB2(p.x, p.y, 16.0F, -16.0F));
		}

		// -- StartupScreenGraphicsDisplayResolutionEditor --

		StartupScreenGraphicsDisplayResolutionEditor::
		    StartupScreenGraphicsDisplayResolutionEditor(StartupScreenUI* ui)
		    : UIElement(&ui->GetUIManager()),
		      r_videoWidth("r_videoWidth", nullptr),
		      r_videoHeight("r_videoHeight", nullptr),
		      helper(&ui->GetHelper()) {
			UIManager* manager = &GetManager();
			{
				Handle<Field> e = Handle<Field>::New(manager);
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(0, 0, 45.0F, 24.0F));
				e->textOrigin.y *= 0.5F;
				e->denyNonAscii = true;
				e->changed = [this](UIElement& s) { ValueEditHandler(s); };
				widthField = e.GetPointerOrNull();
			}
			{
				Handle<Field> e = Handle<Field>::New(manager);
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(53, 0, 45.0F, 24.0F));
				e->textOrigin.y *= 0.5F;
				e->denyNonAscii = true;
				e->changed = [this](UIElement& s) { ValueEditHandler(s); };
				heightField = e.GetPointerOrNull();
			}
			{
				Handle<StartupScreenComboBoxDropdownButton> e =
				    Handle<StartupScreenComboBoxDropdownButton>::New(manager);
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(100, 0, 24.0F, 24.0F));
				e->activated = [this](UIElement& s) { ShowDropdown(s); };
				dropdownButton = e.GetPointerOrNull();
			}
		}

		void StartupScreenGraphicsDisplayResolutionEditor::LoadConfig() {
			widthField->SetText(std::to_string(static_cast<int>(r_videoWidth)));
			heightField->SetText(std::to_string(static_cast<int>(r_videoHeight)));
		}

		void StartupScreenGraphicsDisplayResolutionEditor::SaveConfig() {
			int w = std::atoi(widthField->GetText().c_str());
			int h = std::atoi(heightField->GetText().c_str());
			if (w < 640 || h < 480 || w > 8192 || h > 8192)
				return;
			r_videoWidth = w;
			r_videoHeight = h;
		}

		void StartupScreenGraphicsDisplayResolutionEditor::ValueEditHandler(UIElement&) {
			SaveConfig();
		}

		void StartupScreenGraphicsDisplayResolutionEditor::DropdownHandler(int index) {
			if (index >= 0) {
				widthField->SetText(std::to_string(helper->GetVideoModeWidth(index)));
				heightField->SetText(std::to_string(helper->GetVideoModeHeight(index)));
				SaveConfig();
			}
		}

		void StartupScreenGraphicsDisplayResolutionEditor::ShowDropdown(UIElement&) {
			std::vector<std::string> items;
			int cnt = helper->GetNumVideoModes();
			for (int i = 0; i < cnt; i++) {
				int w = helper->GetVideoModeWidth(i);
				int h = helper->GetVideoModeHeight(i);
				items.push_back(std::to_string(w) + "x" + std::to_string(h));
			}
			ui::ShowDropDownList(*this, std::move(items),
			                     [this](int index) { DropdownHandler(index); });
		}

		void StartupScreenGraphicsDisplayResolutionEditor::Render() {
			client::IFont* font = GetFont();
			if (font)
				font->Draw("x", MakeVector2(45.0F, 0.0F) + GetScreenPosition(), 1.0F,
				           MakeVector4(1.0F, 1.0F, 1.0F, 1.0F));
			UIElement::Render();
		}

		// -- StartupScreenGraphicsAntialiasConfig --

		StartupScreenGraphicsAntialiasConfig::StartupScreenGraphicsAntialiasConfig(
		    StartupScreenUI* ui)
		    : ui(ui), msaaConfig("r_multisamples", nullptr), fxaaConfig("r_fxaa", nullptr) {}

		std::string StartupScreenGraphicsAntialiasConfig::GetValue() {
			if (static_cast<int>(fxaaConfig) != 0) {
				return "fxaa";
			} else {
				int v = msaaConfig;
				if (v < 2)
					return "0";
				else
					return static_cast<std::string>(msaaConfig);
			}
		}

		void StartupScreenGraphicsAntialiasConfig::SetValue(const std::string& v) {
			if (v == "fxaa") {
				msaaConfig = std::string("0");
				fxaaConfig = std::string("1");
			} else if (v == "0" || v == "1") {
				msaaConfig = std::string("0");
				fxaaConfig = std::string("0");
			} else {
				msaaConfig = v;
				fxaaConfig = std::string("0");
			}
		}

		std::string
		StartupScreenGraphicsAntialiasConfig::CheckValueCapability(const std::string& v) {
			StartupScreenHelper& helper = ui->GetHelper();
			if (v == "fxaa") {
				return helper.CheckConfigCapability("r_multisamples", "0") +
				       helper.CheckConfigCapability("r_fxaa", "1");
			} else if (v == "0" || v == "1") {
				return helper.CheckConfigCapability("r_multisamples", "0") +
				       helper.CheckConfigCapability("r_fxaa", "0");
			} else {
				return helper.CheckConfigCapability("r_multisamples", v) +
				       helper.CheckConfigCapability("r_fxaa", "0");
			}
		}

		// -- StartupScreenAudioTab --

		StartupScreenAudioTab::StartupScreenAudioTab(StartupScreenUI* ui, Vector2 size)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      s_audioDriver("s_audioDriver", nullptr),
		      s_openalDevice("s_openalDevice", nullptr) {
			UIManager* manager = &GetManager();
			float mainWidth = size.x - 220.0F;

			{
				Handle<TextViewer> e = Handle<TextViewer>::New(manager);
				e->SetBounds(AABB2(mainWidth + 10.0F, 0.0F, size.x - mainWidth - 10.0F, size.y));
				e->SetFont(&ui->GetFontManager().GetGuiFont());
				e->SetText(_Tr("StartupScreen", "Audio Settings"));
				AddChild(e.GetPointerOrNull());
				helpView = e.GetPointerOrNull();
			}

			AddConfigLabel(*this, 0.0F, 0.0F, 24.0F, _Tr("StartupScreen", "Backend"));
			{
				Handle<RadioButton> e = Handle<RadioButton>::New(manager);
				e->caption = _Tr("StartupScreen", "OpenAL");
				e->SetBounds(AABB2(160.0F, 0.0F, 100.0F, 24.0F));
				e->groupName = "driver";
				WatchHelp(e.GetPointerOrNull(),
				          [this](const std::string& s) { HandleHelpText(s); },
				          _Tr("StartupScreen",
				              "Uses an OpenAL-capable sound card to process sound. "
				              "In most cases where there isn't such a sound card, software "
				              "emulation is "
				              "used."));
				e->activated = [this](UIElement& s) { OnDriverOpenAL(s); };
				AddChild(e.GetPointerOrNull());
				driverOpenAL = e.GetPointerOrNull();
			}
			{
				Handle<RadioButton> e = Handle<RadioButton>::New(manager);
				//! The name of audio driver that outputs no audio.
				e->caption = _Tr("StartupScreen", "Null");
				e->SetBounds(AABB2(270.0F, 0.0F, 100.0F, 24.0F));
				e->groupName = "driver";
				WatchHelp(e.GetPointerOrNull(),
				          [this](const std::string& s) { HandleHelpText(s); },
				          _Tr("StartupScreen", "Disables audio output."));
				e->activated = [this](UIElement& s) { OnDriverNull(s); };
				AddChild(e.GetPointerOrNull());
				driverNull = e.GetPointerOrNull();
			}
			{
				Handle<StartupScreenConfigView> cfg =
				    Handle<StartupScreenConfigView>::New(manager);

				cfg->AddRow(Handle<StartupScreenConfigSliderItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "s_volume")
				                    .Cast<StartupScreenGenericConfig>(),
				                0.0, 100.0, 1.0, _Tr("StartupScreen", "Volume"), "",
				                NumberFormatter(0, "%"))
				                .Cast<UIElement>());

				cfg->AddRow(Handle<StartupScreenConfigSliderItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "s_maxPolyphonics")
				                    .Cast<StartupScreenGenericConfig>(),
				                16.0, 256.0, 8.0, _Tr("StartupScreen", "Polyphonics"),
				                _Tr("StartupScreen",
				                    "Specifies how many sounds can be played simultaneously. "
				                    "Higher value needs more processing power, so setting this "
				                    "too high might "
				                    "cause an overload (especially with a software emulation)."),
				                NumberFormatter(0, " poly"))
				                .Cast<UIElement>());

				cfg->AddRow(Handle<StartupScreenConfigCheckItemEditor>::New(
				                ui,
				                Handle<StartupScreenConfig>::New(ui, "s_eax")
				                    .Cast<StartupScreenGenericConfig>(),
				                "0", "1", _Tr("StartupScreen", "EAX"),
				                _Tr("StartupScreen",
				                    "Enables extended features provided by the OpenAL driver to "
				                    "create "
				                    "more ambience."))
				                .Cast<UIElement>());

				cfg->Finalize();
				cfg->SetHelpTextHandler([this](const std::string& s) { HandleHelpText(s); });
				cfg->SetBounds(AABB2(0.0F, 30.0F, mainWidth, size.y - 30.0F));
				AddChild(cfg.GetPointerOrNull());
				configViewOpenAL = cfg.GetPointerOrNull();
			}

			AddConfigLabel(*this, 0.0F, 120.0F, 24.0F, _Tr("StartupScreen", "Output Device"));
			{
				Handle<StartupScreenAudioOpenALEditor> e =
				    Handle<StartupScreenAudioOpenALEditor>::New(ui);
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(160.0F, 120.0F, 380.0F, 24.0F));
				editOpenAL = e.GetPointerOrNull();
			}
		}

		void StartupScreenAudioTab::HandleHelpText(const std::string& text) {
			helpView->SetText(text);
		}

		void StartupScreenAudioTab::OnDriverOpenAL(UIElement&) {
			s_audioDriver = std::string("openal");
			LoadConfig();
		}

		void StartupScreenAudioTab::OnDriverNull(UIElement&) {
			s_audioDriver = std::string("null");
			LoadConfig();
		}

		void StartupScreenAudioTab::LoadConfig() {
			if (static_cast<std::string>(s_audioDriver) == "openal") {
				driverOpenAL->Check();
				configViewOpenAL->visible = true;
			} else if (static_cast<std::string>(s_audioDriver) == "null") {
				driverNull->Check();
				configViewOpenAL->visible = false;
			}
			driverOpenAL->enable =
			    helper->CheckConfigCapability("s_audioDriver", "openal").empty();
			driverNull->enable = helper->CheckConfigCapability("s_audioDriver", "null").empty();
			configViewOpenAL->LoadConfig();
			editOpenAL->LoadConfig();

			s_openalDevice = static_cast<std::string>(editOpenAL->openal);
		}

		// -- StartupScreenDropdownListDropdownButton --

		void StartupScreenDropdownListDropdownButton::Render() {
			SimpleButton::Render();

			client::IRenderer& r = GetManager().GetRenderer();
			Handle<client::IImage> img = r.RegisterImage("Gfx/UI/ScrollArrow.png");
			AABB2 bnd = GetScreenBounds();
			Vector2 p = (bnd.min + bnd.max) * 0.5F + MakeVector2(-8.0F, 8.0F);
			r.DrawImage(img, AABB2(bnd.max.x - 16.0F, p.y, 16.0F, -16.0F));
		}

		// -- StartupScreenAudioOpenALEditor --

		StartupScreenAudioOpenALEditor::StartupScreenAudioOpenALEditor(StartupScreenUI* ui)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      openal("openal", nullptr) {
			{
				Handle<StartupScreenDropdownListDropdownButton> e =
				    Handle<StartupScreenDropdownListDropdownButton>::New(&GetManager());
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(0.0F, 0.0F, 380.0F, 24.0F));
				e->activated = [this](UIElement& s) { ShowDropdown(s); };
				dropdownButton = e.GetPointerOrNull();
			}
		}

		void StartupScreenAudioOpenALEditor::LoadConfig() {
			std::string drivername = openal;
			std::string name = _Tr("StartupScreen", "System default", drivername);
			if (drivername == "")
				name = _Tr("StartupScreen", "System default");

			int cnt = helper->GetNumAudioOpenALDevices();
			for (int i = 0; i < cnt; i++) {
				if (drivername == helper->GetAudioOpenALDevice(i))
					name = helper->GetAudioOpenALDevice(i);
			}

			dropdownButton->caption = name;
		}

		void StartupScreenAudioOpenALEditor::ShowDropdown(UIElement&) {
			std::vector<std::string> items = {_Tr("StartupScreen", "System default")};
			int cnt = helper->GetNumAudioOpenALDevices();
			for (int i = 0; i < cnt; i++)
				items.push_back(helper->GetAudioOpenALDevice(i));
			ui::ShowDropDownList(*this, std::move(items),
			                     [this](int index) { DropdownHandler(index); });
		}

		void StartupScreenAudioOpenALEditor::DropdownHandler(int index) {
			if (index >= 0) {
				if (index == 0)
					openal = std::string("default");
				else
					openal = helper->GetAudioOpenALDevice(index - 1);

				// Reload the startup screen so the language config takes effect
				ui->Reload();
			}
		}

		// -- StartupScreenGenericTab --

		StartupScreenGenericTab::StartupScreenGenericTab(StartupScreenUI* ui, Vector2 size)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      cg_demoAutoRecord("cg_demoAutoRecord", nullptr),
		      cg_demoAutoPrune("cg_demoAutoPrune", nullptr),
		      cg_demoMaxFiles("cg_demoMaxFiles", nullptr) {
			UIManager* manager = &GetManager();
			float mainWidth = size.x - 240.0F;

			std::string label = _Tr("StartupScreen", "Language");
			if (label != "Language")
				label += " (Language)";

			AddConfigLabel(*this, 0.0F, 0.0F, 24.0F, _Tr("StartupScreen", label));
			{
				Handle<StartupScreenLocaleEditor> e =
				    Handle<StartupScreenLocaleEditor>::New(ui);
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(160.0F, 0.0F, 380.0F, 24.0F));
				editLocale = e.GetPointerOrNull();
			}

			AddConfigLabel(*this, 0.0F, 30.0F, 30.0F, _Tr("StartupScreen", "Tools"));
			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("StartupScreen", "Reset All Settings");
				button->SetBounds(AABB2(160.0F, 30.0F, 380.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnResetSettingsPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
			{
				Handle<Button> button = Handle<Button>::New(manager);
				std::string osType = helper->GetOperatingSystemType();
				if (osType == "Windows") {
					button->caption = _Tr("StartupScreen", "Open Config Folder in Explorer");
				} else if (osType == "Mac") {
					button->caption = _Tr("StartupScreen", "Reveal Config Folder in Finder");
				} else {
					button->caption = _Tr("StartupScreen", "Browse Config Folder");
				}
				button->SetBounds(AABB2(160.0F, 66.0F, 380.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnBrowseUserDirectoryPressed(s); };
				AddChild(button.GetPointerOrNull());
			}

			AddConfigLabel(*this, 0.0F, 108.0F, 24.0F, _Tr("StartupScreen", "Demo Recording"));
			{
				Handle<CheckBox> button = Handle<CheckBox>::New(manager);
				button->caption = _Tr("StartupScreen", "Automatically record every game");
				button->SetBounds(AABB2(0.0F, 138.0F, mainWidth, 20.0F));
				button->activated = [this](UIElement& s) { OnAutoRecordChanged(s); };
				AddChild(button.GetPointerOrNull());
				buttonAutoRecord = button.GetPointerOrNull();
			}
			{
				Handle<CheckBox> button = Handle<CheckBox>::New(manager);
				button->caption = _Tr("StartupScreen", "Delete oldest demos when limit is reached");
				button->SetBounds(AABB2(0.0F, 168.0F, mainWidth, 20.0F));
				button->activated = [this](UIElement& s) { OnAutoPruneChanged(s); };
				AddChild(button.GetPointerOrNull());
				buttonAutoPrune = button.GetPointerOrNull();
			}
			{
				std::string caption = _Tr("StartupScreen", "Max stored demos");
				Vector2 labelSize = ui->GetFontManager().GetGuiFont().Measure(caption);
				AddConfigLabel(*this, 0.0F, 198.0F, 24.0F, caption);

				Handle<Field> field = Handle<Field>::New(manager);
				field->SetBounds(AABB2(labelSize.x + 8.0F, 198.0F, 40.0F, 24.0F));
				field->denyNonAscii = true;
				field->textOrigin.y *= 0.5F;
				field->changed = [this](UIElement& s) { OnMaxDemosChanged(s); };
				AddChild(field.GetPointerOrNull());
				fieldMaxDemos = field.GetPointerOrNull();
			}

			AddConfigLabel(*this, 0.0F, 234.0F, 30.0F, _Tr("StartupScreen", "Demos Folder"));
			{
				Handle<Button> button = Handle<Button>::New(manager);
				std::string osType = helper->GetOperatingSystemType();
				if (osType == "Windows") {
					button->caption = _Tr("StartupScreen", "Open Demos Folder in Explorer");
				} else if (osType == "Mac") {
					button->caption = _Tr("StartupScreen", "Reveal Demos Folder in Finder");
				} else {
					button->caption = _Tr("StartupScreen", "Browse Demos Folder");
				}
				button->SetBounds(AABB2(160.0F, 234.0F, 380.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnBrowseDemosFolderPressed(s); };
				AddChild(button.GetPointerOrNull());
			}
		}

		void StartupScreenGenericTab::LoadConfig() {
			editLocale->LoadConfig();
			buttonAutoRecord->toggled = static_cast<int>(cg_demoAutoRecord) != 0;
			buttonAutoPrune->toggled = static_cast<int>(cg_demoAutoPrune) != 0;
			fieldMaxDemos->SetText(static_cast<std::string>(cg_demoMaxFiles));
		}

		void StartupScreenGenericTab::OnAutoRecordChanged(UIElement&) {
			cg_demoAutoRecord = buttonAutoRecord->toggled ? 1 : 0;
		}

		void StartupScreenGenericTab::OnAutoPruneChanged(UIElement&) {
			cg_demoAutoPrune = buttonAutoPrune->toggled ? 1 : 0;
		}

		void StartupScreenGenericTab::OnMaxDemosChanged(UIElement&) {
			int v = std::atoi(fieldMaxDemos->GetText().c_str());
			if (v >= 1)
				cg_demoMaxFiles = v;
		}

		void StartupScreenGenericTab::OnBrowseDemosFolderPressed(UIElement&) {
			if (helper->BrowseDemosDirectory())
				return;

			std::string msg = _Tr(
			    "StartupScreen", "An unknown error has occurred while opening the demos directory.");
			Handle<AlertScreen> al = Handle<AlertScreen>::New(GetParent(), msg, 100.0F);
			al->Run();
		}

		void StartupScreenGenericTab::OnBrowseUserDirectoryPressed(UIElement&) {
			if (helper->BrowseUserDirectory())
				return;

			std::string msg = _Tr(
			    "StartupScreen", "An unknown error has occurred while opening the config directory.");
			Handle<AlertScreen> al = Handle<AlertScreen>::New(GetParent(), msg, 100.0F);
			al->Run();
		}

		void StartupScreenGenericTab::OnResetSettingsPressed(UIElement&) {
			std::string msg =
			    _Tr("StartupScreen",
			        "Are you sure to reset all settings? They include (but are not limited to):") +
			    "\n" + "- " + _Tr("StartupScreen", "All graphics/audio settings") + "\n" + "- " +
			    _Tr("StartupScreen", "All key bindings") + "\n" + "- " +
			    _Tr("StartupScreen", "Your player name") + "\n" + "- " +
			    _Tr("StartupScreen",
			        "Other advanced settings only accessible through '{0}' tab and in-game "
			        "commands.",
			        _Tr("StartupScreen", "Advanced"));
			Handle<ConfirmScreen> al = Handle<ConfirmScreen>::New(GetParent(), msg, 200.0F);
			al->closed = [this](UIElement& s) { OnResetSettingsConfirmed(s); };
			al->Run();
		}

		void StartupScreenGenericTab::OnResetSettingsConfirmed(UIElement& sender) {
			ConfirmScreen* confirm = dynamic_cast<ConfirmScreen*>(&sender);
			if (confirm == nullptr || !confirm->GetResult())
				return;

			ResetAllSettings();

			// Reload the startup screen so the language config takes effect
			ui->Reload();
		}

		void StartupScreenGenericTab::ResetAllSettings() {
			std::vector<std::string> names = Settings::GetInstance()->GetAllItemNames();
			for (const std::string& name : names) {
				Settings::ItemHandle item(name, nullptr);
				item = std::string(item.GetDescriptor().defaultValue);
			}

			// Some of default values may be infeasible for the user's system.
			helper->FixConfigs();
		}

		// -- StartupScreenLocaleEditor --

		StartupScreenLocaleEditor::StartupScreenLocaleEditor(StartupScreenUI* ui)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      helper(&ui->GetHelper()),
		      core_locale("core_locale", nullptr) {
			{
				Handle<StartupScreenDropdownListDropdownButton> e =
				    Handle<StartupScreenDropdownListDropdownButton>::New(&GetManager());
				AddChild(e.GetPointerOrNull());
				e->SetBounds(AABB2(0.0F, 0.0F, 380.0F, 24.0F));
				e->activated = [this](UIElement& s) { ShowDropdown(s); };
				dropdownButton = e.GetPointerOrNull();
			}
		}

		void StartupScreenLocaleEditor::LoadConfig() {
			std::string locale = core_locale;
			std::string name = _Tr("StartupScreen", "Unknown ({0})", locale);
			if (locale == "")
				name = _Tr("StartupScreen", "System default");

			int cnt = helper->GetNumLocales();
			for (int i = 0; i < cnt; i++) {
				if (locale == helper->GetLocale(i)) {
					name = helper->GetLocaleDescriptionNative(i) + " / " +
					       helper->GetLocaleDescriptionEnglish(i);
				}
			}

			dropdownButton->caption = name;
		}

		void StartupScreenLocaleEditor::ShowDropdown(UIElement&) {
			std::vector<std::string> items = {_Tr("StartupScreen", "System default")};
			int cnt = helper->GetNumLocales();
			for (int i = 0; i < cnt; i++) {
				items.push_back(helper->GetLocaleDescriptionNative(i) + " / " +
				                helper->GetLocaleDescriptionEnglish(i));
			}
			ui::ShowDropDownList(*this, std::move(items),
			                     [this](int index) { DropdownHandler(index); });
		}

		void StartupScreenLocaleEditor::DropdownHandler(int index) {
			if (index >= 0) {
				if (index == 0)
					core_locale = std::string("");
				else
					core_locale = helper->GetLocale(index - 1);

				// Reload the startup screen so the language config takes effect
				ui->Reload();
			}
		}

		// -- StartupScreenSystemInfoTab --

		StartupScreenSystemInfoTab::StartupScreenSystemInfoTab(StartupScreenUI* ui, Vector2 size)
		    : UIElement(&ui->GetUIManager()), ui(ui), helper(&ui->GetHelper()) {
			UIManager* manager = &GetManager();
			{
				Handle<TextViewer> e = Handle<TextViewer>::New(manager);
				e->SetBounds(AABB2(0.0F, 0.0F, size.x, size.y - 30.0F));
				e->SetFont(&ui->GetFontManager().GetGuiFont());
				AddChild(e.GetPointerOrNull());
				for (int i = 0, count = helper->GetNumReportLines(); i < count; i++) {
					std::string text = helper->GetReportLineText(i);
					Vector4 col = helper->GetReportLineColor(i);
					e->AddLine(text, true, col);
				}
				helpView = e.GetPointerOrNull();
			}

			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("StartupScreen", "Copy to Clipboard");
				button->SetBounds(AABB2(size.x - 180.0F, size.y - 30.0F, 180.0F, 30.0F));
				button->activated = [this](UIElement& s) { OnCopyReport(s); };
				AddChild(button.GetPointerOrNull());
			}
		}

		void StartupScreenSystemInfoTab::OnCopyReport(UIElement&) {
			GetManager().Copy(helper->GetReport());
		}

		// -- StartupScreenAdvancedTab --

		StartupScreenAdvancedTab::StartupScreenAdvancedTab(StartupScreenUI* ui, Vector2 size)
		    : UIElement(&ui->GetUIManager()), ui(ui), helper(&ui->GetHelper()) {
			UIManager* manager = &GetManager();
			float mainWidth = size.x - 220.0F;

			{
				Handle<TextViewer> e = Handle<TextViewer>::New(manager);
				e->SetBounds(AABB2(mainWidth + 10.0F, 0.0F, size.x - mainWidth - 10.0F, size.y));
				e->SetFont(&ui->GetFontManager().GetGuiFont());
				e->SetText(_Tr("StartupScreen", "Advanced Settings"));
				e->visible = false;
				AddChild(e.GetPointerOrNull());
				helpView = e.GetPointerOrNull();
			}

			{
				Handle<Field> e = Handle<Field>::New(manager);
				e->placeholder = _Tr("StartupScreen", "Filter");
				e->SetBounds(AABB2(0.0F, 0.0F, size.x, 24.0F));
				e->textOrigin *= 0.5F;
				e->changed = [this](UIElement& s) { OnFilterChanged(s); };
				AddChild(e.GetPointerOrNull());
				filter = e.GetPointerOrNull();
			}

			{
				Handle<StartupScreenConfigView> cfg =
				    Handle<StartupScreenConfigView>::New(manager);

				std::vector<std::string> names = Settings::GetInstance()->GetAllItemNames();
				for (const std::string& name : names) {
					cfg->AddRow(Handle<StartupScreenConfigFieldItemEditor>::New(
					                ui,
					                Handle<StartupScreenConfig>::New(ui, name)
					                    .Cast<StartupScreenGenericConfig>(),
					                name, "")
					                .Cast<UIElement>());
				}

				cfg->Finalize();
				cfg->SetHelpTextHandler([this](const std::string& s) { HandleHelpText(s); });
				cfg->SetBounds(AABB2(0.0F, 30.0F, size.x, size.y - 30.0F));
				AddChild(cfg.GetPointerOrNull());
				configView = cfg.GetPointerOrNull();
			}
		}

		void StartupScreenAdvancedTab::HandleHelpText(const std::string& text) {
			helpView->SetText(text);
		}

		void StartupScreenAdvancedTab::OnFilterChanged(UIElement&) {
			configView->Filter(filter->GetText());
		}

		void StartupScreenAdvancedTab::LoadConfig() { configView->LoadConfig(); }
	} // namespace gui
} // namespace spades
