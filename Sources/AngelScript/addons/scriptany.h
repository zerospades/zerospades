#ifndef SCRIPTANY_H
#define SCRIPTANY_H

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

class CScriptAny
{
public:
	// Constructors
	CScriptAny(asIScriptEngine *engine);
	CScriptAny(void *ref, int refTypeId, asIScriptEngine *engine);

	// Memory management
	int AddRef() const;
	int Release() const;

	// Copy the stored value from another any object
	CScriptAny &operator=(const CScriptAny&);
	int CopyFrom(const CScriptAny *other);

	// Store the value, either as variable type, integer number, or real number
	void Store(void *ref, int refTypeId);
	void Store(asINT64 &value);
	void Store(double &value);

	// Retrieve the stored value, either as variable type, integer number, or real number
	bool Retrieve(void *ref, int refTypeId) const;
	bool Retrieve(asINT64 &value) const;
	bool Retrieve(double &value) const;

	// Get the type id of the stored value
	int  GetTypeId() const;

	// GC methods
	int  GetRefCount();
	void SetFlag();
	bool GetFlag();
	void EnumReferences(asIScriptEngine *engine);
	void ReleaseAllHandles(asIScriptEngine *engine);

protected:
	virtual ~CScriptAny();
	void FreeObject();

	mutable int refCount;
	mutable bool gcFlag;
	asIScriptEngine *engine;

	// The structure for holding the values
    struct valueStruct
    {
        union
        {
            asINT64 valueInt;
            double  valueFlt;
            void   *valueObj;
        };
        int   typeId;
    };

	valueStruct value;
};

void RegisterScriptAny(asIScriptEngine *engine);
void RegisterScriptAny_Native(asIScriptEngine *engine);
void RegisterScriptAny_Generic(asIScriptEngine *engine);

END_AS_NAMESPACE

#endif
