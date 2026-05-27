/*
 Copyright (c) 2013 yvt

 This file is part of OpenSpades.

 OpenSpades is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 OpenSpades is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with OpenSpades.	 If not, see <http://www.gnu.org/licenses/>.

 */

#include "ConfigViewFramework.as"
#include "../UIFramework/DropDownList.as"
#include "../MessageBox.as"

namespace spades {

	class StartupScreenGraphicsTab : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;

		StartupScreenGraphicsDisplayResolutionEditor@ resEdit;

		spades::ui::CheckBox@ fullscreenCheck;
		spades::ui::RadioButton@ driverOpenGL;
		spades::ui::RadioButton@ driverSoftware;

		spades::ui::TextViewer@ helpView;
		StartupScreenConfigView@ configViewGL;
		StartupScreenConfigView@ configViewSoftware;

		private ConfigItem r_renderer("r_renderer");
		private ConfigItem r_fullscreen("r_fullscreen");

		StartupScreenGraphicsTab(StartupScreenUI@ ui, Vector2 size) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;

			float mainWidth = size.x - 250.0F;

			{
				spades::ui::TextViewer e(Manager);
				e.Bounds = AABB2(mainWidth + 10.0F, 0.0F, size.x - mainWidth - 10.0F, size.y);
				@e.Font = ui.fontManager.GuiFont;
				e.Text = _Tr("StartupScreen", "Graphics Settings");
				AddChild(e);
				@helpView = e;
			}

			AddLabel(0.0F, 0.0F, 24.0F, _Tr("StartupScreen", "Resolution"));
			{
				StartupScreenGraphicsDisplayResolutionEditor e(ui);
				e.Bounds = AABB2(110.0F, 0.0F, 124.0F, 24.0F);
				AddChild(e);
				@resEdit = e;
			}

			{
				spades::ui::CheckBox e(Manager);
				e.Caption = _Tr("StartupScreen", "Fullscreen Mode");
				e.Bounds = AABB2(240.0F, 0.0F, 200.0F, 24.0F);
				HelpHandler(
					helpView,
					_Tr("StartupScreen",
						"By running in fullscreen mode OpenSpades occupies the "
						"screen, making it easier for you to concentrate on playing the game."))
					.Watch(e);
				@e.Activated = spades::ui::EventHandler(this.OnFullscreenCheck);
				AddChild(e);
				@fullscreenCheck = e;
			}

			AddLabel(0.0F, 30.0F, 24.0F, _Tr("StartupScreen", "Renderer"));
			{
				spades::ui::RadioButton e(Manager);
				e.Caption = _Tr("StartupScreen", "OpenGL");
				e.Bounds = AABB2(110.0F, 30.0F, 140.0F, 24.0F);
				e.GroupName = "driver";
				HelpHandler(
					helpView,
					_Tr("StartupScreen",
						"OpenGL renderer uses your computer's graphics "
						"accelerator to generate the game screen."))
					.Watch(e);
				@e.Activated = spades::ui::EventHandler(this.OnDriverOpenGL);
				AddChild(e);
				@driverOpenGL = e;
			}
			{
				spades::ui::RadioButton e(Manager);
				e.Caption = _Tr("StartupScreen", "Software");
				e.Bounds = AABB2(260.0F, 30.0F, 140.0F, 24.0F);
				e.GroupName = "driver";
				HelpHandler(
					helpView,
					_Tr("StartupScreen",
						"Software renderer uses CPU to generate the game "
						"screen. Its quality and performance might be inferior to OpenGL "
						"renderer, but it works even with an unsupported GPU."))
					.Watch(e);
				@e.Activated = spades::ui::EventHandler(this.OnDriverSoftware);
				AddChild(e);
				@driverSoftware = e;
			}

