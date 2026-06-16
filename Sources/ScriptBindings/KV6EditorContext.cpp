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
#include <vector>

#include <Core/Math.h>
#include <Gui/KV6EditorContext.h>

namespace spades {
	namespace {
		// Convert an AngelScript array<IntVector3> into a std::vector for the C++
		// editor API. The cell-list edits (fill/erase/select) all take one of these.
		std::vector<IntVector3> ToCells(CScriptArray* arr) {
			std::vector<IntVector3> out;
			if (arr == nullptr)
				return out;
			asUINT n = arr->GetSize();
			out.reserve(n);
			for (asUINT i = 0; i < n; i++)
				out.push_back(*reinterpret_cast<IntVector3*>(arr->At(i)));
			return out;
		}

		void Ctx_FillCells(gui::IEditorContext* c, CScriptArray* arr, uint32_t color) {
			c->FillCells(ToCells(arr), color);
		}
		void Ctx_EraseCells(gui::IEditorContext* c, CScriptArray* arr) {
			c->EraseCells(ToCells(arr));
		}
		void Ctx_PaintCells(gui::IEditorContext* c, CScriptArray* arr, uint32_t color) {
			c->PaintCells(ToCells(arr), color);
		}
		void Ctx_SelectCells(gui::IEditorContext* c, CScriptArray* arr) {
			c->SelectCells(ToCells(arr));
		}
		void Ctx_DeselectCells(gui::IEditorContext* c, CScriptArray* arr) {
			c->DeselectCells(ToCells(arr));
		}
		void Ctx_ApplyCells(gui::IEditorContext* c, CScriptArray* arr, bool secondary) {
			c->ApplyCells(ToCells(arr), secondary);
		}
	} // namespace

	// Exposes the editor's tool seam (`gui::IEditorContext`) to scripts as a
	// non-counted reference type. The editor owns the object and outlives every
	// script call, so no addref/release is needed.
	class EditorContextRegistrar : public ScriptObjectRegistrar {
	public:
		EditorContextRegistrar() : ScriptObjectRegistrar("EditorContext") {}

		void Register(ScriptManager* manager, Phase phase) override {
			asIScriptEngine* eng = manager->GetEngine();
			int r;
			eng->SetDefaultNamespace("spades");
			switch (phase) {
				case PhaseObjectType:
					r = eng->RegisterObjectType("EditorContext", 0, asOBJ_REF | asOBJ_NOCOUNT);
					manager->CheckError(r);
					break;
				case PhaseObjectMember: {
					// --- picking ---
					r = eng->RegisterObjectMethod("EditorContext", "void DoPick()",
					                              asMETHOD(gui::IEditorContext, DoPick),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext", "bool HasPick()",
					                              asMETHOD(gui::IEditorContext, HasPick),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext", "IntVector3 PickSolid()",
					                              asMETHOD(gui::IEditorContext, PickSolid),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext", "IntVector3 PickPlace()",
					                              asMETHOD(gui::IEditorContext, PickPlace),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext", "Vector3 ViewDir()",
					                              asMETHOD(gui::IEditorContext, ViewDir),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext",
					  "bool RayPlaneCell(const Vector3&in, const Vector3&in, IntVector3 &out)",
					  asMETHOD(gui::IEditorContext, RayPlaneCell), asCALL_THISCALL);
					manager->CheckError(r);

					// --- document / colour ---
					r = eng->RegisterObjectMethod("EditorContext", "uint CurrentColor()",
					                              asMETHOD(gui::IEditorContext, CurrentColor),
					                              asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext", "void SetStatus(const string&in)",
					                              asMETHOD(gui::IEditorContext, SetStatus),
					                              asCALL_THISCALL);
					manager->CheckError(r);

					// --- overlay drawing ---
					r = eng->RegisterObjectMethod(
					  "EditorContext",
					  "void DrawLine3D(const Vector3&in, const Vector3&in, const Vector4&in)",
					  asMETHOD(gui::IEditorContext, DrawLine3D), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext", "void DrawCellOutline(int, int, int, const Vector4&in)",
					  asMETHOD(gui::IEditorContext, DrawCellOutline), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext", "void DrawCellOutlineMirrored(int, int, int, const Vector4&in)",
					  asMETHOD(gui::IEditorContext, DrawCellOutlineMirrored), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext",
					  "void DrawBoxOutline(const IntVector3&in, const IntVector3&in, const Vector4&in)",
					  asMETHOD(gui::IEditorContext, DrawBoxOutline), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext",
					  "void DrawBoxOutlineMirrored(const IntVector3&in, const IntVector3&in, const "
					  "Vector4&in)",
					  asMETHOD(gui::IEditorContext, DrawBoxOutlineMirrored), asCALL_THISCALL);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext", "void DrawSolidCube(const Vector3&in, float, const Vector4&in)",
					  asMETHOD(gui::IEditorContext, DrawSolidCube), asCALL_THISCALL);
					manager->CheckError(r);

					// --- cell-list edits (array<IntVector3> -> std::vector) ---
					r = eng->RegisterObjectMethod("EditorContext",
					                              "void FillCells(array<spades::IntVector3>@, uint)",
					                              asFUNCTION(Ctx_FillCells), asCALL_CDECL_OBJFIRST);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext",
					                              "void EraseCells(array<spades::IntVector3>@)",
					                              asFUNCTION(Ctx_EraseCells), asCALL_CDECL_OBJFIRST);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext",
					                              "void PaintCells(array<spades::IntVector3>@, uint)",
					                              asFUNCTION(Ctx_PaintCells), asCALL_CDECL_OBJFIRST);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod("EditorContext",
					                              "void SelectCells(array<spades::IntVector3>@)",
					                              asFUNCTION(Ctx_SelectCells), asCALL_CDECL_OBJFIRST);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext", "void DeselectCells(array<spades::IntVector3>@)",
					  asFUNCTION(Ctx_DeselectCells), asCALL_CDECL_OBJFIRST);
					manager->CheckError(r);
					r = eng->RegisterObjectMethod(
					  "EditorContext", "void ApplyCells(array<spades::IntVector3>@, bool)",
					  asFUNCTION(Ctx_ApplyCells), asCALL_CDECL_OBJFIRST);
					manager->CheckError(r);
					break;
				}
				default: break;
			}
		}
	};

	static EditorContextRegistrar registrar;
} // namespace spades
