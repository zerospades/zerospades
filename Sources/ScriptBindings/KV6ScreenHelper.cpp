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

#include "ScriptManager.h"
#include <Gui/KV6ScreenHelper.h>

namespace spades {
	static gui::KV6ScreenHelper* CreateKV6ScreenHelper() { return new gui::KV6ScreenHelper(); }

	class KV6ScreenHelperRegistrar : public ScriptObjectRegistrar {
	public:
		KV6ScreenHelperRegistrar() : ScriptObjectRegistrar("KV6ScreenHelper") {}

		void Register(ScriptManager* manager, Phase phase) override {
			asIScriptEngine* eng = manager->GetEngine();
			int r;
			eng->SetDefaultNamespace("spades");
			switch (phase) {
				case PhaseObjectType:
					r = eng->RegisterObjectType("KV6ScreenHelper", 0, asOBJ_REF);
					manager->CheckError(r);
					break;
				case PhaseObjectMember:
					r = eng->RegisterObjectBehaviour(
					  "KV6ScreenHelper", asBEHAVE_FACTORY, "KV6ScreenHelper@ f()",
					  asFUNCTION(CreateKV6ScreenHelper), asCALL_CDECL);
					manager->CheckError(r);
					r = eng->RegisterObjectBehaviour("KV6ScreenHelper", asBEHAVE_ADDREF, "void f()",
					                                 asMETHOD(gui::KV6ScreenHelper, AddRef),
					                                 asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectBehaviour("KV6ScreenHelper", asBEHAVE_RELEASE, "void f()",
					                                 asMETHOD(gui::KV6ScreenHelper, Release),
					                                 asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "KV6ScreenHelper", "array<string>@ GetFolders(const string& in)",
					  asMETHOD(gui::KV6ScreenHelper, GetFolders), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "KV6ScreenHelper", "array<string>@ GetFiles(const string& in)",
					  asMETHOD(gui::KV6ScreenHelper, GetFiles), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper", "bool Exists(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, Exists),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper",
					                              "bool IsFolder(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, IsFolder),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper",
					                              "int64 GetFileSize(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, GetFileSize),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper",
					                              "bool CreateFolder(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, CreateFolder),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper", "bool Delete(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, Delete),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "KV6ScreenHelper", "bool Rename(const string& in, const string& in)",
					  asMETHOD(gui::KV6ScreenHelper, Rename), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper", "string DefaultDir()",
					                              asMETHOD(gui::KV6ScreenHelper, DefaultDir),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper",
					                              "string ParentDir(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, ParentDir),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("KV6ScreenHelper",
					                              "VoxelModel@ Load(const string& in)",
					                              asMETHOD(gui::KV6ScreenHelper, Load),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "KV6ScreenHelper", "bool Save(VoxelModel@+, const string& in)",
					  asMETHOD(gui::KV6ScreenHelper, Save), asCALL_THISCALL);
					manager->CheckError(r);
					break;
				default: break;
			}
		}
	};

	static KV6ScreenHelperRegistrar registrar;
} // namespace spades
