#include "Base/Base.as"
#include "Skin/Skin.as"

// KV6 editor interfaces and enums (required before tool implementations)
#include "Gui/KV6Editor/Tools/EditorTool.as"

// Auto-include every KV6 editor tool script; drop a new tool .as in this folder
// and it is compiled into the "Client" module and discovered automatically (see
// RegisterScriptTools in KV6ScriptTool.cpp).
#include "Gui/KV6Editor/Tools/*.as"
