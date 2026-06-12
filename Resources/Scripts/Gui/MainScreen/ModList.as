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

namespace spades {
	funcdef void ModListItemEventHandler(string modName);

	string FormatModSize(int64 bytes) {
		if (bytes < 1024)
			return "" + bytes + " B";
		if (bytes < 1024 * 1024)
			return "" + (bytes / 1024) + " KB";
		return "" + (bytes / (1024 * 1024)) + " MB";
	}

	class ModListItem : spades::ui::ButtonBase {
		string modName;
		int pakCount;
		int64 totalSize;
		bool enabled;   // present in the apply history
		bool exists;    // mod still present on disk
		int orderNum;   // 1-based apply position, 0 when disabled
		float checkColWidth;
		float orderColWidth;
		float nameColWidth;
		float countColWidth;
		float sizeColWidth;

		ModListItem(spades::ui::UIManager@ manager, string modName, int pakCount, int64 totalSize,
		            bool enabled, bool exists, int orderNum, float checkColWidth,
		            float orderColWidth, float nameColWidth, float countColWidth, float sizeColWidth) {
			super(manager);
			this.modName = modName;
			this.pakCount = pakCount;
			this.totalSize = totalSize;
			this.enabled = enabled;
			this.exists = exists;
			this.orderNum = orderNum;
			this.checkColWidth = checkColWidth;
			this.orderColWidth = orderColWidth;
			this.nameColWidth = nameColWidth;
			this.countColWidth = countColWidth;
			this.sizeColWidth = sizeColWidth;
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			Vector4 bgcolor = Vector4(1.0F, 1.0F, 1.0F, 0.0F);
			// White when disabled, green when enabled, orange when enabled but
			// the mod is no longer on disk.
			Vector4 fgcolor = Vector4(1.0F, 1.0F, 1.0F, 1.0F);
			if (enabled)
				fgcolor = exists ? Vector4(0.4F, 1.0F, 0.4F, 1.0F)
				                 : Vector4(1.0F, 0.62F, 0.1F, 1.0F);

			if (Pressed and Hover) {
				bgcolor.w = 0.3F;
			} else if (Hover) {
				bgcolor.w = 0.15F;
			}

			r.ColorNP = bgcolor;
			r.DrawImage(null, AABB2(pos.x + 1.0F, pos.y + 1.0F, size.x, size.y));

			// Checkbox.
			float boxSize = 14.0F;
			float boxX = pos.x + 5.0F;
			float boxY = pos.y + (size.y - boxSize) * 0.5F;
			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.25F);
			r.DrawImage(null, AABB2(boxX, boxY, boxSize, boxSize));
			if (enabled) {
				r.ColorNP = fgcolor;
				r.DrawImage(null, AABB2(boxX + 3.0F, boxY + 3.0F, boxSize - 6.0F, boxSize - 6.0F));
			}

			// Apply-order number (own column, blank when disabled).
			float x = pos.x + checkColWidth;
			if (enabled and orderNum > 0)
				Font.Draw("" + orderNum, Vector2(x + 2.0F, pos.y + 2.0F), 1.0F, fgcolor);

			x = pos.x + checkColWidth + orderColWidth + 2.0F;
			Font.Draw(modName, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			x = pos.x + checkColWidth + orderColWidth + nameColWidth + 2.0F;
			Font.Draw(exists ? ("" + pakCount) : "—", Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			x += countColWidth;
			Font.Draw(exists ? FormatModSize(totalSize) : "—", Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
		}
	}

	class ModListModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		ModsScreenHelper@ helper;
		string[] list;
		int[] orders;     // parallel to list: 1-based apply position, 0 if disabled
		bool[] exists;    // parallel to list: mod still present on disk
		float checkColWidth;
		float orderColWidth;
		float nameColWidth;
		float countColWidth;
		float sizeColWidth;
		ModListItem@[] itemElements;

		ModListItemEventHandler@ ItemActivated;

		ModListModel(spades::ui::UIManager@ manager, ModsScreenHelper@ helper, string[]@ list,
		             int[]@ orders, bool[]@ exists, float checkColWidth, float orderColWidth,
		             float nameColWidth, float countColWidth, float sizeColWidth) {
			@this.manager = manager;
			@this.helper = helper;
			this.list = list;
			this.orders = orders;
			this.exists = exists;
			this.checkColWidth = checkColWidth;
			this.orderColWidth = orderColWidth;
			this.nameColWidth = nameColWidth;
			this.countColWidth = countColWidth;
			this.sizeColWidth = sizeColWidth;

			itemElements.resize(list.length);
		}

		int NumRows { get { return int(list.length); } }

		private void OnItemClicked(spades::ui::UIElement@ sender) {
			ModListItem@ item = cast<ModListItem>(sender);
			if (ItemActivated !is null)
				ItemActivated(item.modName);
		}

		spades::ui::UIElement@ CreateElement(int row) {
			if (itemElements[row] is null) {
				string name = list[row];
				bool ex = exists[row];
				int count = ex ? helper.GetModPakCount(name) : 0;
				int64 size = ex ? helper.GetModTotalSize(name) : 0;
				ModListItem item(manager, name, count, size, orders[row] > 0, ex, orders[row],
				                 checkColWidth, orderColWidth, nameColWidth, countColWidth, sizeColWidth);
				@item.Activated = spades::ui::EventHandler(this.OnItemClicked);
				@itemElements[row] = item;
			}
			return itemElements[row];
		}

		void RecycleElement(spades::ui::UIElement@ elem) {}
	}

	class ModListHeader : spades::ui::UIElement {
		string Text;
		ModListHeader(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;
			Font.Draw(Text, pos + Vector2(2.0F, (size.y - Font.Measure(Text).y) * 0.5F), 1.0F,
			          Vector4(1.0F, 1.0F, 1.0F, 1.0F));
		}
	}

	// Simple fill-bar progress indicator. Fraction is clamped to [0, 1].
	class ModsProgressBar : spades::ui::UIElement {
		float Fraction = 0.0F;

		ModsProgressBar(spades::ui::UIManager@ manager) { super(manager); }

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos = ScreenPosition;
			Vector2 size = Size;

			float f = Fraction;
			if (f < 0.0F) f = 0.0F;
			if (f > 1.0F) f = 1.0F;

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.12F);
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x, size.y));

			r.ColorNP = Vector4(1.0F, 1.0F, 1.0F, 0.55F);
			r.DrawImage(null, AABB2(pos.x, pos.y, size.x * f, size.y));
		}
	}
}
