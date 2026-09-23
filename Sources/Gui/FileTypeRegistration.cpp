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

#include "FileTypeRegistration.h"

#ifdef _WIN32
#include <Gui/ModelFileTypes.h>
#include <windows.h>
// After windows.h, which it expects to be included first.
#include <shlobj.h>

#include <vector>
#endif

namespace spades {
#ifdef _WIN32
	namespace {
		/** The program identifier the model type is registered under. One per file
		 *  type, named after the program so it cannot collide with another. */
		const wchar_t* const kProgId = L"ZeroSpades.kv6";
		const wchar_t* const kProgIdDescription = L"KV6 voxel model";

		std::wstring Widen(const std::string& s) {
			if (s.empty())
				return std::wstring();
			int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
			if (n <= 0)
				return std::wstring();
			std::wstring w(size_t(n), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
			return w;
		}

		std::string ErrorMessage(const char* what, LSTATUS status) {
			return std::string(what) + " failed (error " + std::to_string(long(status)) + ")";
		}

		/** The absolute path of the running executable, which is what the desktop
		 *  has to be told to run. */
		std::wstring ExecutablePath() {
			std::vector<wchar_t> buf(32768);
			DWORD n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
			if (n == 0 || n >= buf.size())
				return std::wstring();
			return std::wstring(buf.data(), n);
		}

		LSTATUS WriteString(HKEY root, const std::wstring& subKey, const wchar_t* valueName,
		                    const std::wstring& value) {
			HKEY key = nullptr;
			LSTATUS status = RegCreateKeyExW(root, subKey.c_str(), 0, nullptr,
			                                 REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &key,
			                                 nullptr);
			if (status != ERROR_SUCCESS)
				return status;

			status = RegSetValueExW(key, valueName, 0, REG_SZ,
			                        reinterpret_cast<const BYTE*>(value.c_str()),
			                        DWORD((value.size() + 1) * sizeof(wchar_t)));
			RegCloseKey(key);
			return status;
		}
	} // namespace

	std::string RegisterModelFileTypes() {
		const std::wstring exe = ExecutablePath();
		if (exe.empty())
			return "Could not determine where this program is installed";

		const std::wstring extension = Widen(gui::KV6DocumentExtension());
		if (extension.empty())
			return "No model file type to register";

		const std::wstring progIdKey = std::wstring(L"Software\\Classes\\") + kProgId;

		// What the type is called, what it looks like, and what opens it. The
		// quotes around the path and the argument are what keeps a program or a
		// model in a folder with a space in its name working.
		LSTATUS status = WriteString(HKEY_CURRENT_USER, progIdKey, nullptr, kProgIdDescription);
		if (status != ERROR_SUCCESS)
			return ErrorMessage("Writing the file type", status);

		status = WriteString(HKEY_CURRENT_USER, progIdKey + L"\\DefaultIcon", nullptr, exe + L",0");
		if (status != ERROR_SUCCESS)
			return ErrorMessage("Writing the file type icon", status);

		status = WriteString(HKEY_CURRENT_USER, progIdKey + L"\\shell\\open\\command", nullptr,
		                     L"\"" + exe + L"\" \"%1\"");
		if (status != ERROR_SUCCESS)
			return ErrorMessage("Writing the open command", status);

		// Offered for the extension rather than claiming it: whatever opens these
		// files today keeps doing so, and this program joins the "Open with" list.
		status = WriteString(HKEY_CURRENT_USER,
		                     L"Software\\Classes\\" + extension + L"\\OpenWithProgids", kProgId,
		                     std::wstring());
		if (status != ERROR_SUCCESS)
			return ErrorMessage("Adding the program to the file type", status);

		// Shell caches associations; without this the change shows up only after a
		// sign-out.
		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
		return std::string();
	}

	std::string UnregisterModelFileTypes() {
		const std::wstring extension = Widen(gui::KV6DocumentExtension());
		const std::wstring progIdKey = std::wstring(L"Software\\Classes\\") + kProgId;

		// Deleting the tree under the program's own identifier takes the command
		// and the icon with it. A key that is already gone is not a failure: the
		// point is that it ends up absent.
		LSTATUS status = RegDeleteTreeW(HKEY_CURRENT_USER, progIdKey.c_str());
		if (status != ERROR_SUCCESS && status != ERROR_FILE_NOT_FOUND)
			return ErrorMessage("Removing the file type", status);

		if (!extension.empty()) {
			HKEY key = nullptr;
			status = RegOpenKeyExW(HKEY_CURRENT_USER,
			                       (L"Software\\Classes\\" + extension + L"\\OpenWithProgids")
			                         .c_str(),
			                       0, KEY_SET_VALUE, &key);
			if (status == ERROR_SUCCESS) {
				RegDeleteValueW(key, kProgId);
				RegCloseKey(key);
			} else if (status != ERROR_FILE_NOT_FOUND) {
				return ErrorMessage("Removing the program from the file type", status);
			}
		}

		SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
		return std::string();
	}
#else
	namespace {
		// Linux packages install a MIME definition and a desktop entry; a macOS
		// bundle carries its document types in its Info.plist. Neither is a thing
		// the running program writes for itself.
		const char* const kNotOurJob = "This system registers file types when the program is "
		                               "installed, not from the program itself";
	} // namespace

	std::string RegisterModelFileTypes() { return kNotOurJob; }

	std::string UnregisterModelFileTypes() { return kNotOurJob; }
#endif
} // namespace spades
