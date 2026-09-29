#include "Base/Base.as"
#include "Skin/Skin.as"

// Editor tool interfaces and enums (required before tool implementations)
#include "Editor/Tools/EditorTool.as"

// Auto-include every editor tool script; drop a new tool .as in this folder
// and it is compiled into the "Client" module and discovered automatically (see
// RegisterScriptTools in ScriptEditorTool.cpp).
#include "Editor/Tools/*.as"
