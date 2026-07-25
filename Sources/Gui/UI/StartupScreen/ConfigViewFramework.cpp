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

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "ConfigViewFramework.h"
#include "StartupScreenUI.h"
#include <Client/Fonts.h>
#include <Client/IFont.h>
#include <Client/IRenderer.h>
#include <Core/Debug.h>
#include <Core/Math.h>
#include <Core/Strings.h>
#include <Gui/StartupScreenHelper.h>
#include <Gui/UI/Framework/TextUtils.h>
#include <Gui/UI/Framework/UIManager.h>
#include <Gui/UI/Widgets/DrawUtils.h>

namespace spades {
	namespace gui {
		using ui::Button;
		using ui::CheckBox;
		using ui::Field;
		using ui::Label;
		using ui::RadioButton;
		using ui::SetColorNP;
		using ui::Slider;
		using ui::TextViewer;
		using ui::UIElement;
		using ui::UIManager;

		void WatchHelp(UIElement* elm, HelpTextHandler handler, const std::string& text) {
			elm->mouseEntered = [handler, text](UIElement&) { handler(text); };
		}

		void WatchHelpDeep(UIElement* elm, HelpTextHandler handler, const std::string& text) {
			if (elm->mouseEntered) {
				ui::EventHandler prev = elm->mouseEntered;
				elm->mouseEntered = [prev, handler, text](UIElement& e) {
					prev(e);
					handler(text);
				};
			} else {
				WatchHelp(elm, handler, text);
			}
			for (const Handle<UIElement>& child : elm->GetChildren())
				WatchHelpDeep(child.GetPointerOrNull(), handler, text);
		}

		void AddConfigLabel(UIElement& parent, float x, float y, float h,
		                    const std::string& text) {
			Handle<Label> label = Handle<Label>::New(&parent.GetManager());
			client::IFont* font = parent.GetFont();
			Vector2 siz = font ? font->Measure(text) : MakeVector2(0, 0);
			label->text = text;
			label->alignment = MakeVector2(0.0F, 0.5F);
			label->SetBounds(AABB2(x, y, siz.x, h));
			parent.AddChild(label.GetPointerOrNull());
		}

		// -- StartupScreenConfig --

		StartupScreenConfig::StartupScreenConfig(StartupScreenUI* ui, const std::string& cfg)
		    : ui(ui), cfg(cfg, nullptr), cfgName(cfg) {}

		std::string StartupScreenConfig::GetValue() { return static_cast<std::string>(cfg); }
		void StartupScreenConfig::SetValue(const std::string& v) { cfg = v; }
		std::string StartupScreenConfig::CheckValueCapability(const std::string& v) {
			return ui->GetHelper().CheckConfigCapability(cfgName, v);
		}

		// -- StartupScreenConfigSelectItemEditor --

		StartupScreenConfigSelectItemEditor::StartupScreenConfigSelectItemEditor(
		    StartupScreenUI* ui, Handle<StartupScreenGenericConfig> cfg, const std::string& values,
		    const std::string& descs)
		    : UIElement(&ui->GetUIManager()), ui(ui), config(std::move(cfg)) {
			this->descs = Split(descs, "|");
			this->values = Split(values, "|");

			{
				std::string desc = this->descs[0];
				size_t idx = desc.find(':');
				if (idx != std::string::npos)
					desc = desc.substr(0, idx);
				AddConfigLabel(*this, 0, 0, 24.0F, desc);
				this->label = desc;
			}

			for (size_t i = 0; i < this->values.size(); i++) {
				Handle<RadioButton> b = Handle<RadioButton>::New(&GetManager());
				std::string desc = this->descs[i + 1];
				size_t idx = desc.find(':');
				if (idx != std::string::npos)
					desc = desc.substr(0, idx);
				b->caption = desc;

				b->groupName = "hoge";
				StartupScreenGenericConfig* configPtr = config.GetPointerOrNull();
				std::string value = this->values[i];
				b->activated = [configPtr, value](UIElement&) { configPtr->SetValue(value); };
				buttons.push_back(b.GetPointerOrNull());
				AddChild(b.GetPointerOrNull());
			}
		}

		void StartupScreenConfigSelectItemEditor::LoadConfig() {
			std::string val = config->GetValue();
			for (size_t i = 0, count = values.size(); i < count; i++) {
				buttons[i]->toggled = (values[i] == val);
				buttons[i]->enable = CheckValueCapability(values[i]).empty();
			}
		}

