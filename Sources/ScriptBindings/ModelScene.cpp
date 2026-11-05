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
 along with OpenSpades.  If not, see <http://www.gnu.org/licenses/>.

 */

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "ScriptManager.h"
#include <Client/IModel.h>
#include <Client/IRenderer.h>
#include <Core/VoxelModelLoader.h>

namespace spades {
	namespace client {
		/**
		 * A .2kv6 scene as a script holds it, registered once: the whole of it,
		 * its parts by their paths of names, and where each of its objects sits,
		 * all in the scene's frame, so drawn with one matrix they assemble. Its
		 * parts are the renderer's ("scene.2kv6#Gun/Bolt"), cached like any
		 * model. It holds its renderer, which holds none of it.
		 */
		class ScriptModelScene : public RefCountedObject {
		public:
			ScriptModelScene(IRenderer& renderer, const std::string& path)
			    : renderer(&renderer),
			      path(path),
			      transforms(VoxelModelLoader::SceneTransforms(path.c_str())) {}

			IModel* Whole() { return renderer->RegisterModel(path.c_str()).Unmanage(); }

			IModel* Part(const std::string& part) {
				return renderer->RegisterModel((path + "#" + part).c_str()).Unmanage();
			}

			IModel* NativePart(const std::string& part) {
				return renderer->RegisterModel((path + "##" + part).c_str()).Unmanage();
			}

			bool Has(const std::string& part) { return Find(part) != nullptr; }

			Matrix4 Transform(const std::string& part) {
				if (const Matrix4* m = Find(part))
					return *m;
				SPRaise("No object '%s' in '%s', or several share that path", part.c_str(),
				        path.c_str());
			}

			const std::string& Path() const { return path; }

		private:
			Handle<IRenderer> renderer;
			std::string path;
			std::vector<std::pair<std::string, Matrix4>> transforms;

			const Matrix4* Find(const std::string& part) const {
				for (const auto& t : transforms)
					if (t.first == part)
						return &t.second;
				return nullptr;
			}
		};

		namespace {
			// Script calls: a native failure becomes a script exception, as the
			// renderer's own registrations do.
			ScriptModelScene* RegisterScene(const std::string& path, IRenderer* r) {
				try {
					return Handle<ScriptModelScene>::New(*r, path).Unmanage();
				} catch (const std::exception& ex) {
					ScriptContextUtils().SetNativeException(ex);
					return nullptr;
				}
			}
			IModel* SceneWhole(ScriptModelScene* s) {
				try {
					return s->Whole();
				} catch (const std::exception& ex) {
					ScriptContextUtils().SetNativeException(ex);
					return nullptr;
				}
			}
			IModel* ScenePart(const std::string& part, ScriptModelScene* s) {
				try {
					return s->Part(part);
				} catch (const std::exception& ex) {
					ScriptContextUtils().SetNativeException(ex);
					return nullptr;
				}
			}
			IModel* SceneNativePart(const std::string& part, ScriptModelScene* s) {
				try {
					return s->NativePart(part);
				} catch (const std::exception& ex) {
					ScriptContextUtils().SetNativeException(ex);
					return nullptr;
				}
			}
			bool SceneHas(const std::string& part, ScriptModelScene* s) { return s->Has(part); }
			Matrix4 SceneTransform(const std::string& part, ScriptModelScene* s) {
				try {
					return s->Transform(part);
				} catch (const std::exception& ex) {
					ScriptContextUtils().SetNativeException(ex);
					return Matrix4::Identity();
				}
			}
			std::string ScenePath(ScriptModelScene* s) { return s->Path(); }
		} // namespace

		class ModelSceneRegistrar : public ScriptObjectRegistrar {
		public:
			ModelSceneRegistrar() : ScriptObjectRegistrar("ModelScene") {}
			void Register(ScriptManager* manager, Phase phase) override {
				asIScriptEngine* eng = manager->GetEngine();
				int r;
				eng->SetDefaultNamespace("spades");
				switch (phase) {
					case PhaseObjectType:
						r = eng->RegisterObjectType("ModelScene", 0, asOBJ_REF);
						manager->CheckError(r);
						r = eng->RegisterObjectBehaviour("ModelScene", asBEHAVE_ADDREF, "void f()",
						                                 asMETHOD(ScriptModelScene, AddRef),
						                                 asCALL_THISCALL);
						manager->CheckError(r);
						r = eng->RegisterObjectBehaviour("ModelScene", asBEHAVE_RELEASE, "void f()",
						                                 asMETHOD(ScriptModelScene, Release),
						                                 asCALL_THISCALL);
						manager->CheckError(r);
						break;
					case PhaseObjectMember:
						r = eng->RegisterObjectMethod("Renderer",
						                              "ModelScene@ RegisterScene(const string& in)",
						                              asFUNCTION(RegisterScene), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						r = eng->RegisterObjectMethod("ModelScene", "Model@ get_Whole()",
						                              asFUNCTION(SceneWhole), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						r = eng->RegisterObjectMethod("ModelScene", "Model@ Part(const string& in)",
						                              asFUNCTION(ScenePart), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						r = eng->RegisterObjectMethod("ModelScene", "Model@ NativePart(const string& in)",
						                              asFUNCTION(SceneNativePart), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						r = eng->RegisterObjectMethod("ModelScene", "bool Has(const string& in)",
						                              asFUNCTION(SceneHas), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						r = eng->RegisterObjectMethod("ModelScene",
						                              "Matrix4 Transform(const string& in)",
						                              asFUNCTION(SceneTransform), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						r = eng->RegisterObjectMethod("ModelScene", "string get_Path()",
						                              asFUNCTION(ScenePath), asCALL_CDECL_OBJLAST);
						manager->CheckError(r);
						break;
					default: break;
				}
			}
		};

		static ModelSceneRegistrar registrar;
	} // namespace client
} // namespace spades
