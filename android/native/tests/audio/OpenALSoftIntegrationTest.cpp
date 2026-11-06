#include <Audio/ALFuncs.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <iostream>

int main() {
    // The null backend exercises real OpenAL Soft without audio hardware.
    setenv("ALSOFT_DRIVERS", "null", 1);
    al::Link();
    ALCdevice* device = al::qalcOpenDevice(nullptr);
    assert(device != nullptr);
    ALCcontext* context = al::qalcCreateContext(device, nullptr);
    assert(context != nullptr && al::qalcMakeContextCurrent(context));
    al::InitEAX();
    ALuint effect = 0;
    al::qalGenEffects(1, &effect);
    assert(effect != 0 && al::qalGetError() == AL_NO_ERROR);
    al::qalDeleteEffects(1, &effect);
    assert(al::qalGetError() == AL_NO_ERROR);
    al::qalcMakeContextCurrent(nullptr);
    al::qalcDestroyContext(context);
    al::qalcCloseDevice(device);
    std::cout << "OpenAL Soft EFX initialization succeeded on null backend\n";
}