		std::string
		StartupScreenConfigSelectItemEditor::CheckValueCapability(const std::string& v) {
			return config->CheckValueCapability(v);
		}

		void StartupScreenConfigSelectItemEditor::SetHelpTextHandler(HelpTextHandler handler) {
			for (size_t i = 0, count = values.size(); i < count; i++) {
				std::string desc = descs[i + 1];
				size_t idx = desc.find(':');
				if (idx == std::string::npos) {
					desc = descs[0];
					idx = desc.find(':');
				}
				desc = desc.substr(idx + 1);
				WatchHelp(buttons[i], handler, desc);
			}
		}

		void StartupScreenConfigSelectItemEditor::OnResized() {
			UIElement::OnResized();
			Vector2 sz = size;
			float h = 24.0F;
			float x = sz.x;
			client::IFont& guiFont = ui->GetFontManager().GetGuiFont();
			for (size_t i = buttons.size(); i > 0; i--) {
				RadioButton* b = buttons[i - 1];
				float w = guiFont.Measure(b->caption).x + 26.0F;
				x -= w + 2.0F;
				b->SetBounds(AABB2(x, 0.0F, w, h));
			}
		}

		// -- StartupScreenConfigCheckItemEditor --

		StartupScreenConfigCheckItemEditor::StartupScreenConfigCheckItemEditor(
		    StartupScreenUI* ui, Handle<StartupScreenGenericConfig> cfg,
		    const std::string& valueOff, const std::string& valueOn, const std::string& label,
		    const std::string& desc)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      desc(desc),
		      valueOff(valueOff),
		      valueOn(valueOn),
		      config(std::move(cfg)),
		      label(label) {
			Handle<CheckBox> b = Handle<CheckBox>::New(&GetManager());
			b->caption = label;
			b->activated = [this](UIElement& s) { StateChanged(s); };
			button = b.GetPointerOrNull();
			AddChild(button);
		}

		void StartupScreenConfigCheckItemEditor::LoadConfig() {
			std::string val = config->GetValue();
			button->toggled = (val != valueOff);
			button->enable = CheckValueCapability(valueOn).empty();
		}

		std::string StartupScreenConfigCheckItemEditor::CheckValueCapability(const std::string& v) {
			return config->CheckValueCapability(v);
		}

		void StartupScreenConfigCheckItemEditor::StateChanged(UIElement&) {
			config->SetValue(button->toggled ? valueOn : valueOff);
		}

		void StartupScreenConfigCheckItemEditor::SetHelpTextHandler(HelpTextHandler handler) {
			WatchHelp(button, handler, desc);
		}

		void StartupScreenConfigCheckItemEditor::OnResized() {
			UIElement::OnResized();
			button->SetBounds(AABB2(0.0F, 0.0F, size.x, 24.0F));
		}

		// -- StartupScreenConfigSliderItemEditor --

		StartupScreenConfigSliderItemEditor::StartupScreenConfigSliderItemEditor(
		    StartupScreenUI* ui, Handle<StartupScreenGenericConfig> cfg, double minValue,
		    double maxValue, double stepSize, const std::string& label, const std::string& desc,
		    Formatter formatter)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      desc(desc),
		      stepSize(stepSize),
		      config(std::move(cfg)),
		      formatter(std::move(formatter)),
		      label(label) {
			AddConfigLabel(*this, 0, 0, 24.0F, label);

			Handle<Slider> b = Handle<Slider>::New(&GetManager());
			b->minValue = minValue;
			b->maxValue = maxValue;

			// compute large change
			int steps = static_cast<int>((maxValue - minValue) / stepSize);
			steps = (steps + 9) / 10;
			b->largeChange = static_cast<double>(steps) * stepSize;
			b->smallChange = stepSize;

			b->changed = [this](UIElement& s) { StateChanged(s); };
			slider = b.GetPointerOrNull();
			AddChild(slider);

			Handle<Label> l = Handle<Label>::New(&GetManager());
			valueLabel = l.GetPointerOrNull();
			l->alignment = MakeVector2(0.5F, 0.5F);
			AddChild(valueLabel);
		}

		void StartupScreenConfigSliderItemEditor::UpdateLabel() {
			double v = slider->value;
			valueLabel->text = formatter.Format(static_cast<float>(v));
		}

