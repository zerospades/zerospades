#ifndef SCRIPTWEAKREF_H
#define SCRIPTWEAKREF_H

// The CScriptWeakRef class was originally implemented by vroad in March 2013

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

class CScriptWeakRef
{
public:
	// Constructors
	CScriptWeakRef(asITypeInfo *type);
	CScriptWeakRef(const CScriptWeakRef &other);
	CScriptWeakRef(void *ref, asITypeInfo *type);

	~CScriptWeakRef();

	// Copy the stored value from another weakref object
	CScriptWeakRef &operator=(const CScriptWeakRef &other);

	// Compare equalness
	bool operator==(const CScriptWeakRef &o) const;
	bool operator!=(const CScriptWeakRef &o) const;

	// Sets a new reference
	CScriptWeakRef &Set(void *newRef);

	// Returns the object if it is still alive
	// This will increment the refCount of the returned object
	void *Get() const;

	// Returns true if the contained reference is the same
	bool Equals(void *ref) const;

	// Returns the type of the reference held
	asITypeInfo *GetRefType() const;

protected:
	// These functions need to have access to protected
	// members in order to call them from the script engine
	friend void RegisterScriptWeakRef_Native(asIScriptEngine *engine);

	void                  *m_ref;
	asITypeInfo           *m_type;
	asILockableSharedBool *m_weakRefFlag;
};

void RegisterScriptWeakRef(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
