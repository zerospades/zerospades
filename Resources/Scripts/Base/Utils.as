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
	// AngelScript doesn't seem to support user-defined template functions...
	uint Min(uint a, uint b) { return (a < b) ? a : b; }
	uint Max(uint a, uint b) { return (a > b) ? a : b; }
	int Min(int a, int b) { return (a < b) ? a : b; }
	int Max(int a, int b) { return (a > b) ? a : b; }
	float Min(float a, float b) { return (a < b) ? a : b; }
	float Max(float a, float b) { return (a > b) ? a : b; }
	uint Clamp(uint v, uint lo, uint hi) { return Min(Max(v, lo), hi); }
	int Clamp(int v, int lo, int hi) { return Min(Max(v, lo), hi); }
	float Clamp(float v, float lo, float hi) { return Min(Max(v, lo), hi); }

	uint8 ToUpper(uint8 c) { return (c >= uint8(0x61) and c <= uint8(0x7A)) ? uint8(c - 0x61 + 0x41) : c; }
	uint8 ToLower(uint8 c) { return (c >= uint8(0x41) and c <= uint8(0x5A)) ? uint8(c - 0x41 + 0x61) : c; }
	string ToUpperCase(string s) {
		for (uint i = 0, len = s.length; i < len; i++)
			s[i] = ToUpper(s[i]);
		return s;
	}
	string ToLowerCase(string s) {
		for (uint i = 0, len = s.length; i < len; i++)
			s[i] = ToLower(s[i]);
		return s;
	}

	uint StringCommonPrefixLength(string a, string b) {
		for (uint i = 0, ln = Min(a.length, b.length); i < ln; i++) {
			if (ToLower(a[i]) != ToLower(b[i]))
				return i;
		}
		return Min(a.length, b.length);
	}

	string FormatFileSize(int64 bytes) {
		if (bytes < 0)
			return "";
		if (bytes < 1024)
			return bytes + " B";
		if (bytes < 1024 * 1024)
			return (bytes / 1024) + " KB";
		return (bytes / (1024 * 1024)) + " MB";
	}

	class TargetParam {
		bool drawLines;
		bool useTStyle;
		Vector4 lineColor;
		float lineGap;
		Vector2 lineLength;
		float lineThickness;
		bool drawOutline;
		bool useRoundedStyle;
		Vector4 outlineColor;
		float outlineThickness;
		bool drawDot;
		Vector4 dotColor;
		float dotThickness;
	}

	void DrawTargetOutlineRect(Renderer@ r, int x, int y, int w, int h, TargetParam param) {
		int thickness = int(param.outlineThickness);
		r.ColorNP = param.outlineColor;
		if (param.useRoundedStyle) {
			r.DrawFilledRect(x, y-thickness, w, y);
			r.DrawFilledRect(x, h, w, h+thickness);
		} else {
			r.DrawFilledRect(x-thickness, y-thickness, w+thickness, y);
			r.DrawFilledRect(x-thickness, h, w+thickness, h+thickness);
		}
		r.DrawFilledRect(x-thickness, y, x, h);
		r.DrawFilledRect(w, y, w+thickness, h);
	}

	void DrawTargetRect(Renderer@ r, int x, int y, int w, int h, TargetParam param) {
		if (param.drawOutline)
			DrawTargetOutlineRect(r, x, y, w, h, param);
		r.ColorNP = param.lineColor;
		r.DrawFilledRect(x, y, w, h);
	}

	// Variants of the two above on a grid of whole screen pixels, `unit` UI units
	// apart (1 / Renderer.ScreenPixelRatio); see DrawTarget.
	void DrawTargetOutlineRect(Renderer@ r, float unit, int x, int y, int w, int h, TargetParam param) {
		int thickness = int(param.outlineThickness);
		r.ColorNP = param.outlineColor;
		if (param.useRoundedStyle) {
			r.DrawFilledRect(x * unit, (y-thickness) * unit, w * unit, y * unit);
			r.DrawFilledRect(x * unit, h * unit, w * unit, (h+thickness) * unit);
		} else {
			r.DrawFilledRect((x-thickness) * unit, (y-thickness) * unit, (w+thickness) * unit, y * unit);
			r.DrawFilledRect((x-thickness) * unit, h * unit, (w+thickness) * unit, (h+thickness) * unit);
		}
		r.DrawFilledRect((x-thickness) * unit, y * unit, x * unit, h * unit);
		r.DrawFilledRect(w * unit, y * unit, (w+thickness) * unit, h * unit);
	}

	void DrawTargetRect(Renderer@ r, float unit, int x, int y, int w, int h, TargetParam param) {
		if (param.drawOutline)
			DrawTargetOutlineRect(r, unit, x, y, w, h, param);
		r.ColorNP = param.lineColor;
		r.DrawFilledRect(x * unit, y * unit, w * unit, h * unit);
	}

	// Sizes in UI pixels, rounded to whole screen pixels (at least 1 if set to 1+).
	float TargetPixels(float v, float scale) { return floor(v * scale + 0.5F); }
	float TargetThickness(float v, float scale) {
		float p = TargetPixels(v, scale);
		return (v >= 1.0F and p < 1.0F) ? 1.0F : p;
	}

	void DrawTarget(Renderer@ r, Vector2 pos, TargetParam uiParam) {
		// A crosshair is pixel-exact: laid out on the UI's grid and then scaled by a
		// fractional interface scale, equal arms would come out 1 or 2 pixels thick
		// depending on where they fall. So each size is scaled once and rounded to
		// whole screen pixels, and the crosshair laid out on the screen's grid. At
		// 1x this is the plain UI layout. (Mirrors Gui/UI/Settings/CrosshairRenderer.)
		float scale = r.ScreenPixelRatio;
		float unit = 1.0F / scale;
		TargetParam param = uiParam;
		param.lineGap = TargetPixels(uiParam.lineGap, scale);
		param.lineLength = Vector2(TargetPixels(uiParam.lineLength.x, scale),
			TargetPixels(uiParam.lineLength.y, scale));
		param.lineThickness = TargetThickness(uiParam.lineThickness, scale);
		param.outlineThickness = TargetThickness(uiParam.outlineThickness, scale);
		param.dotThickness = TargetThickness(uiParam.dotThickness, scale);
		pos *= scale;

		int thickness = int(param.lineThickness);
		int x = int(pos.x) - thickness / 2;
		int y = int(pos.y) - thickness / 2;
		int w = x + thickness;
		int h = y + thickness;

		// draw target lines
		if (param.drawLines) {
			int lineGap = int(param.lineGap);

			// horizontal lines
			int lineLengthHorizontal = int(param.lineLength.x);
			int innerLeft = x - lineGap;
			int innerRight = w + lineGap;
			int outerLeft = innerLeft - lineLengthHorizontal;
			int outerRight = innerRight + lineLengthHorizontal;
			DrawTargetRect(r, unit, outerLeft, y, innerLeft, h, param); // left
			DrawTargetRect(r, unit, innerRight, y, outerRight, h, param); // right

			// vertical lines
			int lineLengthVertical = int(param.lineLength.y);
			int innerTop = y - lineGap;
			int innerBottom = h + lineGap;
			int outerTop = innerTop - lineLengthVertical;
			int outerBottom = innerBottom + lineLengthVertical;
			if (not param.useTStyle)
				DrawTargetRect(r, unit, x, outerTop, w, innerTop, param); // top
			DrawTargetRect(r, unit, x, innerBottom, w, outerBottom, param); // bottom
		}

		// draw center dot
		if (param.drawDot) {
			thickness = int(param.dotThickness);
			x = int(pos.x) - thickness / 2;
			y = int(pos.y) - thickness / 2;
			w = x + thickness;
			h = y + thickness;

			if (param.drawOutline)
				DrawTargetOutlineRect(r, unit, x, y, w, h, param);
			r.ColorNP = param.dotColor;
			r.DrawFilledRect(x * unit, y * unit, w * unit, h * unit);
		}
	}

	/** Returns the byte index for a certain character index. */
	int GetByteIndexForString(string s, int charIndex, int start = 0) {
		int len = s.length;

		while (start < len and charIndex > 0) {
			int c = s[start];

			if ((c & 0x80) == 0) {
				charIndex--;
				start += 1;
			} else if ((c & 0xE0) == 0xC0) {
				charIndex--;
				start += 2;
			} else if ((c & 0xF0) == 0xE0) {
				charIndex--;
				start += 3;
			} else if ((c & 0xF8) == 0xF0) {
				charIndex--;
				start += 4;
			} else if ((c & 0xFC) == 0xF8) {
				charIndex--;
				start += 5;
			} else if ((c & 0xFE) == 0xFC) {
				charIndex--;
				start += 6;
			} else {
				// invalid!
				charIndex--;
				start++;
			}
		}

		if (start > len)
			start = len;

		return start;
	}

	/** Returns the byte index for a certain character index. */
	int GetCharIndexForString(string s, int byteIndex, int start = 0) {
		int len = s.length;
		int charIndex = 0;

		while (start < len and start < byteIndex and byteIndex > 0) {
			int c = s[start];

			if ((c & 0x80) == 0) {
				charIndex++;
				start += 1;
			} else if ((c & 0xE0) == 0xC0) {
				charIndex++;
				start += 2;
			} else if ((c & 0xF0) == 0xE0) {
				charIndex++;
				start += 3;
			} else if ((c & 0xF8) == 0xF0) {
				charIndex++;
				start += 4;
			} else if ((c & 0xFC) == 0xF8) {
				charIndex++;
				start += 5;
			} else if ((c & 0xFE) == 0xFC) {
				charIndex++;
				start += 6;
			} else {
				// invalid!
				charIndex++;
				start++;
			}
		}

		return charIndex;
	}
 }