		void StartupScreenConfigSliderItemEditor::LoadConfig() {
			std::string val = config->GetValue();
			double v = std::atof(val.c_str());
			// don't use `ScrollTo` here; this calls `changed` so
			// out-of-range value will be clamped and saved back
			slider->value = Clamp(v, slider->minValue, slider->maxValue);
			UpdateLabel();
		}

		void StartupScreenConfigSliderItemEditor::DoRounding() {
			double v = static_cast<double>(slider->value - slider->minValue);
			v = std::floor((v / stepSize) + 0.5) * stepSize;
			v += static_cast<double>(slider->minValue);
			slider->value = v;
		}

		void StartupScreenConfigSliderItemEditor::StateChanged(UIElement&) {
			DoRounding();
			// Serialize the value the way AngelScript's ToString(double) did:
			// integers without a decimal point, otherwise a short decimal.
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%g", slider->value);
			config->SetValue(buf);
			UpdateLabel();
		}

		void StartupScreenConfigSliderItemEditor::SetHelpTextHandler(HelpTextHandler handler) {
			WatchHelpDeep(slider, handler, desc);
		}

		void StartupScreenConfigSliderItemEditor::OnResized() {
			UIElement::OnResized();
			Vector2 sz = size;
			float h = 24.0F;
			slider->SetBounds(AABB2(160.0F, 2.0F, sz.x - 160.0F, h - 4.0F));
			valueLabel->SetBounds(AABB2(160.0F, 0.0F, sz.x - 160.0F, h));
		}

		// -- StartupScreenConfigFieldItemEditor --

		StartupScreenConfigFieldItemEditor::StartupScreenConfigFieldItemEditor(
		    StartupScreenUI* ui, Handle<StartupScreenGenericConfig> cfg, const std::string& label,
		    const std::string& desc)
		    : UIElement(&ui->GetUIManager()),
		      ui(ui),
		      desc(desc),
		      config(std::move(cfg)),
		      label(label) {
			AddConfigLabel(*this, 0, 0, 24.0F, label);

			Handle<Field> b = Handle<Field>::New(&GetManager());
			b->changed = [this](UIElement& s) { StateChanged(s); };
			field = b.GetPointerOrNull();
			field->removeNewlines = true;
			AddChild(field);
		}

		void StartupScreenConfigFieldItemEditor::LoadConfig() {
			field->SetText(config->GetValue());
		}

		void StartupScreenConfigFieldItemEditor::StateChanged(UIElement&) {
			config->SetValue(field->GetText());
		}

		void StartupScreenConfigFieldItemEditor::SetHelpTextHandler(HelpTextHandler handler) {
			WatchHelpDeep(field, handler, desc);
		}

		void StartupScreenConfigFieldItemEditor::OnResized() {
			UIElement::OnResized();
			field->SetBounds(AABB2(240.0F, 0.0F, size.x - 240.0F, 24.0F));
		}

		// -- StartupScreenComplexConfig --

		void StartupScreenComplexConfig::AddEditor(Handle<UIElement> editorElement) {
			SPAssert(presets.empty());
			StartupScreenConfigItemEditor* editor =
			    dynamic_cast<StartupScreenConfigItemEditor*>(editorElement.GetPointerOrNull());
			SPAssert(editor != nullptr);
			editors.push_back(editor);
			editorElements.push_back(std::move(editorElement));
		}

		void StartupScreenComplexConfig::AddPreset(Handle<StartupScreenComplexConfigPreset> preset) {
			SPAssert(preset->values.size() == editors.size());
			presets.push_back(std::move(preset));
		}

		std::string StartupScreenComplexConfig::GetValue() {
			std::vector<std::string> values;

			for (StartupScreenConfigItemEditor* editor : editors)
				values.push_back(editor->GetConfig().GetValue());
			for (size_t i = 0; i < presets.size(); i++) {
				const std::vector<std::string>& pval = presets[i]->values;
				size_t j = 0;
				size_t jc = pval.size();
				for (; j < jc; j++) {
					if (values[j] != pval[j])
						break;
				}
				if (j == jc)
					return std::to_string(static_cast<int>(i));
			}
			return "custom";
		}

		void StartupScreenComplexConfig::SetValue(const std::string& v) {
			if (v == "custom")
				return;
			size_t pId = static_cast<size_t>(std::atoi(v.c_str()));
			const std::vector<std::string>& pval = presets[pId]->values;
			for (size_t i = 0; i < pval.size(); i++)
				editors[i]->GetConfig().SetValue(pval[i]);
		}