			{
				StartupScreenConfigView cfg(Manager);

				// TODO: Add r_temporalAA when it's more complete
				cfg.AddRow(StartupScreenConfigSelectItemEditor(
					ui, StartupScreenGraphicsAntialiasConfig(ui), "0|2|4|fxaa",
					_Tr("StartupScreen",
						"Antialiasing:Enables a technique to improve the appearance of high-contrast edges.\n\n"
						"MSAA: Performs antialiasing by generating an intermediate high-resolution image. "
						"Looks best, but doesn't cope with some settings.\n\n"
						"FXAA: Performs antialiasing by smoothing artifacts out as a post-process.|"
						"Off|MSAA 2x|4x|FXAA")));

				cfg.AddRow(StartupScreenConfigCheckItemEditor(
					ui, StartupScreenConfig(ui, "r_radiosity"), "0", "1",
					_Tr("StartupScreen", "Global Illumination"),
					_Tr("StartupScreen",
						"Enables a physically based simulation of light path for more realistic lighting.")));

				cfg.AddRow(StartupScreenConfigCheckItemEditor(
					ui, StartupScreenConfig(ui, "r_hdr"), "0", "1",
					_Tr("StartupScreen", "Linear HDR Rendering"),
					_Tr("StartupScreen", "Uses a number representation which allows wider dynamic range during rendering process. "
						"Additionally, this allows color calculation whose value is in linear correspondence with actual energy, "
						"that is, physically accurate blending can be achieved.")));

                cfg.AddRow(StartupScreenConfigSelectItemEditor(
                    ui, StartupScreenConfig(ui, "r_vsync"), "0|1|-1",
                    _Tr("StartupScreen",
                        "Vertical Sync:Synchronizes screen updates to a monitor's refresh rate.|" "Off|" "On|" "Adaptive")));

				{
					StartupScreenComplexConfig cplx;
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_cameraBlur"), "0", "1",
						_Tr("StartupScreen", "Camera Blur"),
						_Tr("StartupScreen", "Blurs the screen when you turn quickly.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_bloom"), "0", "1",
						_Tr("StartupScreen", "Lens Scattering Filter"),
						_Tr("StartupScreen", "Simulates light being scattered by dust on the camera lens.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_lensFlare"), "0", "1",
						_Tr("StartupScreen", "Lens Flare"),
						_Tr("StartupScreen", "The Sun causes lens flare.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_lensFlareDynamic"), "0", "1",
						_Tr("StartupScreen", "Flares for Dynamic Lights."),
						_Tr("StartupScreen", "Enables lens flare for light sources other than the Sun.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_colorCorrection"), "0", "1",
						_Tr("StartupScreen", "Color Correction"),
						_Tr("StartupScreen", "Applies cinematic color correction to make the image look better.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_depthOfField"), "0", "1",
						_Tr("StartupScreen", "Depth of Field"),
						_Tr("StartupScreen", "Blurs out-of-focus objects.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_ssao"), "0", "1",
						_Tr("StartupScreen", "Screen Space Ambient Occlusion"),
						_Tr("StartupScreen", "Simulates soft shadows that occur between nearby objects.")));

					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Low"), "0|0|0|0|0|0|0"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Medium"), "1|0|1|0|1|0|0"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "High"), "1|1|1|1|1|1|0"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Ultra"), "1|1|1|1|1|1|1"));

					cfg.AddRow(StartupScreenConfigComplexItemEditor(
						ui, cplx, _Tr("StartupScreen", "Post-process"),
						_Tr("StartupScreen", "Post-process modifies the image to make it look better and " "more realistic.")));
				}

				cfg.AddRow(StartupScreenConfigSelectItemEditor(
					ui, StartupScreenConfig(ui, "r_softParticles"), "0|1|2",
					_Tr("StartupScreen",
						"Particle Quality|"
						"Low:Artifact occurs when a particle intersects other objects.|"
						"Medium:Particle intersects objects smoothly.|"
						"High:Particle intersects objects smoothly, and some objects casts " "their shadow to particles.")));

				cfg.AddRow(StartupScreenConfigSelectItemEditor(
					ui, StartupScreenConfig(ui, "r_fogShadow"), "0|1|2",
					_Tr("StartupScreen", "Volumetric Fog") + "|" +
					_Tr("StartupScreen", "None") + ":" +
					_Tr("StartupScreen", "Disables the volumetric fog effect.") + "|" +
					_Tr("StartupScreen", "Level 1") + ":"  +
					_Tr("StartupScreen", "Applies local illumination to the fog.") + "|" +
					_Tr("StartupScreen", "Level 2") + ":"  +
					_Tr("StartupScreen", "Applies both local illumination and global illumination to the fog.") + "\n\n" +
					_Tr("StartupScreen", "Warning: '{0}' must be enabled.", _Tr("StartupScreen", "Global Illumination"))));

				{
					StartupScreenComplexConfig cplx;
					// r_mapSoftShadow is currently no-op
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_dlights"), "0", "1",
						_Tr("StartupScreen", "Dynamic Lights"),
						_Tr("StartupScreen", "Gives some objects an ability to emit light to give them " "an energy-emitting impression.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_modelShadows"), "0", "1",
						_Tr("StartupScreen", "Shadows"),
						_Tr("StartupScreen", "Non-static object casts a shadow.")));
					cplx.AddEditor(StartupScreenConfigCheckItemEditor(
						ui, StartupScreenConfig(ui, "r_physicalLighting"), "0", "1",
						_Tr("StartupScreen", "Physically Based Lighting"),
						_Tr("StartupScreen", "Uses more accurate approximation techniques to decide the brightness of objects.")));

					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Low"), "1|0|0"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Medium"), "1|1|0"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "High"), "1|1|1"));

					cfg.AddRow(StartupScreenConfigComplexItemEditor(
						ui, cplx, _Tr("StartupScreen", "Direct Lights"),
						_Tr("StartupScreen", "Controls how light encounting a material and atmosphere directly " "affects its appearance.")));
				}

				{
					StartupScreenComplexConfig cplx;

					cplx.AddEditor(StartupScreenConfigSelectItemEditor(
						ui, StartupScreenConfig(ui, "r_water"), "0|1|2|3",
						_Tr("StartupScreen",
							"Water Shader|" "None:Water is rendered in the same way that normal blocks are done.|" "Level 1:Refraction and the reflected Sun are simulated.|" "Level 2:Waving water is simulated as well as reflection and refraction.|" "Level 3:Reflections and refractions are rendered at the highest quality using screen-space techniques.")));

					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Low"), "0"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Med"), "1"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "High"), "2"));
					cplx.AddPreset(StartupScreenComplexConfigPreset(_Tr("StartupScreen", "Ultra"), "3"));

					cfg.AddRow(StartupScreenConfigComplexItemEditor(
						ui, cplx, _Tr("StartupScreen", "Shader Effects"),
						_Tr("StartupScreen", "Special effects.")));
				}

				cfg.Finalize();
				cfg.SetHelpTextHandler(HelpTextHandler(this.HandleHelpText));
				cfg.Bounds = AABB2(0.0F, 60.0F, mainWidth, size.y - 60.0F);
				AddChild(cfg);
				@configViewGL = cfg;
			}

			{
				StartupScreenConfigView cfg(Manager);

				cfg.AddRow(StartupScreenConfigSelectItemEditor(
					ui, StartupScreenConfig(ui, "r_swUndersampling"), "0|1|2",
					_Tr("StartupScreen", "Fast Mode:Reduces the image resolution to make the rendering faster.|" "Off|2x|4x")));

				cfg.Finalize();
				cfg.SetHelpTextHandler(HelpTextHandler(this.HandleHelpText));
				cfg.Bounds = AABB2(0.0F, 60.0F, mainWidth, size.y - 60.0F);
				AddChild(cfg);
				@configViewSoftware = cfg;
			}
		}

		private void HandleHelpText(string text) { helpView.Text = text; }

		private void OnDriverOpenGL(spades::ui::UIElement@) {
			r_renderer.StringValue = "gl";
			LoadConfig();
		}
		private void OnDriverSoftware(spades::ui::UIElement@) {
			r_renderer.StringValue = "sw";
			LoadConfig();
		}

		private void OnFullscreenCheck(spades::ui::UIElement@) {
			r_fullscreen.IntValue = fullscreenCheck.Toggled ? 1 : 0;
		}

		void LoadConfig() {
			resEdit.LoadConfig();
			if (r_renderer.StringValue == "sw") {
				driverSoftware.Check();
				configViewGL.Visible = false;
				configViewSoftware.Visible = true;
			} else {
				driverOpenGL.Check();
				configViewGL.Visible = true;
				configViewSoftware.Visible = false;
			}
			fullscreenCheck.Toggled = r_fullscreen.IntValue != 0;
			driverOpenGL.Enable = ui.helper.CheckConfigCapability("r_renderer", "gl").length == 0;
			driverSoftware.Enable = ui.helper.CheckConfigCapability("r_renderer", "sw").length == 0;
			configViewGL.LoadConfig();
			configViewSoftware.LoadConfig();
		}
	}

	class StartupScreenComboBoxDropdownButton : spades::ui::SimpleButton {
		StartupScreenComboBoxDropdownButton(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			SimpleButton::Render();

			Renderer@ r = Manager.Renderer;
			Image@ img = r.RegisterImage("Gfx/UI/ScrollArrow.png");
			AABB2 bnd = ScreenBounds;
			Vector2 p = (bnd.min + bnd.max) * 0.5F + Vector2(-8.0F, 8.0F);
			r.DrawImage(img, AABB2(p.x, p.y, 16.0F, -16.0F));
		}
	}

	class StartupScreenGraphicsDisplayResolutionEditor : spades::ui::UIElement {
		spades::ui::Field@ widthField;
		spades::ui::Field@ heightField;
		spades::ui::Button@ dropdownButton;
		private ConfigItem r_videoWidth("r_videoWidth");
		private ConfigItem r_videoHeight("r_videoHeight");
		StartupScreenHelper@ helper;

		StartupScreenGraphicsDisplayResolutionEditor(StartupScreenUI@ ui) {
			super(ui.manager);
			@helper = ui.helper;
			{
				spades::ui::Field e(Manager);
				AddChild(e);
				e.Bounds = AABB2(0, 0, 45.0F, 24.0F);
				e.TextOrigin.y *= 0.5F;
				e.DenyNonAscii = true;
				@e.Changed = spades::ui::EventHandler(this.ValueEditHandler);
				@widthField = e;
			}
			{
				spades::ui::Field e(Manager);
				AddChild(e);
				e.Bounds = AABB2(53, 0, 45.0F, 24.0F);
				e.TextOrigin.y *= 0.5F;
				e.DenyNonAscii = true;
				@e.Changed = spades::ui::EventHandler(this.ValueEditHandler);
				@heightField = e;
			}
			{
				StartupScreenComboBoxDropdownButton e(Manager);
				AddChild(e);
				e.Bounds = AABB2(100, 0, 24.0F, 24.0F);
				@e.Activated = spades::ui::EventHandler(this.ShowDropdown);
				@dropdownButton = e;
			}
		}

		void LoadConfig() {
			widthField.Text = ToString(r_videoWidth.IntValue);
			heightField.Text = ToString(r_videoHeight.IntValue);
		}

		void SaveConfig() {
			int w = ParseInt(widthField.Text);
			int h = ParseInt(heightField.Text);
			if (w < 640 or h < 480 or w > 8192 or h > 8192)
				return;
			r_videoWidth.IntValue = w;
			r_videoHeight.IntValue = h;
		}

		private void ValueEditHandler(spades::ui::UIElement@) { SaveConfig(); }

		private void DropdownHandler(int index) {
			if (index >= 0) {
				widthField.Text = ToString(helper.GetVideoModeWidth(index));
				heightField.Text = ToString(helper.GetVideoModeHeight(index));
				SaveConfig();
			}
		}

		private void ShowDropdown(spades::ui::UIElement@) {
			string[] items = {};
			int cnt = helper.GetNumVideoModes();
			for (int i = 0; i < cnt; i++) {
				int w = helper.GetVideoModeWidth(i);
				int h = helper.GetVideoModeHeight(i);
				string s = ToString(w) + "x" + ToString(h);
				items.insertLast(s);
			}
			spades::ui::ShowDropDownList(this, items, spades::ui::DropDownListHandler(this.DropdownHandler));
		}
		void Render() {
			Font@ font = this.Font;
			font.Draw("x", Vector2(45.0F, 0.0F) + ScreenPosition, 1.0F, Vector4(1.0F, 1.0F, 1.0F, 1.0F));
			UIElement::Render();
		}
	}

	class StartupScreenGraphicsAntialiasConfig : StartupScreenGenericConfig {
		private StartupScreenUI@ ui;
		private ConfigItem@ msaaConfig;
		private ConfigItem@ fxaaConfig;
		StartupScreenGraphicsAntialiasConfig(StartupScreenUI@ ui) {
			@this.ui = ui;
			@msaaConfig = ConfigItem("r_multisamples");
			@fxaaConfig = ConfigItem("r_fxaa");
		}
		string GetValue() {
			if (fxaaConfig.IntValue != 0) {
				return "fxaa";
			} else {
				int v = msaaConfig.IntValue;
				if (v < 2)
					return "0";
				else
					return msaaConfig.StringValue;
			}
		}
		void SetValue(string v) {
			if (v == "fxaa") {
				msaaConfig.StringValue = "0";
				fxaaConfig.StringValue = "1";
			} else if (v == "0" || v == "1") {
				msaaConfig.StringValue = "0";
				fxaaConfig.StringValue = "0";
			} else {
				msaaConfig.StringValue = v;
				fxaaConfig.StringValue = "0";
			}
		}
		string CheckValueCapability(string v) {
			if (v == "fxaa") {
				return ui.helper.CheckConfigCapability("r_multisamples", "0") +
					ui.helper.CheckConfigCapability("r_fxaa", "1");
			} else if (v == "0" || v == "1") {
				return ui.helper.CheckConfigCapability("r_multisamples", "0") +
					ui.helper.CheckConfigCapability("r_fxaa", "0");
			} else {
				return ui.helper.CheckConfigCapability("r_multisamples", v) +
					ui.helper.CheckConfigCapability("r_fxaa", "0");
			}
		}
	}

	class StartupScreenAudioTab : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;

		spades::ui::RadioButton@ driverOpenAL;
		spades::ui::RadioButton@ driverNull;

		spades::ui::TextViewer@ helpView;
		StartupScreenConfigView@ configViewOpenAL;

		StartupScreenAudioOpenALEditor@ editOpenAL;

		private ConfigItem s_audioDriver("s_audioDriver");
		private ConfigItem s_eax("s_eax");
		private ConfigItem s_openalDevice("s_openalDevice");

		StartupScreenAudioTab(StartupScreenUI@ ui, Vector2 size) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;

			float mainWidth = size.x - 250.0F;

			{
				spades::ui::TextViewer e(Manager);
				e.Bounds = AABB2(mainWidth + 10.0F, 0.0F, size.x - mainWidth - 10.0F, size.y);
				@e.Font = ui.fontManager.GuiFont;
				e.Text = _Tr("StartupScreen", "Audio Settings");
				AddChild(e);
				@helpView = e;
			}

			AddLabel(0.0F, 0.0F, 24.0F, _Tr("StartupScreen", "Backend"));
			{
				spades::ui::RadioButton e(Manager);
				e.Caption = _Tr("StartupScreen", "OpenAL");
				e.Bounds = AABB2(100.0F, 0.0F, 100.0F, 24.0F);
				e.GroupName = "driver";
				HelpHandler(
					helpView,
					_Tr("StartupScreen",
						"Uses an OpenAL-capable sound card to process sound. "
						"In most cases where there isn't such a sound card, software emulation is "
						"used."))
					.Watch(e);
				@e.Activated = spades::ui::EventHandler(this.OnDriverOpenAL);
				AddChild(e);
				@driverOpenAL = e;
			}
			{
				spades::ui::RadioButton e(Manager);
				//! The name of audio driver that outputs no audio.
				e.Caption = _Tr("StartupScreen", "Null");
				e.Bounds = AABB2(210.0F, 0.0F, 100.0F, 24.0F);
				e.GroupName = "driver";
				HelpHandler(helpView, _Tr("StartupScreen", "Disables audio output.")).Watch(e);
				@e.Activated = spades::ui::EventHandler(this.OnDriverNull);
				AddChild(e);
				@driverNull = e;
			}
			{
				StartupScreenConfigView cfg(Manager);

				cfg.AddRow(StartupScreenConfigSliderItemEditor(
					ui, StartupScreenConfig(ui, "s_volume"), 0, 100, 1,
					_Tr("StartupScreen", "Volume"), "",
					ConfigNumberFormatter(0, "%")));

				cfg.AddRow(StartupScreenConfigSliderItemEditor(
					ui, StartupScreenConfig(ui, "s_maxPolyphonics"), 16.0, 256.0, 8.0,
					_Tr("StartupScreen", "Polyphonics"),
					_Tr("StartupScreen",
						"Specifies how many sounds can be played simultaneously. "
						"Higher value needs more processing power, so setting this too high might "
						"cause an overload (especially with a software emulation)."),
					ConfigNumberFormatter(0, " poly")));

				cfg.AddRow(StartupScreenConfigCheckItemEditor(
					ui, StartupScreenConfig(ui, "s_eax"), "0", "1", _Tr("StartupScreen", "EAX"),
					_Tr("StartupScreen",
						"Enables extended features provided by the OpenAL driver to create "
						"more ambience.")));
						
				cfg.Finalize();
				cfg.SetHelpTextHandler(HelpTextHandler(this.HandleHelpText));
				cfg.Bounds = AABB2(0.0F, 30.0F, mainWidth, size.y - 30.0F);
				AddChild(cfg);
				@configViewOpenAL = cfg;
			}

			AddLabel(0.0F, 120.0F, 24.0F, _Tr("StartupScreen", "Output Device"));
			{
				StartupScreenAudioOpenALEditor e(ui);
				AddChild(e);
				e.Bounds = AABB2(160.0F, 120.0F, 354.0F, 24.0F);
				@editOpenAL = e;
			}
		}

		private void HandleHelpText(string text) { helpView.Text = text; }

		private void OnDriverOpenAL(spades::ui::UIElement@) {
			s_audioDriver.StringValue = "openal";
			LoadConfig();
		}
		private void OnDriverNull(spades::ui::UIElement@) {
			s_audioDriver.StringValue = "null";
			LoadConfig();
		}

		void LoadConfig() {
			if (s_audioDriver.StringValue == "openal") {
				driverOpenAL.Check();
				configViewOpenAL.Visible = true;
			} else if (s_audioDriver.StringValue == "null") {
				driverNull.Check();
				configViewOpenAL.Visible = false;
			}
			driverOpenAL.Enable = ui.helper.CheckConfigCapability("s_audioDriver", "openal").length == 0;
			driverNull.Enable = ui.helper.CheckConfigCapability("s_audioDriver", "null").length == 0;
			configViewOpenAL.LoadConfig();
			editOpenAL.LoadConfig();

			s_openalDevice.StringValue = editOpenAL.openal.StringValue;
		}
	}

	class StartupScreenAudioOpenALEditor : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;
		ConfigItem openal("openal");

		spades::ui::Button@ dropdownButton;

		StartupScreenAudioOpenALEditor(StartupScreenUI@ ui) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;
			
			{
				StartupScreenDropdownListDropdownButton e(Manager);
				AddChild(e);
				e.Bounds = AABB2(0.0F, 0.0F, 354.0F, 24.0F);
				@e.Activated = spades::ui::EventHandler(this.ShowDropdown);
				@dropdownButton = e;
			}
		}

		void LoadConfig() {
			string drivername = openal.StringValue;
			string name = _Tr("StartupScreen", "System default", drivername);
			if (drivername == "")
				name = _Tr("StartupScreen", "System default");

			int cnt = helper.GetNumAudioOpenALDevices();
			for (int i = 0; i < cnt; i++) {
				if (drivername == helper.GetAudioOpenALDevice(i))
					name = helper.GetAudioOpenALDevice(i);
			}

			dropdownButton.Caption = name;
		}

		private void ShowDropdown(spades::ui::UIElement@) {
			string[] items = {_Tr("StartupScreen", "System default")};
			int cnt = helper.GetNumAudioOpenALDevices();
			for (int i = 0; i < cnt; i++) {
				string s = helper.GetAudioOpenALDevice(i);
				items.insertLast(s);
			}
			spades::ui::ShowDropDownList(this, items,
					spades::ui::DropDownListHandler(this.DropdownHandler));
		}

		private void DropdownHandler(int index) {
			if (index >= 0) {
				if (index == 0)
					openal = "default";
				else
					openal = helper.GetAudioOpenALDevice(index - 1);

				// Reload the startup screen so the language config takes effect
				ui.Reload();
			}
		}
	}

	class StartupScreenGenericTab : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;
		StartupScreenLocaleEditor@ editLocale;
		private spades::ui::CheckBox@ buttonAutoRecord;
		private spades::ui::CheckBox@ buttonAutoPrune;
		private spades::ui::Field@ fieldMaxDemos;
		private ConfigItem cg_demoAutoRecord("cg_demoAutoRecord");
		private ConfigItem cg_demoAutoPrune("cg_demoAutoPrune");
		private ConfigItem cg_demoMaxFiles("cg_demoMaxFiles");

		StartupScreenGenericTab(StartupScreenUI@ ui, Vector2 size) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;

			string label = _Tr("StartupScreen", "Language");
			if (label != "Language")
				label += " (Language)";

			AddLabel(0.0F, 0.0F, 24.0F, _Tr("StartupScreen", label));
			{
				StartupScreenLocaleEditor e(ui);
				AddChild(e);
				e.Bounds = AABB2(160.0F, 0.0F, 348.0F, 24.0F);
				@editLocale = e;
			}

			AddLabel(0.0F, 30.0F, 30.0F, _Tr("StartupScreen", "Tools"));
			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("StartupScreen", "Reset All Settings");
				button.Bounds = AABB2(160.0F, 30.0F, 350.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnResetSettingsPressed);
				AddChild(button);
			}
			{
				spades::ui::Button button(Manager);
				string osType = helper.OperatingSystemType;
				if (osType == "Windows") {
					button.Caption = _Tr("StartupScreen", "Open Config Folder in Explorer");
				} else if (osType == "Mac") {
					button.Caption = _Tr("StartupScreen", "Reveal Config Folder in Finder");
				} else {
					button.Caption = _Tr("StartupScreen", "Browse Config Folder");
				}
				button.Bounds = AABB2(160.0F, 66.0F, 350.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnBrowseUserDirectoryPressed);
				AddChild(button);
			}

			AddLabel(0.0F, 108.0F, 24.0F, _Tr("StartupScreen", "Demo Recording"));
			{
				spades::ui::CheckBox chk(Manager);
				chk.Caption = _Tr("StartupScreen", "Automatically record every game");
				chk.Bounds = AABB2(160.0F, 108.0F, 350.0F, 24.0F);
				@chk.Activated = spades::ui::EventHandler(this.OnAutoRecordChanged);
				AddChild(chk);
				@buttonAutoRecord = chk;
			}

			AddLabel(0.0F, 138.0F, 24.0F, _Tr("StartupScreen", "Auto-delete old demos"));
			{
				spades::ui::CheckBox chk(Manager);
				chk.Caption = _Tr("StartupScreen", "Delete oldest demos when limit is reached");
				chk.Bounds = AABB2(160.0F, 138.0F, 350.0F, 24.0F);
				@chk.Activated = spades::ui::EventHandler(this.OnAutoPruneChanged);
				AddChild(chk);
				@buttonAutoPrune = chk;
			}

			AddLabel(0.0F, 168.0F, 24.0F, _Tr("StartupScreen", "Max stored demos"));
			{
				spades::ui::Field fld(Manager);
				fld.Bounds = AABB2(160.0F, 168.0F, 80.0F, 24.0F);
				fld.DenyNonAscii = true;
				@fld.Changed = spades::ui::EventHandler(this.OnMaxDemosChanged);
				AddChild(fld);
				@fieldMaxDemos = fld;
			}

			AddLabel(0.0F, 204.0F, 30.0F, _Tr("StartupScreen", "Demos Folder"));
			{
				spades::ui::Button button(Manager);
				string osType = helper.OperatingSystemType;
				if (osType == "Windows") {
					button.Caption = _Tr("StartupScreen", "Open Demos Folder in Explorer");
				} else if (osType == "Mac") {
					button.Caption = _Tr("StartupScreen", "Reveal Demos Folder in Finder");
				} else {
					button.Caption = _Tr("StartupScreen", "Browse Demos Folder");
				}
				button.Bounds = AABB2(160.0F, 204.0F, 350.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnBrowseDemosFolderPressed);
				AddChild(button);
			}
		}

		void LoadConfig() {
			editLocale.LoadConfig();
			buttonAutoRecord.Toggled = cg_demoAutoRecord.IntValue != 0;
			buttonAutoPrune.Toggled = cg_demoAutoPrune.IntValue != 0;
			fieldMaxDemos.Text = cg_demoMaxFiles.StringValue;
		}

		private void OnAutoRecordChanged(spades::ui::UIElement@) {
			cg_demoAutoRecord.IntValue = buttonAutoRecord.Toggled ? 1 : 0;
		}

		private void OnAutoPruneChanged(spades::ui::UIElement@) {
			cg_demoAutoPrune.IntValue = buttonAutoPrune.Toggled ? 1 : 0;
		}

		private void OnMaxDemosChanged(spades::ui::UIElement@) {
			int v = ParseInt(fieldMaxDemos.Text);
			if (v >= 1)
				cg_demoMaxFiles.IntValue = v;
		}

		private void OnBrowseDemosFolderPressed(spades::ui::UIElement@) {
			if (helper.BrowseDemosDirectory())
				return;

			string msg = _Tr("StartupScreen",
							 "An unknown error has occurred while opening the demos directory.");
			AlertScreen al(Parent, msg, 100.0F);
			al.Run();
		}

		private void OnBrowseUserDirectoryPressed(spades::ui::UIElement@) {
			if (helper.BrowseUserDirectory())
				return;

			string msg = _Tr("StartupScreen",
							 "An unknown error has occurred while opening the config directory.");
			AlertScreen al(Parent, msg, 100.0F);
			al.Run();
		}

		private void OnResetSettingsPressed(spades::ui::UIElement@) {
			string msg =
				_Tr("StartupScreen",
					"Are you sure to reset all settings? They include (but are not limited to):") +
				"\n"
				+ "- " + _Tr("StartupScreen", "All graphics/audio settings") + "\n"
				+ "- " + _Tr("StartupScreen", "All key bindings") + "\n"
				+ "- " + _Tr("StartupScreen", "Your player name") + "\n"
				+ "- " +
				_Tr("StartupScreen",
					"Other advanced settings only accessible through '{0}' tab and in-game commands.",
					_Tr("StartupScreen", "Advanced"));
			ConfirmScreen al(Parent, msg, 200.0F);
			@al.Closed = spades::ui::EventHandler(OnResetSettingsConfirmed);
			al.Run();
		}

		private void OnResetSettingsConfirmed(spades::ui::UIElement@ sender) {
			ConfirmScreen@ confirm = cast<ConfirmScreen>(sender);
			if (confirm is null || !confirm.Result)
				return;

			ResetAllSettings();

			// Reload the startup screen so the language config takes effect
			ui.Reload();
		}

		private void ResetAllSettings() {
			string[]@ names = GetAllConfigNames();
			for (uint i = 0, count = names.length; i < count; i++) {
				ConfigItem item(names[i]);
				item.StringValue = item.DefaultValue;
			}

			// Some of default values may be infeasible for the user's system.
			helper.FixConfigs();
		}
	}

	class StartupScreenDropdownListDropdownButton : spades::ui::SimpleButton {
		StartupScreenDropdownListDropdownButton(spades::ui::UIManager@ manager) {
			super(manager);
			Alignment = Vector2(0.0F, 0.5F);
		}
		void Render() {
			SimpleButton::Render();

			Renderer@ r = Manager.Renderer;
			Image@ img = r.RegisterImage("Gfx/UI/ScrollArrow.png");
			AABB2 bnd = ScreenBounds;
			Vector2 p = (bnd.min + bnd.max) * 0.5F + Vector2(-8.0F, 8.0F);
			r.DrawImage(img, AABB2(bnd.max.x - 16.0F, p.y, 16.0F, -16.0F));
		}
	}

	class StartupScreenLocaleEditor : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;
		private ConfigItem core_locale("core_locale");

		spades::ui::Button@ dropdownButton;

		StartupScreenLocaleEditor(StartupScreenUI@ ui) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;
			
			{
				StartupScreenDropdownListDropdownButton e(Manager);
				AddChild(e);
				e.Bounds = AABB2(0.0F, 0.0F, 348.0F, 24.0F);
				@e.Activated = spades::ui::EventHandler(this.ShowDropdown);
				@dropdownButton = e;
			}
		}

		void LoadConfig() {
			string locale = core_locale.StringValue;
			string name = _Tr("StartupScreen", "Unknown ({0})", locale);
			if (locale == "")
				name = _Tr("StartupScreen", "System default");

			int cnt = helper.GetNumLocales();
			for (int i = 0; i < cnt; i++) {
				if (locale == helper.GetLocale(i)) {
					name = helper.GetLocaleDescriptionNative(i) + " / " +
						   helper.GetLocaleDescriptionEnglish(i);
				}
			}

			dropdownButton.Caption = name;
		}

		private void ShowDropdown(spades::ui::UIElement@) {
			string[] items = {_Tr("StartupScreen", "System default")};
			int cnt = helper.GetNumLocales();
			for (int i = 0; i < cnt; i++) {
				string s = helper.GetLocaleDescriptionNative(i) + " / " +
						   helper.GetLocaleDescriptionEnglish(i);
				items.insertLast(s);
			}
			spades::ui::ShowDropDownList(this, items, spades::ui::DropDownListHandler(this.DropdownHandler));
		}

		private void DropdownHandler(int index) {
			if (index >= 0) {
				if (index == 0) {
					core_locale = "";
				} else {
					core_locale = helper.GetLocale(index - 1);
				}

				// Reload the startup screen so the language config takes effect
				ui.Reload();
			}
		}
	}

	class StartupScreenSystemInfoTab : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;
		spades::ui::TextViewer@ helpView;

		StartupScreenSystemInfoTab(StartupScreenUI@ ui, Vector2 size) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;

			{
				spades::ui::TextViewer e(Manager);
				e.Bounds = AABB2(0.0F, 0.0F, size.x, size.y - 30.0F);
				@e.Font = ui.fontManager.GuiFont;
				AddChild(e);
				for (int i = 0, count = helper.GetNumReportLines(); i < count; i++) {
					string text = helper.GetReportLineText(i);
					Vector4 col = helper.GetReportLineColor(i);
					e.AddLine(text, true, col);
				}
				@helpView = e;
			}

			{
				spades::ui::Button button(Manager);
				button.Caption = _Tr("StartupScreen", "Copy to Clipboard");
				button.Bounds = AABB2(size.x - 180.0F, size.y - 30.0F, 180.0F, 30.0F);
				@button.Activated = spades::ui::EventHandler(this.OnCopyReport);
				AddChild(button);
			}
		}
		private void OnCopyReport(spades::ui::UIElement@) { Manager.Copy(helper.GetReport()); }
	}

	class StartupScreenAdvancedTab : spades::ui::UIElement, LabelAddable {
		StartupScreenUI@ ui;
		StartupScreenHelper@ helper;

		spades::ui::Field@ filter;
		spades::ui::TextViewer@ helpView;
		StartupScreenConfigView@ configView;

		StartupScreenAdvancedTab(StartupScreenUI@ ui, Vector2 size) {
			super(ui.manager);
			@this.ui = ui;
			@helper = ui.helper;

			float mainWidth = size.x - 250.0F;

			{
				spades::ui::TextViewer e(Manager);
				e.Bounds = AABB2(mainWidth + 10.0F, 0.0F, size.x - mainWidth - 10.0F, size.y);
				@e.Font = ui.fontManager.GuiFont;
				e.Text = _Tr("StartupScreen", "Advanced Settings");
				e.Visible = false;
				AddChild(e);
				@helpView = e;
			}

			{
				spades::ui::Field e(Manager);
				e.Placeholder = _Tr("StartupScreen", "Filter");
				e.Bounds = AABB2(0.0F, 0.0F, size.x, 24.0F);
				e.TextOrigin *= 0.5F;
				@e.Changed = spades::ui::EventHandler(this.OnFilterChanged);
				AddChild(e);
				@filter = e;
			}

			{
				StartupScreenConfigView cfg(Manager);

				string[]@ names = GetAllConfigNames();
				for (uint i = 0, count = names.length; i < count; i++) {
					cfg.AddRow(StartupScreenConfigFieldItemEditor(
						ui, StartupScreenConfig(ui, names[i]), names[i], ""));
				}

				cfg.Finalize();
				cfg.SetHelpTextHandler(HelpTextHandler(this.HandleHelpText));
				cfg.Bounds = AABB2(0.0F, 30.0F, size.x, size.y - 30.0F);
				AddChild(cfg);
				@configView = cfg;
			}
		}

		private void HandleHelpText(string text) { helpView.Text = text; }
		private void OnFilterChanged(spades::ui::UIElement@) { configView.Filter(filter.Text); }

		void LoadConfig() { configView.LoadConfig(); }
	}
}
