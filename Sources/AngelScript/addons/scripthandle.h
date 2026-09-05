#ifndef SCRIPTHANDLE_H
#define SCRIPTHANDLE_H

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

class CScriptHandle
{
public:
	// Constructors
	CScriptHandle();
	CScriptHandle(const CScriptHandle &other);
	CScriptHandle(void *ref, asITypeInfo *type);
	~CScriptHandle();

	// Copy the stored value from another any object
	CScriptHandle &operator=(const CScriptHandle &other);

	// Set the reference
	void Set(void *ref, asITypeInfo *type);

	// Compare equalness
	bool operator==(const CScriptHandle &o) const;
	bool operator!=(const CScriptHandle &o) const;
	bool Equals(void *ref, int typeId) const;

	// Dynamic cast to desired handle type
	void Cast(void **outRef, int typeId);

	// Returns the type of the reference held
	asITypeInfo *GetType() const;
	int          GetTypeId() const;

	// Get the reference
	void *GetRef();

	// GC callback
	void EnumReferences(asIScriptEngine *engine);
	void ReleaseReferences(asIScriptEngine *engine);

protected:
	// These functions need to have access to protected
	// members in order to call them from the script engine
	friend void Construct(CScriptHandle *self, void *ref, int typeId);
	friend void RegisterScriptHandle_Native(asIScriptEngine *engine);
	friend void CScriptHandle_AssignVar_Generic(asIScriptGeneric *gen);

	void ReleaseHandle();
	void AddRefHandle();

	// These shouldn't be called directly by the
	// application as they requires an active context
	CScriptHandle(void *ref, int typeId);
	CScriptHandle &Assign(void *ref, int typeId);

	void        *m_ref;
	asITypeInfo *m_type;
};

void RegisterScriptHandle(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
