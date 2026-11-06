#include <Audio/ALFuncs.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstring>
#include <exception>
#include <iostream>

static int requested = 0;
static const char* missing = nullptr;
static void AL_APIENTRY stubEffect() {}
static void* AL_APIENTRY getProc(const ALchar* name) noexcept {
    ++requested;
    if (missing && std::strcmp(name, missing) == 0) return nullptr;
    return reinterpret_cast<void*>(&stubEffect);
}
static ALCcontext* ALC_APIENTRY context() noexcept { return reinterpret_cast<ALCcontext*>(1); }
static ALCdevice* ALC_APIENTRY device(ALCcontext*) noexcept { return reinterpret_cast<ALCdevice*>(1); }
static ALCboolean ALC_APIENTRY extension(ALCdevice*, const ALCchar*) noexcept { return ALC_TRUE; }

int main() {
    al::qalGetProcAddress = getProc;
    al::qalcGetCurrentContext = context;
    al::qalcGetContextsDevice = device;
    al::qalcIsExtensionPresent = extension;
    // The test executable contains no DynamicLibrary implementation: this
    // succeeds only if the built-in EFX lookup does not try to dlopen OpenAL.
    al::InitEAX();
    assert(requested > 20 && al::qalGenEffects && al::qalGenAuxiliaryEffectSlots);
    missing = "alGenEffects";
    try {
        al::InitEAX();
        assert(false && "missing EFX entry point should throw");
    } catch (const std::exception& e) {
        assert(std::strstr(e.what(), "alGenEffects") != nullptr);
    }
    al::qalGetProcAddress = nullptr;
    try {
        al::InitEAX();
        assert(false && "missing OpenAL resolver should throw");
    } catch (const std::exception& e) {
        assert(std::strstr(e.what(), "alGenEffects") != nullptr);
    }
    std::cout << "Built-in OpenAL extension resolution checks passed\n";
}
