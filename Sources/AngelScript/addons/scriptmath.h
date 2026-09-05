#ifndef SCRIPTMATH_H
#define SCRIPTMATH_H

#ifndef ANGELSCRIPT_H
// Avoid having to inform include path if header is already include before
#include <angelscript.h>
#endif

// Calling convention support for AS_MAX_PORTABILITY on ARM64 Windows
// Defined outside include guard to ensure macros are always available
#ifdef AS_MAX_PORTABILITY
	#define ADDON_CC_CDECL asCALL_GENERIC
	#define ADDON_CC_THISCALL asCALL_GENERIC
	#define ADDON_CC_CDECL_OBJLAST asCALL_GENERIC
	#define ADDON_CC_CDECL_OBJFIRST asCALL_GENERIC
	#define ADDON_CC_THISCALL_OBJLAST asCALL_GENERIC
	#define ADDON_CC_THISCALL_OBJFIRST asCALL_GENERIC
#else
	#define ADDON_CC_CDECL asCALL_CDECL
	#define ADDON_CC_THISCALL asCALL_THISCALL
	#define ADDON_CC_CDECL_OBJLAST asCALL_CDECL_OBJLAST
	#define ADDON_CC_CDECL_OBJFIRST asCALL_CDECL_OBJFIRST
	#define ADDON_CC_THISCALL_OBJLAST asCALL_THISCALL_OBJLAST
	#define ADDON_CC_THISCALL_OBJFIRST asCALL_THISCALL_OBJFIRST
#endif


BEGIN_AS_NAMESPACE

// This function will determine the configuration of the engine
// and use one of the two functions below to register the math functions
void RegisterScriptMath(asIScriptEngine *engine);

// Call this function to register the math functions
// using native calling conventions
void RegisterScriptMath_Native(asIScriptEngine *engine);

// Use this one instead if native calling conventions
// are not supported on the target platform
void RegisterScriptMath_Generic(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
