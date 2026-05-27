/*
 Copyright (c) 2026 Francois ND
 based on code of OpenSpades (c) yvt 2013.

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

	// -------------------------------------------------------------------------
	// Filename helpers

	string StripDemoPath(string path) {
		// Remove everything up to and including the last '/' (handles both
		// relative "Demos/name.dem" and absolute "/some/dir/Demos/name.dem").
		for (int i = int(path.length) - 1; i >= 0; i--) {
			if (path[i] == uint8(0x2F)) { // '/'
				path = path.substr(i + 1);
				break;
			}
		}
		// Remove ".dem" suffix
		if (path.length > 4 and path.substr(path.length - 4) == ".dem")
			path = path.substr(0, path.length - 4);
		return path;
	}

	bool DemoIsDigit(uint8 c) {
		return c >= uint8(0x30) and c <= uint8(0x39); // '0'..'9'
	}

	// Replace underscores with spaces for display (unescape '-'-encoded fields).
	string DemoUnderscoreToSpace(string s) {
		for (uint i = 0; i < s.length; i++) {
			if (s[i] == uint8(0x5F)) // '_'
				s[i] = uint8(0x20);  // ' '
		}
		return s;
	}

	// Capitalize the first letter of a string.
	string DemoCapFirst(string s) {
		if (s.length == 0)
			return s;
		if (s[0] >= uint8(0x61) and s[0] <= uint8(0x7A)) // 'a'..'z'
			s[0] = s[0] - uint8(0x61) + uint8(0x41);     // to 'A'..'Z'
		return s;
	}

	// Return a human-readable game mode label from a sanitized mode token.
	string DemoFormatGameMode(string raw) {
		if (raw == "ctf")   return "CTF";
		if (raw == "tc")    return "TC";
		if (raw == "arena") return "Arena";
		if (raw == "game")  return "Game";
		return DemoCapFirst(raw);
	}

	// Split a string on '-' (ASCII 0x2D) and return the parts.
	string[] DemoSplitByDash(string s) {
		string[] result;
		int start = 0;
		for (int i = 0; i <= int(s.length); i++) {
			if (i == int(s.length) or s[i] == uint8(0x2D)) {
				result.insertLast(s.substr(start, i - start));
				start = i + 1;
			}
		}
		return result;
	}

	// Format a file size in bytes as a human-readable string.
	string DemoFormatFileSize(int64 bytes) {
		if (bytes < 0)
			return "";
		if (bytes < 1024)
			return bytes + " B";
		if (bytes < 1024 * 1024)
			return (bytes / 1024) + " KB";
		return (bytes / (1024 * 1024)) + " MB";
	}

	// -------------------------------------------------------------------------
	// Parsed demo metadata

	class DemoInfo {
		bool structured;    // false when the filename doesn't follow the expected pattern
		string displayName; // stripped filename, always set
		string timestamp;   // "YYYY-MM-DD HH:MM" — only when structured is true
		string gameMode;    // display-ready mode, e.g. "CTF" — only when structured
		string mapName;     // display-ready map name — only when structured
		string serverName;  // display-ready server name — only when structured
		string fileSize;    // human-readable size, e.g. "1 MB" — always set

		DemoInfo() { structured = false; }
	}

	// Parse a full demo path (e.g. "Demos/2025-03-14-14-30-myserver-dust-ctf.dem")
	// into a DemoInfo.  Falls back to displayName-only when the name does not
	// conform to the expected YYYY-MM-DD-HH-MM[-server[-map[-gamemode]]] format.
	//
	// Escaping: '-' is the field separator and is never emitted within a field
	// (SanitizeComponent replaces it with '_').  DemoUnderscoreToSpace reverses
	// this for display.
	DemoInfo ParseDemoFilename(string path, MainScreenHelper@ helper) {
		DemoInfo info;
		info.displayName = StripDemoPath(path);
		string name = info.displayName;

		int64 sz = helper.GetDemoFileSize(path);
		info.fileSize = DemoFormatFileSize(sz);

		// The timestamp prefix occupies exactly 16 characters:
		//   YYYY-MM-DD-HH-MM  (positions 0-15)
		// followed by either end-of-string or a '-' separator.
		if (name.length >= 16) {
			bool valid =
				DemoIsDigit(name[0])  and DemoIsDigit(name[1])  and
				DemoIsDigit(name[2])  and DemoIsDigit(name[3])  and
				name[4]  == uint8(0x2D) and // '-'
				DemoIsDigit(name[5])  and DemoIsDigit(name[6])  and
				name[7]  == uint8(0x2D) and
				DemoIsDigit(name[8])  and DemoIsDigit(name[9])  and
				name[10] == uint8(0x2D) and
				DemoIsDigit(name[11]) and DemoIsDigit(name[12]) and
				name[13] == uint8(0x2D) and
				DemoIsDigit(name[14]) and DemoIsDigit(name[15]);

			if (valid and (name.length == 16 or name[16] == uint8(0x2D))) {
				// Format as "YYYY-MM-DD HH:MM"
				info.timestamp = name.substr(0, 4) + "-" +
				                 name.substr(5, 2) + "-" +
				                 name.substr(8, 2) + " " +
				                 name.substr(11, 2) + ":" +
				                 name.substr(14, 2);

				// Parse optional context: server[-map[-gamemode]]
				if (name.length > 17) {
					string context = name.substr(17);
					string[] parts = DemoSplitByDash(context);

					// server: all parts except the last two
					// map:    second-to-last part
					// mode:   last part
					// Minimum: just gamemode (1 part), server+gamemode (2 parts),
					// server+map+gamemode (3+ parts).
					if (parts.length == 1) {
						info.gameMode = DemoFormatGameMode(parts[0]);
					} else if (parts.length == 2) {
						info.serverName = DemoUnderscoreToSpace(parts[0]);
						info.gameMode   = DemoFormatGameMode(parts[1]);
					} else {
						// server = parts[0..n-3] joined with space, map = parts[n-2], mode = parts[n-1]
						string server = parts[0];
						for (uint i = 1; i < parts.length - 2; i++)
							server += " " + parts[i];
						info.serverName = DemoUnderscoreToSpace(server);
						info.mapName    = DemoUnderscoreToSpace(parts[parts.length - 2]);
						info.gameMode   = DemoFormatGameMode(parts[parts.length - 1]);
					}
				}

				info.structured = true;
			}
		}

		return info;
	}

	// -------------------------------------------------------------------------
	// List item

	class DemoListItem : spades::ui::ButtonBase {
		string filename;    // full path
		DemoInfo info;      // parsed metadata
		float colDateWidth;
		float colModeWidth;
		float colMapWidth;
		float colSizeWidth;
		float totalWidth;

		DemoListItem(spades::ui::UIManager@ manager, string filename,
		             DemoInfo info,
		             float colDateWidth, float colModeWidth, float colMapWidth, float colSizeWidth,
		             float totalWidth) {
			super(manager);
			this.filename     = filename;
			this.info         = info;
			this.colDateWidth = colDateWidth;
			this.colModeWidth = colModeWidth;
			this.colMapWidth  = colMapWidth;
			this.colSizeWidth = colSizeWidth;
			this.totalWidth   = totalWidth;
		}

		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos  = ScreenPosition;
			Vector2 size = Size;

			Vector4 bgcolor = Vector4(1.0F, 1.0F, 1.0F, 0.0F);
			Vector4 fgcolor = Vector4(1.0F, 1.0F, 1.0F, 1.0F);

			if (Pressed and Hover) {
				bgcolor.w = 0.3F;
			} else if (Hover) {
				bgcolor.w = 0.15F;
			}

			r.ColorNP = bgcolor;
			r.DrawImage(null, AABB2(pos.x + 1.0F, pos.y + 1.0F, size.x, size.y));

			if (info.structured) {
				// Column order (left to right): Server | Map | Mode | Timestamp | Size
				// Server is variable-width; the four rightmost columns are fixed-width.
				// Use totalWidth (= contentsWidth from the layout) so that fixed-column
				// positions match the headers, which are also sized from contentsWidth.
				// (size.x is narrower by the ListView scrollbar width.)
				float fixedRight = colMapWidth + colModeWidth + colDateWidth + colSizeWidth;
				float serverWidth = totalWidth - fixedRight;
				float x = pos.x + 2.0F;
				Font.Draw(info.serverName, Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x = pos.x + serverWidth + 2.0F;
				Font.Draw(info.mapName,    Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x += colMapWidth;
				Font.Draw(info.gameMode,   Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x += colModeWidth;
				Font.Draw(info.timestamp,  Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
				x += colDateWidth;
				Font.Draw(info.fileSize,   Vector2(x, pos.y + 2.0F), 1.0F, fgcolor);
			} else {
				// Filename does not match the structured format; display it as-is.
				Font.Draw(info.displayName, pos + Vector2(2.0F, 2.0F), 1.0F, fgcolor);
			}
		}
	}

	funcdef void DemoListItemEventHandler(DemoListModel@ sender, string filename);

	class DemoListModel : spades::ui::ListViewModel {
		spades::ui::UIManager@ manager;
		MainScreenHelper@ helper;
		string[] list;
		DemoListItem@[] itemElements;
		float colDateWidth;
		float colModeWidth;
		float colMapWidth;
		float colSizeWidth;
		float totalWidth;

		DemoListItemEventHandler@ ItemActivated;
		DemoListItemEventHandler@ ItemDoubleClicked;

		DemoListModel(spades::ui::UIManager@ manager, MainScreenHelper@ helper,
		              string[]@ list,
		              float colDateWidth, float colModeWidth, float colMapWidth, float colSizeWidth,
		              float totalWidth) {
			@this.manager    = manager;
			@this.helper     = helper;
			this.list        = list;
			this.colDateWidth = colDateWidth;
			this.colModeWidth = colModeWidth;
			this.colMapWidth  = colMapWidth;
			this.colSizeWidth = colSizeWidth;
			this.totalWidth   = totalWidth;

			itemElements.resize(list.length);
		}

		int NumRows {
			get { return int(list.length); }
		}

		private void OnItemClicked(spades::ui::UIElement@ sender) {
			DemoListItem@ item = cast<DemoListItem>(sender);
			if (ItemActivated !is null)
				ItemActivated(this, item.filename);
		}

		private void OnItemDoubleClicked(spades::ui::UIElement@ sender) {
			DemoListItem@ item = cast<DemoListItem>(sender);
			if (ItemDoubleClicked !is null)
				ItemDoubleClicked(this, item.filename);
		}

		spades::ui::UIElement@ CreateElement(int row) {
			if (itemElements[row] is null) {
				DemoInfo info = ParseDemoFilename(list[row], helper);
				DemoListItem i(manager, list[row], info, colDateWidth, colModeWidth, colMapWidth, colSizeWidth, totalWidth);
				@i.Activated     = spades::ui::EventHandler(this.OnItemClicked);
				@i.DoubleClicked = spades::ui::EventHandler(this.OnItemDoubleClicked);
				@itemElements[row] = i;
			}
			return itemElements[row];
		}

		void RecycleElement(spades::ui::UIElement@ elem) {}
	}

	// -------------------------------------------------------------------------
	// Column header

	class DemoListHeader : spades::ui::UIElement {
		string Text;
		DemoListHeader(spades::ui::UIManager@ manager) { super(manager); }
		void Render() {
			Renderer@ r = Manager.Renderer;
			Vector2 pos  = ScreenPosition;
			Vector2 size = Size;

			Font.Draw(Text, pos + Vector2(2.0F, (size.y - Font.Measure(Text).y) * 0.5F), 1.0F,
			          Vector4(1.0F, 1.0F, 1.0F, 1.0F));
		}
	}

}
