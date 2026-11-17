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

#include "NumberEntry.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace spades {
	namespace gui {
		namespace {
			// Long enough for a signed number with a few decimals, short enough
			// that nothing pathological is ever parsed or drawn.
			const std::size_t kMaxLength = 16;

			bool IsHexChar(char c) {
				return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
			}

			bool IsNumberChar(char c) {
				return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == ',';
			}
		} // namespace

		std::string NumberEntry::Format(float value, int decimals) {
			char buf[64];
			std::snprintf(buf, sizeof(buf), "%.*f", std::max(0, decimals), value);
			return buf;
		}

		bool NumberEntry::Parse(const std::string& text, float& out) {
			std::string str = text;
			for (char& c : str)
				if (c == ',')
					c = '.';
			const char* begin = str.c_str();
			char* end = nullptr;
			float parsed = std::strtof(begin, &end);
			if (end == begin || !std::isfinite(parsed))
				return false;
			while (*end == ' ' || *end == '\t')
				end++;
			if (*end != '\0')
				return false;
			out = parsed;
			return true;
		}

		NumberEntry::NumberEntry() : entry(kMaxLength) {}

		std::string NumberEntry::FormatHex(float value) {
			char buf[16];
			std::snprintf(buf, sizeof(buf), "#%06X",
			              unsigned(std::max(0.0F, std::min(16777215.0F, value))));
			return buf;
		}

		bool NumberEntry::ParseHex(const std::string& text, float& out) {
			std::string digits;
			for (char c : text) {
				if (c == '#' || c == ' ')
					continue;
				if (!IsHexChar(c))
					return false;
				digits += c;
			}
			if (digits.size() == 3) // the short form doubles each digit
				digits = {digits[0], digits[0], digits[1], digits[1], digits[2], digits[2]};
			if (digits.size() != 6)
				return false;
			out = float(std::strtoul(digits.c_str(), nullptr, 16));
			return true;
		}

		void NumberEntry::BeginHex(float value) {
			hex = true;
			entry.Begin(FormatHex(value), true);
		}

		namespace {
			// A recursive descent over: sum := product (('+'|'-') product)*,
			// product := factor (('*'|'/') factor)*, factor := ('+'|'-') factor |
			// number | '(' sum ')'.
			class Arithmetic {
			public:
				explicit Arithmetic(const std::string& text) : text(text) {}

				bool Run(double& out) {
					if (!Sum(out))
						return false;
					Skip();
					return at == text.size() && std::isfinite(out);
				}

			private:
				const std::string& text;
				std::size_t at = 0;
				int depth = 0;

				void Skip() {
					while (at < text.size() && (text[at] == ' ' || text[at] == '\t'))
						at++;
				}
				bool Take(char c) {
					Skip();
					if (at < text.size() && text[at] == c) {
						at++;
						return true;
					}
					return false;
				}
				bool Sum(double& out) {
					if (!Product(out))
						return false;
					while (true) {
						double rhs;
						if (Take('+')) {
							if (!Product(rhs))
								return false;
							out += rhs;
						} else if (Take('-')) {
							if (!Product(rhs))
								return false;
							out -= rhs;
						} else {
							return true;
						}
					}
				}
				bool Product(double& out) {
					if (!Factor(out))
						return false;
					while (true) {
						double rhs;
						if (Take('*')) {
							if (!Factor(rhs))
								return false;
							out *= rhs;
						} else if (Take('/')) {
							if (!Factor(rhs) || rhs == 0.0)
								return false;
							out /= rhs;
						} else {
							return true;
						}
					}
				}
				bool Factor(double& out) {
					if (++depth > 64) // nothing typed nests that deep
						return false;
					bool ok;
					if (Take('-')) {
						ok = Factor(out);
						out = -out;
					} else if (Take('+')) {
						ok = Factor(out);
					} else if (Take('(')) {
						ok = Sum(out) && Take(')');
					} else {
						ok = Number(out);
					}
					depth--;
					return ok;
				}
				bool Number(double& out) {
					Skip();
					std::string digits;
					while (at < text.size() &&
					       ((text[at] >= '0' && text[at] <= '9') || text[at] == '.' || text[at] == ',')) {
						digits += text[at] == ',' ? '.' : text[at];
						at++;
					}
					if (digits.empty() || digits == ".")
						return false;
					char* end = nullptr;
					out = std::strtod(digits.c_str(), &end);
					return end && *end == '\0';
				}
			};
		} // namespace

		bool NumberEntry::Evaluate(const std::string& text, float& out) {
			double value = 0.0;
			if (!Arithmetic(text).Run(value) || std::fabs(value) > 1.0e30)
				return false;
			out = float(value);
			return true;
		}

		void NumberEntry::Begin(float value, int decimals) {
			hex = false;
			entry.Begin(Format(value, decimals), true);
		}

		void NumberEntry::End() { entry.End(); }

		void NumberEntry::Settle() {
			if (entry.HasSelection())
				entry.SetCaret(entry.Caret(), false);
		}

		void NumberEntry::Insert(const std::string& s) {
			if (!IsEditing())
				return;
			std::string accepted;
			for (char c : s)
				if (hex ? (IsHexChar(c) || c == '#') : IsNumberChar(c))
					accepted += c;
			if (!accepted.empty())
				entry.Insert(accepted); // in place of the value, first time round
		}

		void NumberEntry::Backspace() {
			Settle();
			entry.Backspace(false);
		}

		void NumberEntry::Delete() {
			Settle();
			entry.Delete(false);
		}

		void NumberEntry::MoveCaret(int by) {
			Settle();
			entry.Move(by, false);
		}

		void NumberEntry::CaretToStart() {
			Settle();
			entry.ToStart(false);
		}

		void NumberEntry::CaretToEnd() {
			Settle();
			entry.ToEnd(false);
		}

		bool NumberEntry::Value(float& out) const {
			return IsEditing() && (hex ? ParseHex(Text(), out) : Parse(Text(), out));
		}
	} // namespace gui
} // namespace spades