		std::string StartupScreenComplexConfig::CheckValueCapability(const std::string& v) {
			if (v == "custom")
				return "";
			std::string ret = "";
			size_t pId = static_cast<size_t>(std::atoi(v.c_str()));
			const std::vector<std::string>& pval = presets[pId]->values;
			for (size_t i = 0; i < pval.size(); i++)
				ret += editors[i]->GetConfig().CheckValueCapability(pval[i]);
			return ret;
		}

		std::string StartupScreenComplexConfig::GetValuesString() {
			std::string out;
			for (size_t i = 0; i < presets.size(); i++) {
				out += std::to_string(static_cast<int>(i));
				out += "|";
			}
			out += "custom";
			return out;
		}

		std::string StartupScreenComplexConfig::GetDescriptionsString() {
			std::string out;
			for (const Handle<StartupScreenComplexConfigPreset>& preset : presets) {
				out += preset->name;
				out += "|";
			}
			out += _Tr("StartupScreen", "Custom");
			return out;
		}

		StartupScreenComplexConfigPreset::StartupScreenComplexConfigPreset(
		    const std::string& name, const std::string& values)
		    : name(name) {
			this->values = Split(values, "|");
		}

		// -- StartupScreenConfigComplexItemEditor --

		StartupScreenConfigComplexItemEditor::StartupScreenConfigComplexItemEditor(
		    StartupScreenUI* ui, Handle<StartupScreenComplexConfig> cfg, const std::string& label,
		    const std::string& desc)
		    : StartupScreenConfigSelectItemEditor(
		          ui, cfg.Cast<StartupScreenGenericConfig>(), cfg->GetValuesString(),
		          label + ":" + desc + "|" + cfg->GetDescriptionsString()),
		      dlgTitle(label) {
			buttons[buttons.size() - 1]->activated = [this](UIElement&) { RunDialog(); };
		}

		void StartupScreenConfigComplexItemEditor::RunDialog() {
			Handle<StartupScreenComplexConfigDialog> dlg =
			    Handle<StartupScreenComplexConfigDialog>::New(
			        &GetManager(), config.Cast<StartupScreenComplexConfig>());
			dlg->dialogDone = [this](UIElement&) { LoadConfig(); };
			dlg->title = dlgTitle;
			dlg->RunDialog();
		}

		// -- StartupScreenConfigViewModel --

		void StartupScreenConfigViewModel::Filter(const std::string& text) {
			if (text.empty()) {
				useFiltered = false;
				filtered.clear();
				return;
			}
			std::vector<Handle<UIElement>> newItems;
			for (const Handle<UIElement>& item : items) {
				StartupScreenConfigItemEditor* editor =
				    dynamic_cast<StartupScreenConfigItemEditor*>(item.GetPointerOrNull());
				if (editor == nullptr)
					continue;
				std::string label = editor->GetLabel();
				// case-insensitive contains
				std::string l = label, t = text;
				for (char& c : l)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				for (char& c : t)
					c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
				if (l.find(t) != std::string::npos)
					newItems.push_back(item);
			}
			filtered = std::move(newItems);
			useFiltered = true;
		}

		// -- StartupScreenConfigView --

		StartupScreenConfigView::StartupScreenConfigView(UIManager* manager)
		    : ui::ListViewBase(manager) {
			vmodel = Handle<StartupScreenConfigViewModel>::New();
			rowHeight = 30.0F;
		}

		void StartupScreenConfigView::Filter(const std::string& text) {
			vmodel->Filter(text);
			Reload();
		}

		void StartupScreenConfigView::SetHelpTextHandler(HelpTextHandler handler) {
			for (const Handle<UIElement>& elm : vmodel->items) {
				StartupScreenConfigItemEditor* item =
				    dynamic_cast<StartupScreenConfigItemEditor*>(elm.GetPointerOrNull());
				if (item != nullptr)
					item->SetHelpTextHandler(handler);
			}
		}

		void StartupScreenConfigView::LoadConfig() {
			for (const Handle<UIElement>& elm : vmodel->items) {
				StartupScreenConfigItemEditor* item =
				    dynamic_cast<StartupScreenConfigItemEditor*>(elm.GetPointerOrNull());
				if (item != nullptr)
					item->LoadConfig();
			}
		}

