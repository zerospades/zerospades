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

namespace spades {

	// Passed in through PreferenceViewOptions rather than held as a script
	// global because the script engine disallows them (asEP_DISALLOW_GLOBAL_VARS).
	class PreferenceViewPersistedState {
		int SelectedTabIndex = 0;
		// Stored as decimal strings: AngelScript 2.38 fails to resolve `length`
		// as a property accessor on array<primitive> in this build.
		array<string> TabScrollRows;
	}

	class PreferenceViewOptions {
		bool GameActive = false;
		// Null means "don't remember position" — Restore/Save no-op.
		PreferenceViewPersistedState@ PersistedState;
	}

	class PreferenceView : spades::ui::UIElement {
		private spades::ui::UIElement@ owner;
		private PreferenceViewPersistedState@ persistedState;

		private PreferenceTab @[] tabs;

		float ContentsLeft, ContentsWidth;
		float ContentsTop, ContentsHeight;
		float ContentsRight;

		float tabLeft, tabRight, tabWidth;
		float tabTop, tabRowHeight;
		float panelTop, panelBottom;

		int SelectedTabIndex = 0;

		spades::ui::EventHandler@ Closed;

		PreferenceView(spades::ui::UIElement@ owner,
			PreferenceViewOptions@ options, FontManager@ fontManager) {
			super(owner.Manager);
			@this.owner = owner;
			@this.persistedState = options.PersistedState;
			this.Bounds = owner.Bounds;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			ContentsWidth = sw - 16.0F;
			float maxContentsWidth = 800.0F;
			if (ContentsWidth > maxContentsWidth)
				ContentsWidth = maxContentsWidth;

			ContentsHeight = sh - 8.0F;
			float maxContentsHeight = 550.0F;
			if (ContentsHeight > maxContentsHeight)
				ContentsHeight = maxContentsHeight;

			ContentsTop = (sh - ContentsHeight) * 0.5F;
			ContentsLeft = (sw - ContentsWidth) * 0.5F;
			ContentsRight = ContentsLeft + ContentsWidth;

			tabWidth = 150.0F;
			tabRowHeight = 30.0F;
			tabTop = ContentsTop + 2.0F;
			tabLeft = ContentsLeft + 2.0F;
			tabRight = tabLeft + tabWidth;

			float panelBorderOffset = 14.0F;
			panelTop = ContentsTop - panelBorderOffset;
			panelBottom = ContentsTop + ContentsHeight + panelBorderOffset;

			{
				spades::ui::Label label(Manager);
				label.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				label.Bounds = AABB2(0.0F, panelTop + 1.0F, Size.x, (panelBottom - panelTop) - 1.0F);
				AddChild(label);
			}

			HeadingNavIndex gameNav();
			HeadingNavIndex hudNav();
			HeadingNavIndex rendererNav();
			HeadingNavIndex controlsNav();
			//HeadingNavIndex miscNav();

			AddTab(GameOptionsPanel(Manager, options, fontManager, gameNav), _Tr("Preferences", "Game Options"), gameNav);
			InterfaceOptions@ interfacePanel = InterfaceOptions(Manager, options, fontManager, hudNav);
			@interfacePanel.OnEditHUDRequested = spades::ui::EventHandler(this.OnEditHUDRequested);
			AddTab(interfacePanel, _Tr("Preferences", "Interface"), hudNav);
			AddTab(RendererOptionsPanel(Manager, options, fontManager, rendererNav), _Tr("Preferences", "Graphics"), rendererNav);
			AddTab(ControlOptionsPanel(Manager, options, fontManager, controlsNav), _Tr("Preferences", "Controls"), controlsNav);
			AddTab(MiscOptionsPanel(Manager, options, fontManager), _Tr("Preferences", "Misc"));

			float backButtonTop = tabTop + float(tabs.length) * tabRowHeight + 5.0F;

			{
				PreferenceTabButton button(Manager);
				button.Caption = _Tr("Preferences", "Back");
				button.HotKeyText = _Tr("Client", "[Esc]");
				button.Bounds = AABB2(tabLeft, backButtonTop, tabWidth, tabRowHeight);
				@button.Activated = spades::ui::EventHandler(this.OnClosePressed);
				AddChild(button);
			}

			// build navigation containers in the sidebar
			{
				float btnH = 24.0F;
				float btnY = backButtonTop + tabRowHeight + btnH;
				float btnGap = 2.0F;

				for (uint i = 0; i < tabs.length; i++) {
					PreferenceTab@ tab = tabs[i];

					HeadingNavIndex@ nav = tab.HeadingNav;
					if (nav is null)
						continue;

					int numHeadings = nav.entries.length;
					if (numHeadings < 2)
						continue;

					float height = float(numHeadings) * (btnH + btnGap) - btnGap;

					spades::ui::UIElement container(Manager);
					container.Bounds = AABB2(tabLeft, btnY, tabWidth, height);
					container.Visible = false;

					for (int j = 0; j < numHeadings; j++) {
						HeadingNavEntry ent = nav.entries[j];
						HeadingNavButton btn(Manager);
						btn.Caption = ent.caption;
						btn.SetTarget(nav.list, ent.row);
						btn.Bounds = AABB2(0.0F, float(j) * (btnH + btnGap), tabWidth, btnH);
						container.AddChild(btn);
					}

					AddChild(container);
					@tab.NavPanel = container;
				}
			}

			// Must run after the panel constructors above: each one calls
			// FinishLayout() which sets the ListView's bounds, so MaxValue
			// is correct by the time ScrollToRow clamps against it.
			RestorePersistedState();
			UpdateTabs();
		}

		private spades::ui::ListView@ GetTabList(int idx) {
			if (idx < 0 or idx >= int(tabs.length))
				return null;
			spades::ui::UIElement@ view = tabs[idx].View;
			if (view is null)
				return null;
			spades::ui::UIElement @[] @children = view.GetChildren();
			for (uint i = 0; i < children.length; i++) {
				spades::ui::ListView@ list = cast<spades::ui::ListView>(children[i]);
				if (list !is null)
					return list;
			}
			return null;
		}

		private void RestorePersistedState() {
			if (persistedState is null)
				return;

			if (persistedState.SelectedTabIndex >= 0 and persistedState.SelectedTabIndex < int(tabs.length))
				SelectedTabIndex = persistedState.SelectedTabIndex;

			uint limit = tabs.length;
			if (persistedState.TabScrollRows.length < limit)
				limit = persistedState.TabScrollRows.length;
			for (uint i = 0; i < limit; i++) {
				spades::ui::ListView@ list = GetTabList(int(i));
				if (list !is null)
					list.ScrollToRow(int(parseInt(persistedState.TabScrollRows[i])));
			}
		}

		private void SavePersistedState() {
			if (persistedState is null)
				return;

			persistedState.SelectedTabIndex = SelectedTabIndex;
			persistedState.TabScrollRows.resize(tabs.length);
			for (uint i = 0; i < tabs.length; i++) {
				spades::ui::ListView@ list = GetTabList(int(i));
				persistedState.TabScrollRows[i] = "" + ((list !is null) ? list.TopRowIndex : 0);
			}
		}

		private void AddTab(spades::ui::UIElement@ view, string caption, HeadingNavIndex@ nav = null) {
			PreferenceTab tab(this, view);
			tab.TabButton.Caption = caption;
			tab.TabButton.Bounds = AABB2(tabLeft, tabTop + float(tabs.length) * tabRowHeight, tabWidth, tabRowHeight);
			@tab.TabButton.Activated = spades::ui::EventHandler(this.OnTabButtonActivated);
			tab.View.Bounds = AABB2(tabRight, tabTop, ContentsRight - tabRight, ContentsHeight - 6.0F);
			tab.View.Visible = false;
			@tab.HeadingNav = nav;
			AddChild(tab.View);
			AddChild(tab.TabButton);
			tabs.insertLast(tab);
		}

		private void OnTabButtonActivated(spades::ui::UIElement@ sender) {
			for (uint i = 0; i < tabs.length; i++) {
				if (cast<spades::ui::UIElement>(tabs[i].TabButton) is sender) {
					SelectedTabIndex = i;
					UpdateTabs();
				}
			}
		}

		private void UpdateTabs() {
			for (uint i = 0; i < tabs.length; i++) {
				bool selected = SelectedTabIndex == int(i);
				tabs[i].TabButton.Toggled = selected;
				tabs[i].View.Visible = selected;
				if (tabs[i].NavPanel !is null)
					tabs[i].NavPanel.Visible = selected;
			}
		}

		private void OnClosePressed(spades::ui::UIElement@ sender) { Close(); }

		private void OnEditHUDRequested(spades::ui::UIElement@ sender) {
			this.Visible = false;
			ConfigHUDEdit overlay(Manager);
			@overlay.OnDone = spades::ui::EventHandler(this.OnHUDEditDone);
			Parent.AddChild(overlay);
		}

		private void OnHUDEditDone(spades::ui::UIElement@ sender) {
			@sender.Parent = null;
			this.Visible = true;
		}

		private void OnClosed() {
			if (Closed !is null)
				Closed(this);
		}

		void HotKey(string key) {
			if (key == "Escape") {
				Close();
			} else {
				UIElement::HotKey(key);
			}
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			//r.ColorNP = Vector4(1.0F, 0.0F, 0.0F, 1.0F);
			//r.DrawImage(null, AABB2(tabRight, tabTop, ContentsRight - tabRight, ContentsHeight - 6.0F));

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.07F);
			r.DrawImage(null, AABB2(pos.x, pos.y + panelTop, size.x, 1.0F));
			r.DrawImage(null, AABB2(pos.x, pos.y + panelBottom, size.x, 1.0F));

			UIElement::Render();
		}

		void Close() {
			// Both Esc and the Back button route through Close(), so this
			// is the single save point for the persisted position.
			SavePersistedState();
			owner.Enable = true;
			@this.Parent = null;
			OnClosed();
		}

