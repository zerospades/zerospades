/*
 Copyright (c) 2026 Fran6nd, ZeroSpades developers.

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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

namespace spades {

	// Only .kv6 is editable; .2kv6 and .vxl are listed but not yet supported.
	bool KV6IsEditable(const string&in name) {
		return name.length >= 4 and EqualsIgnoringCase(name.substr(name.length - 4), ".kv6");
	}
	bool KV6IsModelFile(const string&in name) {
		return KV6IsEditable(name)
		   or (name.length >= 5 and EqualsIgnoringCase(name.substr(name.length - 5), ".2kv6"))
		   or (name.length >= 4 and EqualsIgnoringCase(name.substr(name.length - 4), ".vxl"));
	}

	// A single row in the KV6 file explorer: a folder or a `.kv6` file.
	class KV6ListItem : spades::ui::ButtonBase {
		string entryName;
		bool isFolder;

		KV6ListItem(spades::ui::UIManager@ manager, string entryName, bool isFolder) {
			super(manager);
			this.entryName = entryName;
			this.isFolder = isFolder;
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			Vector4 bgcolor = Vector4(1.0F, 1.0F, 1.0F, 0.0F);
			if (Pressed and Hover)
				bgcolor.w = 0.3F;
			else if (Hover)
				bgcolor.w = 0.15F;
			r.ColorNP = bgcolor;
			r.DrawImage(null, AABB2(pos.x + 1.0F, pos.y + 1.0F, size.x, size.y));

			bool supported = isFolder or KV6IsEditable(entryName);
			Vector4 fg;
			string label;
			if (isFolder) {
				fg = Vector4(0.55F, 0.78F, 1.0F, 1.0F);
				label = entryName + "/";
			} else if (supported) {
				fg = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
				label = entryName;
			} else {
				fg = Vector4(0.55F, 0.55F, 0.55F, 1.0F);
				label = entryName + "   (not implemented)";
			}
			Font.Draw(label, pos + Vector2(6.0F, 2.0F), 1.0F, fg);
		}
	}

	funcdef void KV6ListEventHandler(string entryName, bool isFolder);

	class KV6ListModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		string[] names;
		bool[] folders;
		KV6ListItem@[] elems;

		KV6ListEventHandler@ ItemActivated;
		KV6ListEventHandler@ ItemDoubleClicked;

		KV6ListModel(spades::ui::UIManager@ manager, string[]@ names, bool[]@ folders) {
			@this.manager = manager;
			this.names = names;
			this.folders = folders;
			elems.resize(names.length);
		}

		int NumRows { get { return int(names.length); } }

		private void OnItemClicked(spades::ui::UIElement@ sender) {
			KV6ListItem@ item = cast<KV6ListItem>(sender);
			if (ItemActivated !is null)
				ItemActivated(item.entryName, item.isFolder);
		}
		private void OnItemDoubleClicked(spades::ui::UIElement@ sender) {
			KV6ListItem@ item = cast<KV6ListItem>(sender);
			if (ItemDoubleClicked !is null)
				ItemDoubleClicked(item.entryName, item.isFolder);
		}

		spades::ui::UIElement@ CreateElement(int row) {
			if (elems[row] is null) {
				KV6ListItem i(manager, names[row], folders[row]);
				@i.Activated = spades::ui::EventHandler(this.OnItemClicked);
				@i.DoubleClicked = spades::ui::EventHandler(this.OnItemDoubleClicked);
				@elems[row] = i;
			}
			return elems[row];
		}

		void RecycleElement(spades::ui::UIElement@ elem) {}
	}

	// A modal text prompt (used for "New Model" / "New Folder").
	class KV6NamePrompt : spades::ui::UIElement {
		spades::ui::EventHandler@ Closed;
		bool Result = false;
		string Text;

		private spades::ui::UIElement@ owner;
		private spades::ui::Field@ nameField;

		KV6NamePrompt(spades::ui::UIElement@ owner, string title, string initial) {
			super(owner.Manager);
			@this.owner = owner;
			@Font = Manager.RootElement.Font;
			Bounds = owner.Bounds;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;
			float w = Min(sw - 16.0F, 500.0F);
			float h = 160.0F;
			float x = (sw - w) * 0.5F;
			float y = (sh - h) * 0.5F;

			{
				spades::ui::Label bg(Manager);
				bg.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				bg.Bounds = AABB2(0.0F, y - 13.0F, Size.x, h + 27.0F);
				AddChild(bg);
			}
			{
				spades::ui::Label label(Manager);
				label.Text = title;
				label.Bounds = AABB2(x, y, w, 30.0F);
				label.Alignment = Vector2(0.0F, 0.5F);
				AddChild(label);
			}
			{
				@nameField = spades::ui::Field(Manager);
				nameField.Bounds = AABB2(x, y + 40.0F, w, 30.0F);
				nameField.Text = initial;
				nameField.SelectAll();
				AddChild(nameField);
			}
			{
				spades::ui::Button btn(Manager);
				btn.Caption = _Tr("MainScreen", "OK");
				btn.Bounds = AABB2(x + w - 320.0F, y + 90.0F, 150.0F, 30.0F);
				@btn.Activated = spades::ui::EventHandler(this.OnConfirm);
				AddChild(btn);
			}
			{
				spades::ui::Button btn(Manager);
				btn.Caption = _Tr("MainScreen", "Cancel");
				btn.Bounds = AABB2(x + w - 160.0F, y + 90.0F, 150.0F, 30.0F);
				@btn.Activated = spades::ui::EventHandler(this.OnCancel);
				AddChild(btn);
			}
		}

		void OnConfirm(spades::ui::UIElement@ sender) {
			Text = nameField.Text;
			Result = true;
			Close();
		}
		void OnCancel(spades::ui::UIElement@ sender) {
			Result = false;
			Close();
		}
		void Close() {
			owner.Enable = true;
			owner.Parent.RemoveChild(this);
			if (Closed !is null)
				Closed(this);
		}
		void Run() {
			owner.Enable = false;
			owner.Parent.AddChild(this);
			@Manager.ActiveElement = nameField;
		}
		void HotKey(string key) {
			if (IsEnabled and key == "Enter")
				OnConfirm(this);
			else if (IsEnabled and key == "Escape")
				OnCancel(this);
			else
				UIElement::HotKey(key);
		}
	}

	// A modal yes/no confirmation (used for deletion).
	class KV6ConfirmPrompt : spades::ui::UIElement {
		spades::ui::EventHandler@ Closed;
		bool Result = false;

		private spades::ui::UIElement@ owner;

		KV6ConfirmPrompt(spades::ui::UIElement@ owner, string message) {
			super(owner.Manager);
			@this.owner = owner;
			@Font = Manager.RootElement.Font;
			Bounds = owner.Bounds;

			float sw = Manager.ScreenWidth;
			float sh = Manager.ScreenHeight;
			float w = Min(sw - 16.0F, 500.0F);
			float h = 130.0F;
			float x = (sw - w) * 0.5F;
			float y = (sh - h) * 0.5F;

			{
				spades::ui::Label bg(Manager);
				bg.BackgroundColor = Vector4(0.0F, 0.0F, 0.0F, 0.9F);
				bg.Bounds = AABB2(0.0F, y - 13.0F, Size.x, h + 27.0F);
				AddChild(bg);
			}
			{
				spades::ui::Label label(Manager);
				label.Text = message;
				label.Bounds = AABB2(x, y, w, 40.0F);
				label.Alignment = Vector2(0.0F, 0.5F);
				AddChild(label);
			}
			{
				spades::ui::Button btn(Manager);
				btn.Caption = _Tr("MainScreen", "Delete");
				btn.Bounds = AABB2(x + w - 320.0F, y + 60.0F, 150.0F, 30.0F);
				@btn.Activated = spades::ui::EventHandler(this.OnConfirm);
				AddChild(btn);
			}
			{
				spades::ui::Button btn(Manager);
				btn.Caption = _Tr("MainScreen", "Cancel");
				btn.Bounds = AABB2(x + w - 160.0F, y + 60.0F, 150.0F, 30.0F);
				@btn.Activated = spades::ui::EventHandler(this.OnCancel);
				AddChild(btn);
			}
		}

		void OnConfirm(spades::ui::UIElement@ sender) {
			Result = true;
			Close();
		}
		void OnCancel(spades::ui::UIElement@ sender) {
			Result = false;
			Close();
		}
		void Close() {
			owner.Enable = true;
			owner.Parent.RemoveChild(this);
			if (Closed !is null)
				Closed(this);
		}
		void Run() {
			owner.Enable = false;
			owner.Parent.AddChild(this);
		}
		void HotKey(string key) {
			if (IsEnabled and key == "Escape")
				OnCancel(this);
			else
				UIElement::HotKey(key);
		}
	}
}