		// -- StartupScreenComplexConfigDialog --

		StartupScreenComplexConfigDialog::StartupScreenComplexConfigDialog(
		    UIManager* manager, Handle<StartupScreenComplexConfig> config)
		    : UIElement(manager), config(std::move(config)) {
			contentsTop = 50.0F;
			Vector2 sz = manager->GetRootElement().size;
			contentsHeight = sz.y - contentsTop * 2.0F;

			float mainWidth = sz.x - 220.0F;
			{
				Handle<TextViewer> e = Handle<TextViewer>::New(manager);
				e->SetBounds(AABB2(mainWidth, contentsTop + 30.0F, sz.x - mainWidth - 10.0F,
				                   contentsHeight - 60.0F));
				AddChild(e.GetPointerOrNull());
				helpView = e.GetPointerOrNull();
			}

			{
				Handle<StartupScreenConfigView> cfg =
				    Handle<StartupScreenConfigView>::New(manager);

				for (const Handle<UIElement>& editor : this->config->editorElements)
					cfg->AddRow(editor);

				cfg->Finalize();
				cfg->SetHelpTextHandler(
				    [this](const std::string& s) { HandleHelpText(s); });
				cfg->SetBounds(AABB2(10.0F, contentsTop + 30.0F, mainWidth - 20.0F,
				                     contentsHeight - 60.0F));
				AddChild(cfg.GetPointerOrNull());
				configView = cfg.GetPointerOrNull();
			}

			{
				Handle<Button> button = Handle<Button>::New(manager);
				button->caption = _Tr("StartupScreen", "Close");
				button->SetBounds(
				    AABB2(sz.x - 160.0F, contentsTop + contentsHeight - 30.0F, 150.0F, 30.0F));
				button->activated = [this](UIElement& s) { CloseActivated(s); };
				AddChild(button.GetPointerOrNull());
			}
		}

		void StartupScreenComplexConfigDialog::CloseActivated(UIElement&) {
			// keep ourselves alive while detaching and running the done handler
			Handle<StartupScreenComplexConfigDialog> keepAlive(this);
			if (oldRoot != nullptr) {
				oldRoot->enable = true;
				oldRoot = nullptr;
			}
			SetParent(nullptr);
			if (dialogDone)
				dialogDone(*this);
		}

		void StartupScreenComplexConfigDialog::HandleHelpText(const std::string& s) {
			helpView->SetText(s);
		}

		void StartupScreenComplexConfigDialog::RunDialog() {
			UIElement& root = GetManager().GetRootElement();

			for (const Handle<UIElement>& e : root.GetChildren()) {
				if (e->enable && e->visible) {
					oldRoot = e.GetPointerOrNull();
					e->enable = false;
					break;
				}
			}

			SetBounds(root.GetBounds());
			root.AddChild(this);
			configView->LoadConfig();
		}

		void StartupScreenComplexConfigDialog::Render() {
			client::IRenderer& r = GetManager().GetRenderer();
			Vector2 pos = GetScreenPosition();
			Vector2 sz = size;

			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.8F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y, sz.x, contentsTop - 15.0F));
			r.DrawImage(nullptr, AABB2(pos.x, contentsTop + contentsHeight + 15.0F, sz.x,
			                           contentsTop - 15.0F));

			SetColorNP(r, MakeVector4(0.0F, 0.0F, 0.0F, 0.95F));
			r.DrawImage(nullptr, AABB2(pos.x, contentsTop - 15.0F, sz.x, contentsHeight + 30.0F));

			SetColorNP(r, MakeVector4(1.0F, 1.0F, 1.0F, 0.07F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contentsTop - 14.0F, sz.x, 1.0F));
			r.DrawImage(nullptr,
			            AABB2(pos.x, pos.y + contentsTop + contentsHeight + 14.0F, sz.x, 1.0F));

			client::IFont* font = GetFont();
			SetColorNP(r, MakeVector4(0.8F, 0.8F, 0.8F, 1.0F));
			r.DrawImage(nullptr, AABB2(pos.x, pos.y + contentsTop, sz.x, 20.0F));
			if (font)
				font->Draw(title, MakeVector2(pos.x + 10.0F, pos.y + contentsTop), 1.0F,
				           MakeVector4(0.0F, 0.0F, 0.0F, 1.0F));

			UIElement::Render();
		}
	} // namespace gui
} // namespace spades