		void Run() {
			owner.Enable = false;
			owner.Parent.AddChild(this);
		}
	}

	class PreferenceTabButton : spades::ui::Button {
		PreferenceTabButton(spades::ui::UIManager@ manager) {
			super(manager);
			Alignment = Vector2(0.0F, 0.5F);
		}
	}

	class PreferenceTab {
		spades::ui::UIElement@ View;
		PreferenceTabButton@ TabButton;

		HeadingNavIndex@ HeadingNav;
		spades::ui::UIElement@ NavPanel;

		PreferenceTab(PreferenceView@ parent, spades::ui::UIElement@ view) {
			@View = view;
			@TabButton = PreferenceTabButton(parent.Manager);
			TabButton.Toggle = true;
		}
	}

	class HeadingNavEntry {
		string caption;
		int row;
	}
	class HeadingNavIndex {
		HeadingNavEntry[] entries;
		spades::ui::ListView@ list;
	}
	class HeadingNavButton : spades::ui::SimpleButton {
		private spades::ui::ListView@ targetList;
		private int targetRow;

		HeadingNavButton(spades::ui::UIManager@ manager) {
			super(manager);
		}

		void SetTarget(spades::ui::ListView@ list, int row) {
			@targetList = list;
			targetRow = row;
		}

		void OnActivated() {
			if (targetList !is null)
				targetList.ScrollToRow(targetRow);
		}
	}

	class ConfigHUDEdit : spades::ui::UIElement {
		private ConfigItem cg_hudSafezoneX("cg_hudSafezoneX");
		private ConfigItem cg_hudSafezoneY("cg_hudSafezoneY");
		spades::ui::EventHandler@ OnDone;

		ConfigHUDEdit(spades::ui::UIManager@ manager) {
			super(manager);

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;
			Bounds = AABB2(0.0F, 0.0F, sw, sh);

			float panelW = 280.0F;
			float panelH = 180.0F;
			float panelX = (sw - panelW) * 0.5F;
			float panelY = (sh - panelH) * 0.5F;

			{
				spades::ui::Label bg(Manager);
				bg.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.8F);
				bg.Bounds = AABB2(panelX - 8.0F, panelY - 8.0F, panelW + 16.0F, panelH + 16.0F);
				AddChild(bg);
			}

			{
				spades::ui::Label label(Manager);
				label.Text = _Tr("Preferences", "Horizontal Alignment");
				label.Alignment = Vector2(0.5F, 0.5F);
				label.Bounds = AABB2(panelX, panelY, panelW, 24.0F);
				AddChild(label);

				ConfigSlider sliderX(Manager, "cg_hudSafezoneX", 0.2, 1.0, 0.01, ConfigNumberFormatter(2, ""));
				sliderX.Bounds = AABB2(panelX, label.Bounds.minY + 24.0F, panelW, 24.0F);
				AddChild(sliderX);
			}

			{
				spades::ui::Label label(Manager);
				label.Text = _Tr("Preferences", "Vertical Alignment");
				label.Alignment = Vector2(0.5F, 0.5F);
				label.Bounds = AABB2(panelX, panelY + 64.0F, panelW, 24.0F);
				AddChild(label);

				ConfigSlider sliderY(Manager, "cg_hudSafezoneY", 0.2, 1.0, 0.01, ConfigNumberFormatter(2, ""));
				sliderY.Bounds = AABB2(panelX, label.Bounds.minY + 24.0F, panelW, 24.0F);
				AddChild(sliderY);
			}

			{
				spades::ui::Button doneBtn(Manager);
				doneBtn.Caption = _Tr("Preferences", "OK");
				doneBtn.Bounds = AABB2(panelX + panelW - 80.0F, panelY + panelH - 24.0F, 80.0F, 24.0F);
				@doneBtn.Activated = spades::ui::EventHandler(this.OnDoneClicked);
				AddChild(doneBtn);
			}
		}

		private void OnDoneClicked(spades::ui::UIElement@ sender) {
			if (OnDone !is null)
				OnDone(this);
		}

		void HotKey(string key) {
			if (key == "Escape")
				OnDoneClicked(this);
			else
				UIElement::HotKey(key);
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;

			float ratio = sw / sh;
			float safeZoneXMin = 0.75F;
			if (ratio < 0.26F)
				safeZoneXMin = 0.28F;
			else if (ratio < 0.56F)
				safeZoneXMin = 0.475F;

			float safeZoneX = Clamp(cg_hudSafezoneX.FloatValue, safeZoneXMin, 1.0F);
			float safeZoneY = Clamp(cg_hudSafezoneY.FloatValue, 0.85F, 1.0F);
			float x = (sw - 16.0F) * safeZoneX;
			float y = (sh - 16.0F) * safeZoneY;

			r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 0.5F);
			r.DrawFilledRect(0, 0, sw, sh - y);
			r.DrawFilledRect(0, y, sw, sh);
			r.DrawFilledRect(0, sh - y, sw - x, y);
			r.DrawFilledRect(x, sh - y, sw, y);

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.9F);
			r.DrawOutlinedRect(x, y, sw - x, sh - y);

			UIElement::Render();
		}
	}

	class ConfigField : spades::ui::Field {
		ConfigItem@ config;
		ConfigField(spades::ui::UIManager manager, string configName) {
			super(manager);
			@config = ConfigItem(configName);
			this.Text = config.StringValue;
		}

		void OnChanged() {
			Field::OnChanged();
			config = this.Text;
		}
	}

	class ConfigNumberFormatter {
		int digits;
		string suffix;
		string prefix;
		float prescale = 1;
		ConfigNumberFormatter(int digits, string suffix) {
			this.digits = digits;
			this.suffix = suffix;
			this.prefix = "";
		}
		ConfigNumberFormatter(int digits, string suffix, string prefix) {
			this.digits = digits;
			this.suffix = suffix;
			this.prefix = prefix;
		}
		ConfigNumberFormatter(int digits, string suffix, string prefix, float prescale) {
			this.digits = digits;
			this.suffix = suffix;
			this.prefix = prefix;
			this.prescale = prescale;
		}
		private string FormatInternal(float value) {
			if (value < 0.0F)
				return "-" + Format(-value);

			value *= prescale;

			// do rounding
			float rounding = 0.5F;
			for (int i = digits; i > 0; i--)
				rounding *= 0.1F;
			value += rounding;

			int intPart = int(value);
			string s = ToString(intPart);
			if (digits > 0) {
				s += ".";
				for (int i = digits; i > 0; i--) {
					value -= float(intPart);
					value *= 10.0F;
					intPart = int(value);
					if (intPart > 9)
						intPart = 9;
					s += ToString(intPart);
				}
			}
			s += suffix;
			return s;
		}
		string Format(float value) { return prefix + FormatInternal(value); }
	}

	class ConfigViewmodelSideFormatter : ConfigNumberFormatter {
		ConfigViewmodelSideFormatter() {
			super(1, "");
		}

		string Format(float value) {
			if (value == -1)
				return _Tr("Preferences", "Left");
			else if (value == 0)
				return _Tr("Preferences", "Center");
			else if (value == 1)
				return _Tr("Preferences", "Right");
			else
				return ConfigNumberFormatter::Format(value);
		}
	}

	class ConfigFOVFormatter : ConfigNumberFormatter {
		ConfigFOVFormatter() {
			super(0, "");
		}

		string Format(float value) {
			if (value == 68)
				return _Tr("Preferences", "Default");
			else if (value >= 110)
				return _Tr("Preferences", "Quake Pro");
			else
				return ConfigNumberFormatter::Format(value);
		}
	}

	class ConfigRenderScaleFormatter : ConfigNumberFormatter {
		ConfigRenderScaleFormatter() {
			super(0, "");
		}

		string Format(float value) {
			if (value == 1)
				return _Tr("Preferences", "Bicubic");
			else if (value == 2)
				return _Tr("Preferences", "Bilinear");
			else
				return _Tr("Preferences", "Nearest Neighbour");
		}
	}

	class FPSValueMapper : ConfigValueMapper {
		float Map(float value) { return (value < 20) ? 0 : value; }
	}
	class ConfigFPSFormatter : ConfigNumberFormatter {
		ConfigFPSFormatter() {
			super(0, "FPS");
		}
		string Format(float value) {
			if (value == 0)
				return _Tr("Preferences", "Unlimited");
			else
				return ConfigNumberFormatter::Format(value);
		}
	}

	class ConfigSensScaleFormatter : ConfigNumberFormatter {
		ConfigSensScaleFormatter() {
			super(0, "");
		}

		string Format(float value) {
			if (value == 1)
				return "Voxlap";
			else if (value == 2)
				return "BetterSpades";
			else if (value == 3)
				return "Quake/Source";
			else if (value == 4)
				return "Overwatch";
			else if (value == 5)
				return "Valorant";
			else
				return _Tr("Preferences", "Default");
		}
	}

	class ConfigHUDColorFormatter : ConfigNumberFormatter {
		ConfigHUDColorFormatter() {
			super(0, "");
		}

		string Format(float value) {
			if (value == 1)
				return _Tr("Preferences", "Team Color");
			else if (value == 2)
				return _Tr("Preferences", "Light Blue");
			else if (value == 3)
				return _Tr("Preferences", "Blue");
			else if (value == 4)
				return _Tr("Preferences", "Purple");
			else if (value == 5)
				return _Tr("Preferences", "Red");
			else if (value == 6)
				return _Tr("Preferences", "Orange");
			else if (value == 7)
				return _Tr("Preferences", "Yellow");
			else if (value == 8)
				return _Tr("Preferences", "Green");
			else if (value == 9)
				return _Tr("Preferences", "Aqua");
			else if (value == 10)
				return _Tr("Preferences", "Pink");
			else
				return _Tr("Preferences", "Custom");
		}
	}

	class ConfigTargetColorFormatter : ConfigNumberFormatter {
		ConfigTargetColorFormatter() {
			super(0, "");
		}

		string Format(float value) {
			if (value == 1)
				return _Tr("Preferences", "Red");
			else if (value == 2)
				return _Tr("Preferences", "Green");
			else if (value == 3)
				return _Tr("Preferences", "Blue");
			else if (value == 4)
				return _Tr("Preferences", "Yellow");
			else if (value == 5)
				return _Tr("Preferences", "Cyan");
			else if (value == 6)
				return _Tr("Preferences", "Pink");
			else
				return _Tr("Preferences", "Custom");
		}
	}

	class ConfigScopeTypeFormatter : ConfigNumberFormatter {
		ConfigScopeTypeFormatter() {
			super(0, "");
		}

		string Format(float value) {
			if (value == 1)
				return _Tr("Preferences", "Classic");
			else if (value == 2)
				return _Tr("Preferences", "Dot Sight");
			else if (value == 3)
				return _Tr("Preferences", "Custom");
			else
				return _Tr("Preferences", "Iron Sight");
		}
	}

	interface ConfigValueMapper { float Map(float value); }
	class ConfigSlider : spades::ui::Slider {
		ConfigItem@ config;
		float stepSize;
		spades::ui::Label@ label;
		ConfigNumberFormatter@ formatter;
		ConfigValueMapper@ mapper;

		ConfigSlider(spades::ui::UIManager manager, string configName, float minValue,
					 float maxValue, float stepValue, ConfigNumberFormatter@ formatter) {
			super(manager);
			@config = ConfigItem(configName);
			this.MinValue = minValue;
			this.MaxValue = maxValue;
			this.Value = Clamp(config.FloatValue, minValue, maxValue);
			this.stepSize = stepValue;
			@this.formatter = formatter;

			// compute large change
			int steps = int((maxValue - minValue) / stepValue);
			steps = (steps + 9) / 10;
			this.LargeChange = float(steps) * stepValue;

			@label = spades::ui::Label(Manager);
			label.Alignment = Vector2(0.5F, 0.5F);
			AddChild(label);
			UpdateLabel();
		}

		void OnResized() {
			Slider::OnResized();
			label.Bounds = AABB2(0.0F, 0.0F, Size.x, Size.y);
		}

		void UpdateLabel() {
			label.Text = formatter.Format(config.FloatValue);
		}

		void DoRounding() {
			float v = float(this.Value - this.MinValue);
			v = floor((v / stepSize) + 0.5) * stepSize;
			v += float(this.MinValue);
			if (mapper !is null)
				v = mapper.Map(v);
			this.Value = v;
		}

		void OnChanged() {
			Slider::OnChanged();
			DoRounding();
			config = this.Value;
			UpdateLabel();
		}
	}

	uint8 ToUpper(uint8 c) {
		if (c >= uint8(0x61) and c <= uint8(0x7a)) {
			return uint8(c - 0x61 + 0x41);
		} else {
			return c;
		}
	}
	class ConfigHotKeyField : spades::ui::UIElement {
		ConfigItem@ config;
		private bool hover;
		spades::ui::EventHandler@ KeyBound;

		ConfigHotKeyField(spades::ui::UIManager manager, string configName) {
			super(manager);
			@config = ConfigItem(configName);
			IsMouseInteractive = true;
		}

		string BoundKey {
			get { return config.StringValue; }
			set { config = value; }
		}

		void KeyDown(string key) {
			if (IsFocused) {
				if (key != "Escape") {
					if (key == " ") {
						key = "Space";
					} else if (key == "BackSpace" or key == "Delete") {
						key = ""; // unbind
					}
					config = key;
					KeyBound(this);
				}
				@Manager.ActiveElement = null;
				AcceptsFocus = false;
			} else {
				UIElement::KeyDown(key);
			}
		}

		void MouseDown(spades::ui::MouseButton button, Vector2 clientPosition) {
			if (not AcceptsFocus) {
				AcceptsFocus = true;
				@Manager.ActiveElement = this;
				return;
			}
			if (IsFocused) {
				if (button == spades::ui::MouseButton::LeftMouseButton) {
					config = "LeftMouseButton";
				} else if (button == spades::ui::MouseButton::RightMouseButton) {
					config = "RightMouseButton";
				} else if (button == spades::ui::MouseButton::MiddleMouseButton) {
					config = "MiddleMouseButton";
				} else if (button == spades::ui::MouseButton::MouseButton4) {
					config = "MouseButton4";
				} else if (button == spades::ui::MouseButton::MouseButton5) {
					config = "MouseButton5";
				}
				KeyBound(this);
				@Manager.ActiveElement = null;
				AcceptsFocus = false;
			}
		}

		void MouseEnter() { hover = true; }
		void MouseLeave() { hover = false; }

		void Render() {
			// render background
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, IsFocused ? 0.3F : 0.1F);
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

			if (IsFocused)
				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.2F);
			else if (hover)
				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.1F);
			else
				r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.06F);
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

			Font@ font = this.Font;
			string text = IsFocused
				? _Tr("Preferences", "Press Key to Bind or [Escape] to Cancel...")
				: config.StringValue;

			Vector4 color = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (IsFocused) {
				color.w = abs(sin(Manager.Time * 2.0F));
			} else {
				AcceptsFocus = false;
			}

			if (text.length == 0) {
				text = _Tr("Preferences", "Unbound");
				color.w *= 0.3F;
			} else if (text == "LeftMouseButton") {
				text = _Tr("Preferences", "Left Mouse Button");
			} else if (text == "RightMouseButton") {
				text = _Tr("Preferences", "Right Mouse Button");
			} else if (text == "MiddleMouseButton") {
				text = _Tr("Preferences", "Middle Mouse Button");
			} else if (text == "MouseButton4") {
				text = _Tr("Preferences", "Mouse Button 4");
			} else if (text == "MouseButton5") {
				text = _Tr("Preferences", "Mouse Button 5");
			} else if (!IsFocused) {
				for (uint i = 0, len = text.length; i < len; i++)
					text[i] = ToUpper(text[i]);
				text = _Tr("Client", text);
			}

			Vector2 txtSize = font.Measure(text);
			Vector2 txtPos = pos + (size - txtSize) * 0.5F;

			font.Draw(text, txtPos, 1.0F, color);
		}
	}

	class ConfigToggleButtonBase : spades::ui::RadioButton {
		ConfigToggleButtonBase(spades::ui::UIManager@ manager, string caption) {
			super(manager);
			this.Caption = caption;
			this.Toggle = true;
		}
		bool IsSelected() { return false; }
		void SetValue() {}
		void OnActivated() {
			RadioButton::OnActivated();
			SetValue();
		}
		void Render() {
			this.Toggled = IsSelected();
			RadioButton::Render();
		}
	}
	class ConfigSimpleToggleButton : ConfigToggleButtonBase {
		ConfigItem@ config;
		int value;
		ConfigSimpleToggleButton(spades::ui::UIManager@ manager,
			string caption, string configName, int value) {
			super(manager, caption);
			@config = ConfigItem(configName);
			this.value = value;
			this.Toggled = config.IntValue == value;
		}
		bool IsSelected() { return config.IntValue == value; }
		void SetValue() { config = value; }
	}
	class ConfigSimpleToggleStringButton : ConfigToggleButtonBase {
		ConfigItem@ config;
		string value;
		ConfigSimpleToggleStringButton(spades::ui::UIManager@ manager,
			string caption, string configName, string value) {
			super(manager, caption);
			@config = ConfigItem(configName);
			this.value = value;
			this.Toggled = config.StringValue == value;
		}
		bool IsSelected() { return config.StringValue == value; }
		void SetValue() { config = value; }
	}

	class ViewmodelPositionPresetButton : ConfigToggleButtonBase {
		ConfigItem@ cfgX;
		ConfigItem@ cfgY;
		ConfigItem@ cfgZ;
		float presetX, presetY, presetZ;
		ViewmodelPositionPresetButton(spades::ui::UIManager@ manager,
			string caption, string groupName, float x, float y, float z) {
			super(manager, caption);
			@cfgX = ConfigItem("cg_viewWeaponX");
			@cfgY = ConfigItem("cg_viewWeaponY");
			@cfgZ = ConfigItem("cg_viewWeaponZ");
			this.GroupName = groupName;
			this.presetX = x;
			this.presetY = y;
			this.presetZ = z;
		}
		bool IsSelected() {
			return cfgX.FloatValue == presetX
				and cfgY.FloatValue == presetY
				and cfgZ.FloatValue == presetZ;
		}
		void SetValue() {
			cfgX = presetX;
			cfgY = presetY;
			cfgZ = presetZ;
		}
	}

	class ConfigTarget : spades::ui::UIElement {
		private ConfigItem cg_target("cg_target", "1");
		private ConfigItem cg_targetLines("cg_targetLines", "1");
		private ConfigItem cg_targetColor("cg_targetColor", "0");
		private ConfigItem cg_targetColorR("cg_targetColorR", "255");
		private ConfigItem cg_targetColorG("cg_targetColorG", "255");
		private ConfigItem cg_targetColorB("cg_targetColorB", "255");
		private ConfigItem cg_targetAlpha("cg_targetAlpha", "255");
		private ConfigItem cg_targetGap("cg_targetGap", "4");
		private ConfigItem cg_targetSizeHorizontal("cg_targetSizeHorizontal", "5");
		private ConfigItem cg_targetSizeVertical("cg_targetSizeVertical", "5");
		private ConfigItem cg_targetThickness("cg_targetThickness", "1");
		private ConfigItem cg_targetTStyle("cg_targetTStyle", "0");
		private ConfigItem cg_targetDot("cg_targetDot", "0");
		private ConfigItem cg_targetDotColorR("cg_targetDotColorR", "255");
		private ConfigItem cg_targetDotColorG("cg_targetDotColorG", "255");
		private ConfigItem cg_targetDotColorB("cg_targetDotColorB", "255");
		private ConfigItem cg_targetDotAlpha("cg_targetDotAlpha", "255");
		private ConfigItem cg_targetDotThickness("cg_targetDotThickness", "1");
		private ConfigItem cg_targetOutline("cg_targetOutline", "1");
		private ConfigItem cg_targetOutlineColorR("cg_targetOutlineColorR", "0");
		private ConfigItem cg_targetOutlineColorG("cg_targetOutlineColorG", "0");
		private ConfigItem cg_targetOutlineColorB("cg_targetOutlineColorB", "0");
		private ConfigItem cg_targetOutlineAlpha("cg_targetOutlineAlpha", "255");
		private ConfigItem cg_targetOutlineThickness("cg_targetOutlineThickness", "1");
		private ConfigItem cg_targetOutlineRoundedStyle("cg_targetOutlineRoundedStyle", "0");
		private ConfigItem cg_targetDynamic("cg_targetDynamic", "1");
		private ConfigItem cg_targetDynamicSplitDist("cg_targetDynamicSplitdist", "7");

		ConfigTarget(spades::ui::UIManager@ manager) {
			super(manager);
		}

		private void OnResetPressed(spades::ui::UIElement@ sender) {
			cg_targetLines.StringValue = cg_targetLines.DefaultValue;
			cg_targetColor.StringValue = cg_targetColor.DefaultValue;
			cg_targetColorR.StringValue = cg_targetColorR.DefaultValue;
			cg_targetColorG.StringValue = cg_targetColorG.DefaultValue;
			cg_targetColorB.StringValue = cg_targetColorB.DefaultValue;
			cg_targetAlpha.StringValue = cg_targetAlpha.DefaultValue;
			cg_targetGap.StringValue = cg_targetGap.DefaultValue;
			cg_targetSizeHorizontal.StringValue = cg_targetSizeHorizontal.DefaultValue;
			cg_targetSizeVertical.StringValue = cg_targetSizeVertical.DefaultValue;
			cg_targetThickness.StringValue = cg_targetThickness.DefaultValue;
			cg_targetTStyle.StringValue = cg_targetTStyle.DefaultValue;
			cg_targetDot.StringValue = cg_targetDot.DefaultValue;
			cg_targetDotColorR.StringValue = cg_targetDotColorR.DefaultValue;
			cg_targetDotColorG.StringValue = cg_targetDotColorG.DefaultValue;
			cg_targetDotColorB.StringValue = cg_targetDotColorB.DefaultValue;
			cg_targetDotAlpha.StringValue = cg_targetDotAlpha.DefaultValue;
			cg_targetDotThickness.StringValue = cg_targetDotThickness.DefaultValue;
			cg_targetOutline.StringValue = cg_targetOutline.DefaultValue;
			cg_targetOutlineColorR.StringValue = cg_targetOutlineColorR.DefaultValue;
			cg_targetOutlineColorG.StringValue = cg_targetOutlineColorG.DefaultValue;
			cg_targetOutlineColorB.StringValue = cg_targetOutlineColorB.DefaultValue;
			cg_targetOutlineAlpha.StringValue = cg_targetOutlineAlpha.DefaultValue;
			cg_targetOutlineThickness.StringValue = cg_targetOutlineThickness.DefaultValue;
			cg_targetOutlineRoundedStyle.StringValue = cg_targetOutlineRoundedStyle.DefaultValue;
			cg_targetDynamic.StringValue = cg_targetDynamic.DefaultValue;
			cg_targetDynamicSplitDist.StringValue = cg_targetDynamicSplitDist.DefaultValue;
		}

		private void OnRandomizePressed(spades::ui::UIElement@ sender) {
			cg_targetColor.StringValue = cg_targetColor.DefaultValue; // reset
			cg_targetColorR.IntValue = GetRandom(0, 255);
			cg_targetColorG.IntValue = GetRandom(0, 255);
			cg_targetColorB.IntValue = GetRandom(0, 255);
			cg_targetAlpha.IntValue = GetRandom(50, 255);
			cg_targetGap.IntValue = GetRandom(1, 10);
			cg_targetSizeHorizontal.IntValue = GetRandom(1, 10);
			cg_targetSizeVertical.IntValue = cg_targetSizeHorizontal.IntValue;
			cg_targetThickness.IntValue = GetRandom(1, 4);
			cg_targetDot.IntValue = GetRandom(2);
			cg_targetDotColorR.IntValue = GetRandom(0, 255);
			cg_targetDotColorG.IntValue = GetRandom(0, 255);
			cg_targetDotColorB.IntValue = GetRandom(0, 255);
			cg_targetDotAlpha.IntValue = GetRandom(50, 255);
			cg_targetDotThickness.IntValue = cg_targetThickness.IntValue;
			cg_targetOutline.IntValue = GetRandom(2);
			cg_targetOutlineColorR.IntValue = GetRandom(0, 255);
			cg_targetOutlineColorG.IntValue = GetRandom(0, 255);
			cg_targetOutlineColorB.IntValue = GetRandom(0, 255);
			cg_targetOutlineAlpha.IntValue = GetRandom(50, 255);
			cg_targetOutlineRoundedStyle.IntValue = GetRandom(2);
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;
			Vector2 center = pos + size * 0.5F;

			TargetParam param;
			int targetType = cg_target.IntValue;

			IntVector3 col;
			switch (cg_targetColor.IntValue) {
				case 1: col = IntVector3(250, 50, 50); break; // red
				case 2: col = IntVector3(50, 250, 50); break; // green
				case 3: col = IntVector3(50, 50, 250); break; // blue
				case 4: col = IntVector3(250, 250, 50); break; // yellow
				case 5: col = IntVector3(50, 250, 250); break; // cyan
				case 6: col = IntVector3(250, 50, 250); break; // pink
				default: // custom
					col.x = cg_targetColorR.IntValue;
					col.y = cg_targetColorG.IntValue;
					col.z = cg_targetColorB.IntValue;
					break;
			}

			Vector4 color = ConvertColorRGBA(col);
			color.w = Clamp(cg_targetAlpha.IntValue, 0, 255) / 255.0F;

			// draw preview background
			if (cg_target.BoolValue) {
				float luminosity = color.x + color.y + color.z;
				float opacity = 1.0F - luminosity;
				if (cg_targetOutline.BoolValue and targetType == 2)
					r.ColorNP = Vector4(0.6F, 0.6F, 0.6F, 0.9F);
				else
					r.ColorNP = Vector4(opacity, opacity, opacity, 0.6F);
			} else {
				r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 0.6F);
			}
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

			// draw preview border
			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.1F);
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

			// draw target
			if (targetType == 1) { // draw default target
				Image@ sightImage = r.RegisterImage("Gfx/Sight.tga");
				Vector2 imgSize = Vector2(sightImage.Width, sightImage.Height);
				r.ColorNP = color;
				r.DrawImage(sightImage, center - (imgSize * 0.5F));
			} else if (targetType == 2) { // draw custom target
				param.lineColor = color;
				param.drawLines = cg_targetLines.BoolValue;
				param.useTStyle = cg_targetTStyle.BoolValue;
				param.lineGap = Clamp(cg_targetGap.FloatValue, -10.0F, 10.0F);
				param.lineLength.x = Clamp(cg_targetSizeHorizontal.FloatValue, 0.0F, 10.0F);
				param.lineLength.y = Clamp(cg_targetSizeVertical.FloatValue, 0.0F, 10.0F);
				param.lineThickness = Clamp(cg_targetThickness.FloatValue, 1.0F, 4.0F);

				param.drawDot = cg_targetDot.BoolValue;
				col.x = cg_targetDotColorR.IntValue;
				col.y = cg_targetDotColorG.IntValue;
				col.z = cg_targetDotColorB.IntValue;
				color = ConvertColorRGBA(col);
				color.w = Clamp(cg_targetDotAlpha.IntValue, 0, 255) / 255.0F;
				param.dotColor = color;
				param.dotThickness = Clamp(cg_targetDotThickness.FloatValue, 1.0F, 4.0F);

				param.drawOutline = cg_targetOutline.BoolValue;
				param.useRoundedStyle = cg_targetOutlineRoundedStyle.BoolValue;
				col.x = cg_targetOutlineColorR.IntValue;
				col.y = cg_targetOutlineColorG.IntValue;
				col.z = cg_targetOutlineColorB.IntValue;
				color = ConvertColorRGBA(col);
				color.w = Clamp(cg_targetOutlineAlpha.IntValue, 0, 255) / 255.0F;
				param.outlineColor = color;
				param.outlineThickness = Clamp(cg_targetOutlineThickness.FloatValue, 1.0F, 4.0F);

				DrawTarget(r, center, param);
			} else {
				Font@ font = this.Font;
				string text = _Tr("Preferences", "No Preview Available.");
				Vector2 txtPos = pos + (size - font.Measure(text)) * 0.5F;
				font.Draw(text, txtPos, 1.0F, Vector4(1.0F, 1.0F, 1.0F, 0.5F));
			}
		}
	}

	class ConfigScope : spades::ui::UIElement {
		private ConfigItem cg_pngScope("cg_pngScope", "0");
		private ConfigItem cg_scopeLines("cg_scopeLines", "1");
		private ConfigItem cg_scopeColor("cg_scopeColor", "0");
		private ConfigItem cg_scopeColorR("cg_scopeColorR", "255");
		private ConfigItem cg_scopeColorG("cg_scopeColorG", "0");
		private ConfigItem cg_scopeColorB("cg_scopeColorB", "255");
		private ConfigItem cg_scopeAlpha("cg_scopeAlpha", "255");
		private ConfigItem cg_scopeGap("cg_scopeGap", "4");
		private ConfigItem cg_scopeSizeHorizontal("cg_scopeSizeHorizontal", "5");
		private ConfigItem cg_scopeSizeVertical("cg_scopeSizeVertical", "5");
		private ConfigItem cg_scopeThickness("cg_scopeThickness", "1");
		private ConfigItem cg_scopeTStyle("cg_scopeTStyle", "0");
		private ConfigItem cg_scopeDot("cg_scopeDot", "0");
		private ConfigItem cg_scopeDotColorR("cg_scopeDotColorR", "0");
		private ConfigItem cg_scopeDotColorG("cg_scopeDotColorG", "0");
		private ConfigItem cg_scopeDotColorB("cg_scopeDotColorB", "0");
		private ConfigItem cg_scopeDotAlpha("cg_scopeDotAlpha", "255");
		private ConfigItem cg_scopeDotThickness("cg_scopeDotThickness", "1");
		private ConfigItem cg_scopeOutline("cg_scopeOutline", "1");
		private ConfigItem cg_scopeOutlineColorR("cg_scopeOutlineColorR", "0");
		private ConfigItem cg_scopeOutlineColorG("cg_scopeOutlineColorG", "0");
		private ConfigItem cg_scopeOutlineColorB("cg_scopeOutlineColorB", "0");
		private ConfigItem cg_scopeOutlineAlpha("cg_scopeOutlineAlpha", "255");
		private ConfigItem cg_scopeOutlineThickness("cg_scopeOutlineThickness", "1");
		private ConfigItem cg_scopeOutlineRoundedStyle("cg_scopeOutlineRoundedStyle", "0");
		private ConfigItem cg_scopeDynamic("cg_scopeDynamic", "1");
		private ConfigItem cg_scopeDynamicSplitDist("cg_scopeDynamicSplitdist", "7");

		ConfigScope(spades::ui::UIManager@ manager) {
			super(manager);
		}

		private void OnResetPressed(spades::ui::UIElement@ sender) {
			cg_scopeLines.StringValue = cg_scopeLines.DefaultValue;
			cg_scopeColor.StringValue = cg_scopeColor.DefaultValue;
			cg_scopeColorR.StringValue = cg_scopeColorR.DefaultValue;
			cg_scopeColorG.StringValue = cg_scopeColorG.DefaultValue;
			cg_scopeColorB.StringValue = cg_scopeColorB.DefaultValue;
			cg_scopeAlpha.StringValue = cg_scopeAlpha.DefaultValue;
			cg_scopeGap.StringValue = cg_scopeGap.DefaultValue;
			cg_scopeSizeHorizontal.StringValue = cg_scopeSizeHorizontal.DefaultValue;
			cg_scopeSizeVertical.StringValue = cg_scopeSizeVertical.DefaultValue;
			cg_scopeThickness.StringValue = cg_scopeThickness.DefaultValue;
			cg_scopeTStyle.StringValue = cg_scopeTStyle.DefaultValue;
			cg_scopeDot.StringValue = cg_scopeDot.DefaultValue;
			cg_scopeDotColorR.StringValue = cg_scopeDotColorR.DefaultValue;
			cg_scopeDotColorG.StringValue = cg_scopeDotColorG.DefaultValue;
			cg_scopeDotColorB.StringValue = cg_scopeDotColorB.DefaultValue;
			cg_scopeDotAlpha.StringValue = cg_scopeDotAlpha.DefaultValue;
			cg_scopeDotThickness.StringValue = cg_scopeDotThickness.DefaultValue;
			cg_scopeOutline.StringValue = cg_scopeOutline.DefaultValue;
			cg_scopeOutlineColorR.StringValue = cg_scopeOutlineColorR.DefaultValue;
			cg_scopeOutlineColorG.StringValue = cg_scopeOutlineColorG.DefaultValue;
			cg_scopeOutlineColorB.StringValue = cg_scopeOutlineColorB.DefaultValue;
			cg_scopeOutlineAlpha.StringValue = cg_scopeOutlineAlpha.DefaultValue;
			cg_scopeOutlineThickness.StringValue = cg_scopeOutlineThickness.DefaultValue;
			cg_scopeOutlineRoundedStyle.StringValue = cg_scopeOutlineRoundedStyle.DefaultValue;
			cg_scopeDynamic.StringValue = cg_scopeDynamic.DefaultValue;
			cg_scopeDynamicSplitDist.StringValue = cg_scopeDynamicSplitDist.DefaultValue;
		}

		private void OnRandomizePressed(spades::ui::UIElement@ sender) {
			cg_scopeColor.StringValue = cg_scopeColor.DefaultValue;
			cg_scopeColorR.IntValue = GetRandom(0, 255);
			cg_scopeColorG.IntValue = GetRandom(0, 255);
			cg_scopeColorB.IntValue = GetRandom(0, 255);
			cg_scopeAlpha.IntValue = GetRandom(50, 255);
			cg_scopeGap.IntValue = GetRandom(1, 10);
			cg_scopeSizeHorizontal.IntValue = GetRandom(1, 10);
			cg_scopeSizeVertical.IntValue = cg_scopeSizeHorizontal.IntValue;
			cg_scopeThickness.IntValue = GetRandom(1, 4);
			cg_scopeDot.IntValue = GetRandom(2);
			cg_scopeDotColorR.IntValue = GetRandom(0, 255);
			cg_scopeDotColorG.IntValue = GetRandom(0, 255);
			cg_scopeDotColorB.IntValue = GetRandom(0, 255);
			cg_scopeDotAlpha.IntValue = GetRandom(50, 255);
			cg_scopeDotThickness.IntValue = cg_scopeThickness.IntValue;
			cg_scopeOutline.IntValue = GetRandom(2);
			cg_scopeOutlineColorR.IntValue = GetRandom(0, 255);
			cg_scopeOutlineColorG.IntValue = GetRandom(0, 255);
			cg_scopeOutlineColorB.IntValue = GetRandom(0, 255);
			cg_scopeOutlineAlpha.IntValue = GetRandom(50, 255);
			cg_scopeOutlineRoundedStyle.IntValue = GetRandom(2);
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;
			Vector2 center = pos + size * 0.5F;

			TargetParam param;
			int scopeType = cg_pngScope.IntValue;

			IntVector3 col;
			col.x = cg_scopeColorR.IntValue;
			col.y = cg_scopeColorG.IntValue;
			col.z = cg_scopeColorB.IntValue;

			Vector4 color = ConvertColorRGBA(col);
			color.w = Clamp(cg_scopeAlpha.IntValue, 0, 255) / 255.0F;

			// draw preview background
			if (scopeType >= 2) {
				float luminosity = color.x + color.y + color.z;
				float opacity = 1.0F - luminosity;
				if (cg_scopeOutline.BoolValue and scopeType == 3)
					r.ColorNP = Vector4(0.6F, 0.6F, 0.6F, 0.9F);
				else
					r.ColorNP = Vector4(opacity, opacity, opacity, 0.6F);
			} else if (scopeType < 2) {
				r.ColorNP = Vector4(0.0F, 0.0F, 0.0F, 0.6F);
			}
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

			// draw preview border
			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.1F);
			r.DrawOutlinedRect(pos.x, pos.y, pos.x + size.x, pos.y + size.y);

			// draw target
			if (scopeType == 2) { // draw dot png scope
				Image@ dotSightImage = r.RegisterImage("Gfx/DotSight.tga");
				Vector2 imgSize = Vector2(dotSightImage.Width, dotSightImage.Height);
				r.ColorNP = color;
				r.DrawImage(dotSightImage, center - (imgSize * 0.5F));
			} else if (scopeType == 3) { // draw custom target scope
				param.lineColor = color;
				param.drawLines = cg_scopeLines.BoolValue;
				param.useTStyle = cg_scopeTStyle.BoolValue;
				param.lineGap = Clamp(cg_scopeGap.FloatValue, -10.0F, 10.0F);
				param.lineLength.x = Clamp(cg_scopeSizeHorizontal.FloatValue, 0.0F, 10.0F);
				param.lineLength.y = Clamp(cg_scopeSizeVertical.FloatValue, 0.0F, 10.0F);
				param.lineThickness = Clamp(cg_scopeThickness.FloatValue, 1.0F, 4.0F);

				param.drawDot = cg_scopeDot.BoolValue;
				col.x = cg_scopeDotColorR.IntValue;
				col.y = cg_scopeDotColorG.IntValue;
				col.z = cg_scopeDotColorB.IntValue;
				color = ConvertColorRGBA(col);
				color.w = Clamp(cg_scopeDotAlpha.IntValue, 0, 255) / 255.0F;
				param.dotColor = color;
				param.dotThickness = Clamp(cg_scopeDotThickness.FloatValue, 1.0F, 4.0F);

				param.drawOutline = cg_scopeOutline.BoolValue;
				param.useRoundedStyle = cg_scopeOutlineRoundedStyle.BoolValue;
				col.x = cg_scopeOutlineColorR.IntValue;
				col.y = cg_scopeOutlineColorG.IntValue;
				col.z = cg_scopeOutlineColorB.IntValue;
				color = ConvertColorRGBA(col);
				color.w = Clamp(cg_scopeOutlineAlpha.IntValue, 0, 255) / 255.0F;
				param.outlineColor = color;
				param.outlineThickness = Clamp(cg_scopeOutlineThickness.FloatValue, 1.0F, 4.0F);

				DrawTarget(r, center, param);
			} else {
				Font@ font = this.Font;
				string text = _Tr("Preferences", "No Preview Available.");
				Vector2 txtPos = pos + (size - font.Measure(text)) * 0.5F;
				font.Draw(text, txtPos, 1.0F, Vector4(1.0F, 1.0F, 1.0F, 0.5F));
			}
		}
	}

	class StandardPreferenceLayouterModel : spades::ui::ListViewModel {
		private spades::ui::UIElement @[] @items;
		StandardPreferenceLayouterModel(spades::ui::UIElement @[] @items) { @this.items = items; }
		int NumRows { get { return int(items.length); } }
		spades::ui::UIElement@ CreateElement(int row) { return items[row]; }
		void RecycleElement(spades::ui::UIElement@ elem) {}
	}
	class StandardPreferenceLayouter {
		spades::ui::UIElement@ Parent;
		private spades::ui::UIElement @[] items;
		private ConfigHotKeyField @[] hotkeyItems;
		private FontManager@ fontManager;

		// Heading navigation tracking
		private HeadingNavEntry[] headings;
		HeadingNavIndex@ HeadingNav;

		float FieldX, FieldWidth, FieldHeight;
		float ListX, ListWidth;

		StandardPreferenceLayouter(spades::ui::UIElement@ parent, FontManager@ fontManager) {
			@Parent = parent;
			@this.fontManager = fontManager;

			float sw = Parent.Manager.ScreenWidth;
			float sh = Parent.Manager.ScreenHeight;

			FieldX = sw - 440.0F;
			float maxFieldX = 250.0F;
			if (FieldX > maxFieldX)
				FieldX = maxFieldX;

			FieldWidth = sw - 400.0F;
			float maxFieldWidth = 320.0F;
			if (FieldWidth > maxFieldWidth)
				FieldWidth = maxFieldWidth;

			FieldHeight = sh - 8.0F;
			float maxFieldHeight = 550.0F;
			if (FieldHeight > maxFieldHeight)
				FieldHeight = maxFieldHeight;

			ListX = 10.0F;
			ListWidth = FieldX + FieldWidth - ListX;
		}

		private void OnKeyBound(spades::ui::UIElement@ sender) {
			// unbind already bound key
			ConfigHotKeyField@ bindField = cast<ConfigHotKeyField>(sender);
			string key = bindField.BoundKey;
			for (uint i = 0; i < hotkeyItems.length; i++) {
				ConfigHotKeyField@ f = hotkeyItems[i];
				if (f !is bindField) {
					if (f.BoundKey == key)
						f.BoundKey = "";
				}
			}
		}

		private spades::ui::UIElement@ CreateItem() {
			spades::ui::UIElement elem(Parent.Manager);
			elem.Size = Vector2(FieldWidth, 32.0F);
			items.insertLast(elem);
			return elem;
		}

		private spades::ui::UIElement@ CreateBasicLabel(string caption, bool enabled = true) {
			spades::ui::UIElement@ container = CreateItem();

			spades::ui::Label label(Parent.Manager);
			label.Text = caption;
			label.Alignment = Vector2(0.0F, 0.5F);
			label.Bounds = AABB2(ListX, 0.0F, ListWidth, 32.0F);
			label.Enable = enabled;
			container.AddChild(label);

			return container;
		}

		void AddHeading(string text) {
			// record this heading for the navigation bar
			if (text.length > 0) {
				HeadingNavEntry entry;
				entry.caption = text;
				entry.row = int(items.length);
				headings.insertLast(entry);
			}

			spades::ui::UIElement@ container = CreateItem();

			spades::ui::Label label(Parent.Manager);
			@label.Font = fontManager.HeadingFont;
			label.Text = text;
			label.Alignment = Vector2(0.5F, 0.5F);
			label.Bounds = AABB2(ListX, 0.0F, ListWidth, 32.0F);
			container.AddChild(label);
		}

		ConfigField@ AddInputField(string caption, string configName, bool enabled = true, bool compact = false) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			ConfigField field(Parent.Manager, configName);
			if (compact)
				@field.Font = fontManager.SmallFont;
			field.Bounds = AABB2(FieldX, 2.0F, FieldWidth, compact ? 20.0F : 28.0F);
			field.Enable = enabled;
			container.AddChild(field);

			return field;
		}

		ConfigSlider@ AddSliderField(string caption, string configName, float minRange, float maxRange,
			float step, ConfigNumberFormatter@ formatter, bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			ConfigSlider slider(Parent.Manager, configName, minRange, maxRange, step, formatter);
			slider.Bounds = AABB2(FieldX, 4.0F, FieldWidth, 24.0F);
			slider.Enable = enabled;
			container.AddChild(slider);

			return slider;
		}

		void AddSliderGroup(string caption, array<string> labels,
			float minRange, float maxRange, float step, int digits,
			array<string> prefix, bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			float width = (FieldWidth - (labels.length - 1)) / labels.length;
			for (uint i = 0; i < labels.length; ++i) {
				ConfigSlider slider(Parent.Manager, labels[i], minRange, maxRange, step,
					ConfigNumberFormatter(digits, "", prefix[i]));
				slider.Bounds = AABB2(FieldX + float(i) * (width + 1.0F), 4.0F, width, 24.0F);
				slider.Enable = enabled;
				container.AddChild(slider);
			}
		}

		void AddVolumeSlider(string caption, string configName) {
			AddSliderField(caption, configName, 0, 1, 0.01, ConfigNumberFormatter(0, "%", "", 100));
		}

		void AddRGBSlider(string caption, array<string> labels, bool enabled = true) {
			AddSliderGroup(caption, labels, 0, 255, 1, 0, array<string> = { "R: ", "G: ", "B: "});
		}

		void AddControl(string caption, string configName, bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			ConfigHotKeyField field(Parent.Manager, configName);
			field.Bounds = AABB2(FieldX, 2.0F, FieldWidth, 28.0F);
			field.Enable = enabled;
			container.AddChild(field);

			@field.KeyBound = spades::ui::EventHandler(this.OnKeyBound);
			hotkeyItems.insertLast(field);
		}

		void AddChoiceField(string caption, string configName,
			array<string>@ labels, array<int>@ values, bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			float width = (FieldWidth - (labels.length - 1)) / labels.length;
			for (uint i = 0; i < labels.length; ++i) {
				ConfigSimpleToggleButton field(Parent.Manager, labels[i], configName, values[i]);
				field.Bounds = AABB2(FieldX + float(i) * (width + 1.0F), 2.0F, width, 28.0F);
				field.Enable = enabled;
				container.AddChild(field);
			}
		}

		void AddChoiceStringField(string caption, string configName,
			array<string>@ labels, array<string>@ values, bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			float width = (FieldWidth - (labels.length - 1)) / labels.length;
			for (uint i = 0; i < labels.length; ++i) {
				ConfigSimpleToggleStringButton field(Parent.Manager, labels[i], configName, values[i]);
				field.Bounds = AABB2(FieldX + float(i) * (width + 1.0F), 2.0F, width, 28.0F);
				field.Enable = enabled;
				container.AddChild(field);
			}
		}

		void AddViewmodelPresetField(string caption,
			array<string>@ labels,
			array<float>@ xs, array<float>@ ys, array<float>@ zs,
			bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption, enabled);

			float width = (FieldWidth - (labels.length - 1)) / labels.length;
			for (uint i = 0; i < labels.length; ++i) {
				ViewmodelPositionPresetButton button(Parent.Manager, labels[i], caption, xs[i], ys[i], zs[i]);
				button.Bounds = AABB2(FieldX + float(i) * (width + 1.0F), 2.0F, width, 28.0F);
				button.Enable = enabled;
				container.AddChild(button);
			}
		}

		void AddToggleField(string caption, string configName, bool enabled = true) {
			AddChoiceField(caption, configName,
						   array<string> = {_Tr("Preferences", "ON"),
											_Tr("Preferences", "OFF")},
						   array<int> = {1, 0}, enabled);
		}

		void AddPlusMinusField(string caption, string configName, bool enabled = true) {
			AddChoiceField(caption, configName,
						   array<string> = {_Tr("Preferences", "ON"),
											_Tr("Preferences", "REVERSED"),
											_Tr("Preferences", "OFF")},
						   array<int> = {1, -1, 0}, enabled);
		}

		void AddButtonField(string caption, string btnCaption, spades::ui::EventHandler@ handler, bool enabled = true) {
			spades::ui::UIElement@ container = CreateBasicLabel(caption);

			spades::ui::SimpleButton button(Parent.Manager);
			button.Caption = btnCaption;
			button.Bounds = AABB2(FieldX, 4.0F, FieldWidth, 24.0F);
			button.Enable = enabled;
			@button.Activated = handler;
			container.AddChild(button);
		}

		private void CreatePreview(spades::ui::UIElement@ field,
			spades::ui::EventHandler@ resetHandler,
			spades::ui::EventHandler@ randomizeHandler) {
			spades::ui::UIElement@ container = CreateItem();

			field.Bounds = AABB2(ListX, 0.0F, ListWidth, 64.0F);
			container.AddChild(field);

			spades::ui::SimpleButton resetButton(Parent.Manager);
			resetButton.Caption = _Tr("Preferences", "Reset");
			resetButton.Bounds = AABB2(10.0F + 2.0F, 2.0F, 50.0F, 20.0F);
			@resetButton.Activated = resetHandler;
			container.AddChild(resetButton);

			spades::ui::SimpleButton randomizeButton(Parent.Manager);
			randomizeButton.Caption = _Tr("Preferences", "Randomize");
			randomizeButton.Bounds = AABB2(FieldX + FieldWidth - 80.0F - 2.0F, 2.0F, 80.0F, 20.0F);
			@randomizeButton.Activated = randomizeHandler;
			container.AddChild(randomizeButton);
		}

		void AddTargetPreview() {
			ConfigTarget field(Parent.Manager);
			CreatePreview(field,
				spades::ui::EventHandler(field.OnResetPressed),
				spades::ui::EventHandler(field.OnRandomizePressed));
		}
		void AddScopePreview() {
			ConfigScope field(Parent.Manager);
			CreatePreview(field,
				spades::ui::EventHandler(field.OnResetPressed),
				spades::ui::EventHandler(field.OnRandomizePressed));
		}

		void FinishLayout() {
			spades::ui::ListView list(Parent.Manager);
			@list.Model = StandardPreferenceLayouterModel(items);
			list.RowHeight = 32.0F;
			list.Bounds = AABB2(ListX, 0.0F, ListWidth + 30.0F, FieldHeight - 6.0F);
			Parent.AddChild(list);

			// pass heading data to PreferenceView if it set a receiver
			if (HeadingNav !is null and headings.length >= 2) {
				HeadingNav.entries = headings;
				@HeadingNav.list = list;
			}
		}
	}

	class GameOptionsPanel : spades::ui::UIElement {
		GameOptionsPanel(spades::ui::UIManager@ manager,
			PreferenceViewOptions@ options, FontManager@ fontManager,
			HeadingNavIndex@ nav = null) {
			super(manager);

			StandardPreferenceLayouter layouter(this, fontManager);
			@layouter.HeadingNav = nav;

			layouter.AddHeading(_Tr("Preferences", "Player Information"));
			ConfigField@ nameField = layouter.AddInputField(_Tr("Preferences", "Player Name"),
				"cg_playerName", not options.GameActive);
			nameField.MaxLength = 15;
			nameField.DenyNonAscii = false;
			nameField.RemoveNewlines = true;

			layouter.AddHeading(_Tr("Preferences", "Gameplay"));
			layouter.AddChoiceField(_Tr("Preferences", "Aiming Animation"), "cg_animations",
									array<string> = {_Tr("Preferences", "FAST"),
													 _Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "OFF")},
									array<int> = {2, 1, 0});
			layouter.AddToggleField(_Tr("Preferences", "Full Aim Down Sight"), "cg_trueAimDownSight");
			layouter.AddToggleField(_Tr("Preferences", "Classic Weapon Recoil"), "cg_classicWeaponRecoil");
			layouter.AddToggleField(_Tr("Preferences", "Classic Sprinting"), "cg_classicSprinting");
			layouter.AddToggleField(_Tr("Preferences", "Classic Zoom"), "cg_classicZoom");

			layouter.AddHeading(_Tr("Preferences", "Effects"));
			layouter.AddChoiceField(_Tr("Preferences", "Blood"), "cg_blood",
									array<string> = {_Tr("Preferences", "MARKS"),
													 _Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "OFF")},
									array<int> = {2, 1, 0});
			layouter.AddToggleField(_Tr("Preferences", "Ragdoll Corpses"), "cg_ragdoll");
			layouter.AddToggleField(_Tr("Preferences", "Enhanced Ragdoll Physics"), "cg_corpseLineCollision");
			layouter.AddChoiceField(_Tr("Preferences", "Bullet Tracers"), "cg_tracers",
									array<string> = {_Tr("Preferences", "ON"),
													 _Tr("Preferences", "3rd Person"),
													 _Tr("Preferences", "OFF")},
									array<int> = {1, 2, 0});
			layouter.AddChoiceField(_Tr("Preferences", "Muzzle Flashes"), "cg_muzzleFire",
									array<string> = {_Tr("Preferences", "ON"),
													 _Tr("Preferences", "3rd Person"),
													 _Tr("Preferences", "OFF")},
									array<int> = {1, 2, 0});
			layouter.AddToggleField(_Tr("Preferences", "Eject Bullet Casings"), "cg_ejectBrass");
			layouter.AddToggleField(_Tr("Preferences", "Hurt Screen Effects"), "cg_hurtScreenEffects");
			layouter.AddToggleField(_Tr("Preferences", "Heal Screen Effects"), "cg_healScreenEffects");
			layouter.AddChoiceField(_Tr("Preferences", "Camera Shake"), "cg_shake",
									array<string> = {_Tr("Preferences", "MORE"),
													 _Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "OFF")},
									array<int> = {2, 1, 0});
			layouter.AddChoiceField(_Tr("Preferences", "Particles"), "cg_particles",
									array<string> = {_Tr("Preferences", "MORE"),
													 _Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "OFF")},
									array<int> = {2, 1, 0});
			layouter.AddToggleField(_Tr("Preferences", "Water Splash"), "cg_waterImpact");

			layouter.AddHeading(_Tr("Preferences", "Feedbacks"));
			layouter.AddChoiceField(_Tr("Preferences", "Center Messages"), "cg_centerMessage",
									array<string> = {_Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "LESS")},
									array<int> = {2, 0});
			layouter.AddChoiceField(_Tr("Preferences", "Center Messages Size"), "cg_centerMessageSmallFont",
									array<string> = {_Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "SMALL")},
									array<int> = {0, 1}, not options.GameActive);
			layouter.AddToggleField(_Tr("Preferences", "Ignore Chat Messages"), "cg_ignoreChatMessages");
			layouter.AddToggleField(_Tr("Preferences", "Ignore Private Messages"), "cg_ignorePrivateMessages");
			layouter.AddSliderField(_Tr("Preferences",	"Master Volume"), "s_volume",
			0, 100, 1, ConfigNumberFormatter(0, "%"));
			layouter.AddVolumeSlider(_Tr("Preferences", "Death Camera Volume"), "cg_deathSoundGain");
			layouter.AddVolumeSlider(_Tr("Preferences", "Respawn Beep Volume"), "cg_respawnSoundGain");
			layouter.AddVolumeSlider(_Tr("Preferences", "Hit Feedback Volume"), "cg_hitFeedbackSoundGain");
			layouter.AddVolumeSlider(_Tr("Preferences", "Headshot Feedback Volume"), "cg_headshotFeedbackSoundGain");
			layouter.AddVolumeSlider(_Tr("Preferences", "Chat Notify Sounds"), "cg_chatBeep");
			layouter.AddToggleField(_Tr("Preferences", "Show Alerts"), "cg_alerts");
			layouter.AddToggleField(_Tr("Preferences", "Show Server Alerts"), "cg_serverAlert");
			layouter.AddVolumeSlider(_Tr("Preferences", "Alert Sounds"), "cg_alertSounds");
			layouter.AddToggleField(_Tr("Preferences", "Hit Log"), "cg_hitLog");
			layouter.AddToggleField(_Tr("Preferences", "Hit Indicator"), "cg_hitIndicator");

			layouter.AddHeading(_Tr("Preferences", "Viewmodel"));
			layouter.AddToggleField(_Tr("Preferences", "Weapon Keychains"), "cg_weaponCharms");
			layouter.AddViewmodelPresetField(
				_Tr("Preferences", "Viewmodel Position"),
				array<string> = {
					_Tr("Preferences", "Default"),
					_Tr("Preferences", "Balanced"),
					_Tr("Preferences", "Minimal")
				},
				array<float> = {  0.0F, -0.1F, -0.25F },
				array<float> = {  0.0F,	 0.05F, 0.25F },
				array<float> = {  0.0F,	 0.12F, 0.3F }
			);
			layouter.AddSliderField(_Tr("Preferences", "Viewmodel Alignment"), "cg_viewWeaponSide",
			-1, 1, 0.1, ConfigViewmodelSideFormatter());
			layouter.AddControl(_Tr("Preferences", "Switch Viewmodel Handedness"), "cg_keyToggleLeftHand");
			layouter.AddToggleField(_Tr("Preferences", "Hide Player Arms"), "cg_hideArms");
			layouter.AddToggleField(_Tr("Preferences", "Hide Player Body"), "cg_hideBody");
			layouter.AddToggleField(_Tr("Preferences", "Classic Player Model"), "cg_classicPlayerModels");
			layouter.AddToggleField(_Tr("Preferences", "Classic Viewmodel"), "cg_classicViewWeapon");

			layouter.AddHeading(_Tr("Preferences", "Misc"));
			layouter.AddSliderField(_Tr("Preferences", "Field of View"), "cg_fov",
			45, 110, 1, ConfigFOVFormatter());
			layouter.AddToggleField(_Tr("Preferences", "Horizontal FOV"), "cg_horizontalFov");
			layouter.AddToggleField(_Tr("Preferences", "Environmental Audio"), "cg_environmentalAudio");
			layouter.AddToggleField(_Tr("Preferences", "Skip dead players (death cam)"), "cg_skipDeadPlayersWhenDead");
			layouter.AddToggleField(_Tr("Preferences", "Save Final Score Screenshot"), "cg_autoScreenshot");
			layouter.AddToggleField(_Tr("Preferences", "Debug Hit Detection"), "cg_debugHitTest");
			layouter.AddSliderField(_Tr("Preferences", "Hit Test Debugger Size"), "cg_dbgHitTestSize",
			64, 256, 8, ConfigNumberFormatter(0, "px"));
			layouter.AddControl(_Tr("Preferences", "Toggle Hit Debugger Zoom"), "cg_keyToggleHitTestZoom");
			layouter.AddSliderField(_Tr("Preferences", "Hit Debugger Fade Time"), "cg_dbgHitTestFadeTime",
			1, 20, 1, ConfigNumberFormatter(0, "s"));
			layouter.AddToggleField(_Tr("Preferences", "Debug Weapon Spread"), "cg_debugAim");
			layouter.AddToggleField(_Tr("Preferences", "Debug Block Cursor"), "cg_debugBlockCursor");
			layouter.AddToggleField(_Tr("Preferences", "Debug Players Hitbox"), "cg_debugPlayerHitboxes");
			layouter.AddToggleField(_Tr("Preferences", "Debug Weapon Anchor Points"), "cg_debugToolSkinAnchors");

			layouter.FinishLayout();
		}
	}

	class InterfaceOptions : spades::ui::UIElement {
		spades::ui::EventHandler@ OnEditHUDRequested;
		private void OnEditHUDClicked(spades::ui::UIElement@ sender) {
			if (OnEditHUDRequested !is null)
				OnEditHUDRequested(this);
		}

		InterfaceOptions(spades::ui::UIManager@ manager,
			PreferenceViewOptions@ options, FontManager@ fontManager,
			HeadingNavIndex@ nav = null) {
			super(manager);

			StandardPreferenceLayouter layouter(this, fontManager);
			@layouter.HeadingNav = nav;

			layouter.AddHeading(_Tr("Preferences", "Heads-Up Display"));
			layouter.AddToggleField(_Tr("Preferences", "Hide HUD"), "cg_hideHud");
			layouter.AddSliderField(_Tr("Preferences", "HUD Color"), "cg_hudColor",
			0, 10, 1, ConfigHUDColorFormatter());
			layouter.AddRGBSlider(_Tr("Preferences", "Custom Color"),
			array<string> = { "cg_hudColorR", "cg_hudColorG", "cg_hudColorB"});
			layouter.AddButtonField(_Tr("Preferences", "HUD Edge Positions"),
				_Tr("Preferences", "Edit"), spades::ui::EventHandler(this.OnEditHUDClicked), options.GameActive);
			layouter.AddChoiceField(_Tr("Preferences", "HUD Ammo Style"), "cg_hudAmmoStyle",
									array<string> = {_Tr("Preferences", "NORMAL"),
													 _Tr("Preferences", "SIMPLE")},
									array<int> = {0, 1});
			layouter.AddChoiceField(_Tr("Preferences", "Show Alive Player Count"), "cg_hudPlayerCount",
									array<string> = {_Tr("Preferences", "OFF"),
													 _Tr("Preferences", "Top"),
													 _Tr("Preferences", "Bottom")},
									array<int> = {0, 1, 2});
			layouter.AddChoiceField(_Tr("Preferences", "Show Client Statistics"), "cg_stats",
									array<string> = {_Tr("Preferences", "OFF"),
													 _Tr("Preferences", "Top"),
													 _Tr("Preferences", "Bottom")},
									array<int> = {0, 2, 1});
			layouter.AddToggleField(_Tr("Preferences", "Small Stats Font"), "cg_statsSmallFont");
			layouter.AddToggleField(_Tr("Preferences", "Show Player Statistics"), "cg_playerStats");
			layouter.AddToggleField(_Tr("Preferences", "Show Placed Blocks"), "cg_playerStatsShowPlacedBlocks");
			layouter.AddSliderField(_Tr("Preferences", "Player Statistics Height"), "cg_playerStatsHeight",
			0, 100, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddSliderField(_Tr("Preferences", "Chat Height"), "cg_chatHeight",
			10, 100, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddSliderField(_Tr("Preferences", "Chat Fade Time"), "cg_chatFadeTime",
			5, 20, 1, ConfigNumberFormatter(0, "s"));
			layouter.AddSliderField(_Tr("Preferences", "Killfeed Height"), "cg_killfeedHeight",
			10, 100, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddSliderField(_Tr("Preferences", "Killfeed Fade Time"), "cg_killfeedFadeTime",
			5, 20, 1, ConfigNumberFormatter(0, "s"));
			layouter.AddToggleField(_Tr("Preferences", "Killfeed Icons"), "cg_killfeedIcons");
			layouter.AddToggleField(_Tr("Preferences", "Show Dominations"), "cg_killfeedStreaks");

			layouter.AddHeading(_Tr("Preferences", "Minimap"));
			layouter.AddSliderField(_Tr("Preferences", "Minimap Size"), "cg_minimapSize",
			128, 256, 8, ConfigNumberFormatter(0, "px"));
			layouter.AddSliderField(_Tr("Preferences", "Minimap Scale Mode"), "cg_minimapScaleMode",
			0, 3, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderField(_Tr("Preferences", "Minimap Opacity"), "cg_minimapOpacity",
			0.1, 1, 0.1, ConfigNumberFormatter(1, ""));
			layouter.AddToggleField(_Tr("Preferences", "Use Weapon Icons"),	 "cg_minimapPlayerIcon");
			layouter.AddToggleField(_Tr("Preferences", "Use Random Colors"), "cg_minimapPlayerColor");
			layouter.AddChoiceField(_Tr("Preferences", "Show Map Location"), "cg_minimapCoords",
									array<string> = {_Tr("Preferences", "OFF"),
													 _Tr("Preferences", "Bottom"),
													 _Tr("Preferences", "Side")},
									array<int> = {0, 1, 2});
			layouter.AddToggleField(_Tr("Preferences", "Show Player Names"), "cg_minimapPlayerNames");

			layouter.AddHeading(_Tr("Preferences", "Target"));
			layouter.AddTargetPreview();
			layouter.AddHeading("");
			layouter.AddChoiceField(_Tr("Preferences", "Target Type"), "cg_target",
									array<string> = {_Tr("Preferences", "OFF"),
													 _Tr("Preferences", "Default"),
													 _Tr("Preferences", "Custom")},
									array<int> = {0, 1, 2});
			layouter.AddToggleField(_Tr("Preferences", "Lines"), "cg_targetLines");
			layouter.AddSliderField(_Tr("Preferences", "Color"), "cg_targetColor",
			0, 6, 1, ConfigTargetColorFormatter());
			layouter.AddRGBSlider(_Tr("Preferences", "Custom Color"),
			array<string> = { "cg_targetColorR", "cg_targetColorG", "cg_targetColorB"});
			layouter.AddSliderField(_Tr("Preferences", "Opacity"), "cg_targetAlpha",
			0, 255, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderGroup(_Tr("Preferences", "Length"),
			array<string> = { "cg_targetSizeHorizontal", "cg_targetSizeVertical"},
			1, 10, 1, 0, array<string> = { "", "" });
			layouter.AddSliderField(_Tr("Preferences", "Thickness"), "cg_targetThickness",
			1, 4, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddSliderField(_Tr("Preferences", "Gap"), "cg_targetGap",
			-10, 10, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddToggleField(_Tr("Preferences", "Dynamic"), "cg_targetDynamic");
			layouter.AddSliderField(_Tr("Preferences", "Dynamic Split Dist"), "cg_targetDynamicSplitdist",
			1, 20, 1, ConfigNumberFormatter(0, ""));
			layouter.AddToggleField(_Tr("Preferences", "Outline"), "cg_targetOutline");
			layouter.AddRGBSlider(_Tr("Preferences", "Outline Color"),
			array<string> = { "cg_targetOutlineColorR", "cg_targetOutlineColorG", "cg_targetOutlineColorB"});
			layouter.AddSliderField(_Tr("Preferences", "Outline Opacity"), "cg_targetOutlineAlpha",
			0, 255, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderField(_Tr("Preferences", "Outline Thickness"), "cg_targetOutlineThickness",
			1, 4, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddToggleField(_Tr("Preferences", "Rounded Corners Style"), "cg_targetOutlineRoundedStyle");
			layouter.AddToggleField(_Tr("Preferences", "Center Dot"), "cg_targetDot");
			layouter.AddRGBSlider(_Tr("Preferences", "Center Dot Color"),
			array<string> = { "cg_targetDotColorR", "cg_targetDotColorG", "cg_targetDotColorB"});
			layouter.AddSliderField(_Tr("Preferences", "Center Dot Opacity"), "cg_targetDotAlpha",
			0, 255, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderField(_Tr("Preferences", "Center Dot Thickness"), "cg_targetDotThickness",
			1, 4, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddToggleField(_Tr("Preferences", "T Style"), "cg_targetTStyle");

			layouter.AddHeading(_Tr("Preferences", "Scope"));
			layouter.AddScopePreview();
			layouter.AddHeading("");
			layouter.AddChoiceField(_Tr("Preferences", "Reflex Sight"), "cg_reflexScope",
									array<string> = {_Tr("Preferences", "OFF"),
													 _Tr("Preferences", "Dot"),
													 _Tr("Preferences", "Cross")},
									array<int> = {0, 3, 4});
			layouter.AddSliderField(_Tr("Preferences", "Scope Type"), "cg_pngScope",
			0, 3, 1, ConfigScopeTypeFormatter());
			layouter.AddToggleField(_Tr("Preferences", "Lines"), "cg_scopeLines");
			layouter.AddRGBSlider(_Tr("Preferences", "Color"),
			array<string> = { "cg_scopeColorR", "cg_scopeColorG", "cg_scopeColorB"});
			layouter.AddSliderField(_Tr("Preferences", "Opacity"), "cg_scopeAlpha",
			0, 255, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderGroup(_Tr("Preferences", "Length"),
			array<string> = { "cg_scopeSizeHorizontal", "cg_scopeSizeVertical"},
			1, 10, 1, 0, array<string> = { "", "" });
			layouter.AddSliderField(_Tr("Preferences", "Thickness"), "cg_scopeThickness",
			1, 4, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddSliderField(_Tr("Preferences", "Gap"), "cg_scopeGap",
			-10, 10, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddToggleField(_Tr("Preferences", "Dynamic"), "cg_scopeDynamic");
			layouter.AddSliderField(_Tr("Preferences", "Dynamic Split Dist"), "cg_scopeDynamicSplitdist",
			1, 20, 1, ConfigNumberFormatter(0, ""));
			layouter.AddToggleField(_Tr("Preferences", "Outline"), "cg_scopeOutline");
			layouter.AddRGBSlider(_Tr("Preferences", "Outline Color"),
			array<string> = { "cg_scopeOutlineColorR", "cg_scopeOutlineColorG", "cg_scopeOutlineColorB"});
			layouter.AddSliderField(_Tr("Preferences", "Outline Opacity"), "cg_scopeOutlineAlpha",
			0, 255, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderField(_Tr("Preferences", "Outline Thickness"), "cg_scopeOutlineThickness",
			1, 4, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddToggleField(_Tr("Preferences", "Rounded Corners Style"), "cg_scopeOutlineRoundedStyle");
			layouter.AddToggleField(_Tr("Preferences", "Center Dot"), "cg_scopeDot");
			layouter.AddRGBSlider(_Tr("Preferences", "Center Dot Color"),
			array<string> = { "cg_scopeDotColorR", "cg_scopeDotColorG", "cg_scopeDotColorB"});
			layouter.AddSliderField(_Tr("Preferences", "Center Dot Opacity"), "cg_scopeDotAlpha",
			0, 255, 1, ConfigNumberFormatter(0, ""));
			layouter.AddSliderField(_Tr("Preferences", "Center Dot Thickness"), "cg_scopeDotThickness",
			1, 4, 1, ConfigNumberFormatter(0, "px"));
			layouter.AddToggleField(_Tr("Preferences", "T Style"), "cg_scopeTStyle");

			layouter.AddHeading(_Tr("Preferences", "World"));
			layouter.AddToggleField(_Tr("Preferences", "Damage Indicators"), "cg_damageIndicators");
			layouter.AddToggleField(_Tr("Preferences", "Show Hover Player Names"), "cg_playerNames");
			layouter.AddToggleField(_Tr("Preferences", "Show Dead Player Names"), "cg_playerNamesDead");

			layouter.FinishLayout();
		}
	}

	class RendererOptionsPanel : spades::ui::UIElement {
		RendererOptionsPanel(spades::ui::UIManager@ manager,
			PreferenceViewOptions@ options, FontManager@ fontManager,
			HeadingNavIndex@ nav = null) {
			super(manager);

			StandardPreferenceLayouter layouter(this, fontManager);
			@layouter.HeadingNav = nav;

			layouter.AddHeading(_Tr("Preferences", "General"));
			layouter.AddSliderField(_Tr("Preferences", "Render Scale"), "r_scale",
			0.2, 1, 0.01, ConfigNumberFormatter(0, "%", "", 100));
			layouter.AddSliderField(_Tr("Preferences", "Render Scaling Filter"), "r_scaleFilter",
			0, 2, 1, ConfigRenderScaleFormatter());
			layouter.AddToggleField(_Tr("Preferences", "Rendering Statistics"), "r_debugTiming");
			layouter.AddToggleField(_Tr("Preferences", "Allow CPU Rendering"), "r_allowSoftwareRendering");

			layouter.AddHeading(_Tr("Preferences", "World"));
			layouter.AddToggleField(_Tr("Preferences", "Dynamic Lights"), "r_dlights");
			layouter.AddToggleField(_Tr("Preferences", "Tracers Lights"), "cg_tracerLights");
			layouter.AddToggleField(_Tr("Preferences", "Depth Prepass"), "r_depthPrepass");
			layouter.AddToggleField(_Tr("Preferences", "Occlusion Querying"), "r_occlusionQuery");
			layouter.AddToggleField(_Tr("Preferences", "Object Outlines"), "r_outlines");

			layouter.AddHeading(_Tr("Preferences", "Post-processing"));
			layouter.AddToggleField(_Tr("Preferences", "Depth Of Field"), "r_depthOfField");
			layouter.AddToggleField(_Tr("Preferences", "Camera Blur"), "r_cameraBlur");
			layouter.AddToggleField(_Tr("Preferences", "FXAA Anti-Aliasing"), "r_fxaa");
			layouter.AddToggleField(_Tr("Preferences", "Lens Flares"), "r_lensFlare");
			layouter.AddToggleField(_Tr("Preferences", "Flares Lights"), "r_lensFlareDynamic");
			layouter.AddToggleField(_Tr("Preferences", "Color Correction"), "r_colorCorrection");
			layouter.AddSliderField(_Tr("Preferences", "Sharpening"), "r_sharpen",
			0, 1, 0.1, ConfigNumberFormatter(1, ""));
			layouter.AddSliderField(_Tr("Preferences", "Saturation"), "r_saturation",
			0, 2, 0.1, ConfigNumberFormatter(1, ""));
			layouter.AddSliderField(_Tr("Preferences", "Exposure"), "r_exposureValue",
			-18, 18, 0.1, ConfigNumberFormatter(1, "EV"));

			layouter.AddHeading(_Tr("Preferences", "Software Renderer"));
			layouter.AddSliderField(_Tr("Preferences", "Thread Count"), "r_swNumThreads",
			1, 16, 1, ConfigNumberFormatter(0, _Tr("Preferences", " Threads")));
			layouter.AddSliderField(_Tr("Preferences", "Undersampling"), "r_swUndersampling",
			0, 4, 1, ConfigNumberFormatter(0, "x"));

			layouter.FinishLayout();
		}
	}

	class ControlOptionsPanel : spades::ui::UIElement {
		ControlOptionsPanel(spades::ui::UIManager@ manager,
			PreferenceViewOptions@ options, FontManager@ fontManager,
			HeadingNavIndex@ nav = null) {
			super(manager);

			StandardPreferenceLayouter layouter(this, fontManager);
			@layouter.HeadingNav = nav;

			layouter.AddHeading(_Tr("Preferences", "Weapons/Tools"));
			layouter.AddControl(_Tr("Preferences", "Attack"), "cg_keyAttack");
			layouter.AddControl(_Tr("Preferences", "Alt. Attack"), "cg_keyAltAttack");
			layouter.AddToggleField(_Tr("Preferences", "Hold Aim Down Sight"), "cg_holdAimDownSight");
			layouter.AddSliderField(_Tr("Preferences", "Mouse Sensitivity Type"), "cg_mouseSensScale",
			0, 5, 1, ConfigSensScaleFormatter());
			layouter.AddSliderField(_Tr("Preferences", "Mouse Sensitivity"), "cg_mouseSensitivity",
			0.1, 10, 0.1, ConfigNumberFormatter(1, ""));
			layouter.AddSliderField(_Tr("Preferences", "ADS Mouse Sens. Scale"), "cg_zoomedMouseSensScale",
			0.05, 3, 0.05, ConfigNumberFormatter(2, "x"));
			layouter.AddToggleField(_Tr("Preferences", "Mouse Acceleration"), "cg_mouseAccel");
			layouter.AddSliderField(_Tr("Preferences", "Exponential Power"), "cg_mouseExpPower",
			0.5, 1.5, 0.02, ConfigNumberFormatter(2, "", "^"));
			layouter.AddToggleField(_Tr("Preferences", "Invert Y-axis Mouse Input"), "cg_invertMouseY");
			layouter.AddControl(_Tr("Preferences", "Reload"), "cg_keyReloadWeapon");
			layouter.AddControl(_Tr("Preferences", "Equip Spade"), "cg_keyToolSpade");
			layouter.AddControl(_Tr("Preferences", "Equip Block"), "cg_keyToolBlock");
			layouter.AddControl(_Tr("Preferences", "Equip Weapon"), "cg_keyToolWeapon");
			layouter.AddControl(_Tr("Preferences", "Equip Grenade"), "cg_keyToolGrenade");
			layouter.AddControl(_Tr("Preferences", "Last Used Tool"), "cg_keyLastTool");
			layouter.AddPlusMinusField(_Tr("Preferences", "Switch Tools by Wheel"), "cg_switchToolByWheel");

			layouter.AddHeading(_Tr("Preferences", "Movement"));
			layouter.AddControl(_Tr("Preferences", "Move Forward"), "cg_keyMoveForward");
			layouter.AddControl(_Tr("Preferences", "Move Backward"), "cg_keyMoveBackward");
			layouter.AddControl(_Tr("Preferences", "Move Left"), "cg_keyMoveLeft");
			layouter.AddControl(_Tr("Preferences", "Move Right"), "cg_keyMoveRight");
			layouter.AddControl(_Tr("Preferences", "Crouch"), "cg_keyCrouch");
			layouter.AddControl(_Tr("Preferences", "Sneak"), "cg_keySneak");
			layouter.AddControl(_Tr("Preferences", "Jump"), "cg_keyJump");
			layouter.AddControl(_Tr("Preferences", "Sprint"), "cg_keySprint");

			layouter.AddHeading(_Tr("Preferences", "Color Palette"));
			layouter.AddControl(_Tr("Preferences", "Capture Color"), "cg_keyCaptureColor");
			layouter.AddControl(_Tr("Preferences", "Navigate up"), "cg_keyPaletteUp");
			layouter.AddControl(_Tr("Preferences", "Navigate down"), "cg_keyPaletteDown");
			layouter.AddControl(_Tr("Preferences", "Navigate left"), "cg_keyPaletteLeft");
			layouter.AddControl(_Tr("Preferences", "Navigate right"), "cg_keyPaletteRight");
			layouter.AddControl(_Tr("Preferences", "Toggle extended palette"), "cg_keyExtendedPalette");

			layouter.AddHeading(_Tr("Preferences", "Misc"));
			layouter.AddControl(_Tr("Preferences", "Scoreboard"), "cg_keyScoreboard");
			layouter.AddToggleField(_Tr("Preferences", "Hold Large Map"), "cg_holdMapZoom");
			layouter.AddControl(_Tr("Preferences", "Minimap Scale"), "cg_keyChangeMapScale");
			layouter.AddControl(_Tr("Preferences", "Toggle Map"), "cg_keyToggleMapZoom");
			layouter.AddControl(_Tr("Preferences", "Flashlight"), "cg_keyFlashlight");
			layouter.AddControl(_Tr("Preferences", "Global Chat"), "cg_keyGlobalChat");
			layouter.AddControl(_Tr("Preferences", "Team Chat"), "cg_keyTeamChat");
			layouter.AddControl(_Tr("Preferences", "Chat Log"), "cg_keyChatLog");
			layouter.AddControl(_Tr("Preferences", "Chat Zoom"), "cg_keyZoomChatLog");
			layouter.AddControl(_Tr("Preferences", "Pie Menu"), "cg_keyPieMenu");
			layouter.AddControl(_Tr("Preferences", "Limbo Menu"), "cg_keyLimbo");
			layouter.AddControl(_Tr("Preferences", "Save Map"), "cg_keySaveMap");
			layouter.AddControl(_Tr("Preferences", "Save Sceneshot"), "cg_keySceneshot");
			layouter.AddControl(_Tr("Preferences", "Save Screenshot"), "cg_keyScreenshot");
			layouter.AddControl(_Tr("Preferences", "Master Volume Up"), "cg_keyVolumeUp");
			layouter.AddControl(_Tr("Preferences", "Master Volume Down"), "cg_keyVolumeDown");
			layouter.AddControl(_Tr("Preferences", "Force Spectator Mode"), "cg_keyStaffSpectating");

			layouter.FinishLayout();
		}
	}

	class MiscOptionsPanel : spades::ui::UIElement {
		MiscOptionsPanel(spades::ui::UIManager@ manager,
			PreferenceViewOptions@ options, FontManager@ fontManager,
			HeadingNavIndex@ nav = null) {
			super(manager);

			StandardPreferenceLayouter layouter(this, fontManager);
			@layouter.HeadingNav = nav;

			layouter.AddHeading(_Tr("Preferences", "General"));
			ConfigSlider@ fpsSlider = layouter.AddSliderField(_Tr("Preferences", "FPS Limit"), "cl_fps",
			0, 300, 1, ConfigFPSFormatter());
			@fpsSlider.mapper = FPSValueMapper();
			layouter.AddSliderField(_Tr("Preferences", "Console Scrollback Lines"), "cl_consoleScrollbackLines",
			100, 2000, 100, ConfigNumberFormatter(0, ""));
			ConfigField@ urlField = layouter.AddInputField(_Tr("Preferences", "Server List URL"), "cl_serverListUrl", true, true);
			ConfigField@ modUrlField = layouter.AddInputField(_Tr("Preferences", "Mod List URL"), "cl_modsIndexUrl", true, true);
			layouter.AddToggleField(_Tr("Preferences", "Allow Unicode"), "cg_unicode");
			layouter.AddChoiceStringField(_Tr("Preferences", "Screenshot Format"), "cg_screenshotFormat",
					 array<string> = { "PNG", "JPEG", "TGA" },
					 array<string> = { "png", "jpeg", "tga" });
			layouter.AddSliderField(_Tr("Preferences", "JPEG Quality"), "core_jpegQuality",
			1, 100, 1, ConfigNumberFormatter(0, "%"));
			layouter.AddToggleField(_Tr("Preferences", "Enable Startup Window"), "cl_showStartupWindow");

			layouter.AddHeading(_Tr("Preferences", "Demo Recording"));
			layouter.AddControl(_Tr("Preferences", "Start/Stop Recording"), "cg_keyDemoRecord");
			layouter.AddToggleField(_Tr("Preferences", "Auto Record"), "cg_demoAutoRecord");
			layouter.AddToggleField(_Tr("Preferences", "Auto Delete Old Demos"), "cg_demoAutoPrune");
			layouter.AddSliderField(_Tr("Preferences", "Max Stored Demos"), "cg_demoMaxFiles",
				1, 256, 1, ConfigNumberFormatter(0, ""));

			layouter.AddHeading(_Tr("Preferences", "Demo Playback"));
			layouter.AddControl(_Tr("Preferences", "Pause/Resume"), "cg_keyDemoPlayPause");
			layouter.AddControl(_Tr("Preferences", "Speed Up"), "cg_keyDemoSpeedUp");
			layouter.AddControl(_Tr("Preferences", "Slow Down"), "cg_keyDemoSlowDown");
			layouter.AddControl(_Tr("Preferences", "Seek Forward"), "cg_keyDemoSeekForward");
			layouter.AddControl(_Tr("Preferences", "Seek Backward"), "cg_keyDemoSeekBackward");
			layouter.AddControl(_Tr("Preferences", "Toggle Demo HUD"), "cg_keyDemoToggleHud");

			layouter.FinishLayout();
		}
	}
}
