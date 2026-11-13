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

#include "IViewToolSkin.h"
#include "ScriptManager.h"
#include <Core/Debug.h>

namespace spades {
	namespace client {
		ScriptIViewToolSkin::ScriptIViewToolSkin(asIScriptObject* obj) : obj(obj) {}

		void ScriptIViewToolSkin::SetEyeMatrix(Matrix4 m) {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin", "void set_EyeMatrix(Matrix4)");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			r = ctx->SetArgObject(0, &m);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
		}

		void ScriptIViewToolSkin::SetSwing(spades::Vector3 v) {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin", "void set_Swing(Vector3)");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			r = ctx->SetArgObject(0, &v);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
		}

		Vector3 ScriptIViewToolSkin::GetLeftHandPosition() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin", "Vector3 get_LeftHandPosition()");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
			return *reinterpret_cast<Vector3*>(ctx->GetReturnObject());
		}

		Vector3 ScriptIViewToolSkin::GetRightHandPosition() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin", "Vector3 get_RightHandPosition()");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
			return *reinterpret_cast<Vector3*>(ctx->GetReturnObject());
		}

		void ScriptIViewToolSkin::Draw2D() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin", "void Draw2D()");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
		}

		ScriptIViewToolSkin2::ScriptIViewToolSkin2(asIScriptObject* obj) : obj(obj) {}

		bool ScriptIViewToolSkin2::ImplementsInterface() {
			return obj && obj->GetObjectType()->Implements(
			                obj->GetEngine()->GetTypeInfoByName("IViewToolSkin2"));
		}

		void ScriptIViewToolSkin2::SetLeftHanded(bool v) {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin2", "void set_LeftHanded(bool)");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			r = ctx->SetArgByte(0, v ? 1 : 0);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
		}

		Vector3 ScriptIViewToolSkin2::GetCaseEjectDirection() {
			SPADES_MARK_FUNCTION_DEBUG();
			static ScriptFunction func("IViewToolSkin2", "Vector3 get_CaseEjectDirection()");
			ScriptContextHandle ctx = func.Prepare();
			int r;
			r = ctx->SetObject((void*)obj);
			ScriptManager::CheckError(r);
			ctx.ExecuteChecked();
			return *reinterpret_cast<Vector3*>(ctx->GetReturnObject());
		}

		class IViewToolSkinRegistrar : public ScriptObjectRegistrar {
		public:
			IViewToolSkinRegistrar() : ScriptObjectRegistrar("IViewToolSkin") {}
			virtual void Register(ScriptManager* manager, Phase phase) {
				asIScriptEngine* eng = manager->GetEngine();
				int r;
				eng->SetDefaultNamespace("spades");
				switch (phase) {
					case PhaseObjectType:
						r = eng->RegisterInterface("IViewToolSkin");
						manager->CheckError(r);
						r = eng->RegisterInterface("IViewToolSkin2");
						manager->CheckError(r);
						break;
					case PhaseObjectMember:
						r = eng->RegisterInterfaceMethod("IViewToolSkin", "void set_EyeMatrix(Matrix4)");
						manager->CheckError(r);
						r = eng->RegisterInterfaceMethod("IViewToolSkin", "void set_Swing(Vector3)");
						manager->CheckError(r);
						r = eng->RegisterInterfaceMethod("IViewToolSkin", "Vector3 get_LeftHandPosition()");
						manager->CheckError(r);
						r = eng->RegisterInterfaceMethod("IViewToolSkin", "Vector3 get_RightHandPosition()");
						manager->CheckError(r);
						r = eng->RegisterInterfaceMethod("IViewToolSkin", "void Draw2D()");
						manager->CheckError(r);
						r = eng->RegisterInterfaceMethod("IViewToolSkin2", "void set_LeftHanded(bool)");
						manager->CheckError(r);
						r = eng->RegisterInterfaceMethod("IViewToolSkin2",
						                                 "Vector3 get_CaseEjectDirection()");
						manager->CheckError(r);
						break;
					default: break;
				}
			}
		};

		static IViewToolSkinRegistrar registrar;
	} // namespace client
} // namespace spades