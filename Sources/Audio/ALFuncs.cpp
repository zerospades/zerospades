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

#include "ALFuncs.h"
#include <Core/DynamicLibrary.h>
#include <Core/Exception.h>
#include <Core/Math.h>
#include <Core/Settings.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <libgen.h>
#include <climits>
#endif

#if defined(__APPLE__)
// Use embedded OpenAL Soft from the app bundle
DEFINE_SPADES_SETTING(s_alDriver, "@executable_path/../Frameworks/libopenal.1.dylib;/System/Library/Frameworks/OpenAL.framework/OpenAL");
#elif defined(WIN32)
DEFINE_SPADES_SETTING(s_alDriver, "OpenAL32.dll");
#else
DEFINE_SPADES_SETTING(s_alDriver, "libopenal.so.1;libopenal.so.0;libopenal.so");
#endif

DEFINE_SPADES_SETTING(s_alErrorFatal, "1");

namespace al {

	LPALENABLE qalEnable;
	LPALDISABLE qalDisable;
	LPALISENABLED qalIsEnabled;
	LPALGETSTRING qalGetString;
	LPALGETBOOLEANV qalGetBooleanv;
	LPALGETINTEGERV qalGetIntegerv;
	LPALGETFLOATV qalGetFloatv;
	LPALGETDOUBLEV qalGetDoublev;
	LPALGETBOOLEAN qalGetBoolean;
	LPALGETINTEGER qalGetInteger;
	LPALGETFLOAT qalGetFloat;
	LPALGETDOUBLE qalGetDouble;
	LPALGETERROR qalGetError;
	LPALISEXTENSIONPRESENT qalIsExtensionPresent;
	LPALGETPROCADDRESS qalGetProcAddress = NULL;
	LPALGETENUMVALUE qalGetEnumValue;
	LPALLISTENERF qalListenerf;
	LPALLISTENER3F qalListener3f;
	LPALLISTENERFV qalListenerfv;
	LPALLISTENERI qalListeneri;
	LPALGETLISTENERF qalGetListenerf;
	LPALGETLISTENER3F qalGetListener3f;
	LPALGETLISTENERFV qalGetListenerfv;
	LPALGETLISTENERI qalGetListeneri;
	LPALGENSOURCES qalGenSources;
	LPALDELETESOURCES qalDeleteSources;
	LPALISSOURCE qalIsSource;
	LPALSOURCEF qalSourcef;
	LPALSOURCE3F qalSource3f;
	LPALSOURCEFV qalSourcefv;
	LPALSOURCEI qalSourcei;
	LPALSOURCE3I qalSource3i;
	LPALGETSOURCEF qalGetSourcef;
	LPALGETSOURCE3F qalGetSource3f;
	LPALGETSOURCEFV qalGetSourcefv;
	LPALGETSOURCEI qalGetSourcei;
	LPALSOURCEPLAYV qalSourcePlayv;
	LPALSOURCESTOPV qalSourceStopv;
	LPALSOURCEREWINDV qalSourceRewindv;
	LPALSOURCEPAUSEV qalSourcePausev;
	LPALSOURCEPLAY qalSourcePlay;
	LPALSOURCESTOP qalSourceStop;
	LPALSOURCEREWIND qalSourceRewind;
	LPALSOURCEPAUSE qalSourcePause;
	LPALSOURCEQUEUEBUFFERS qalSourceQueueBuffers;
	LPALSOURCEUNQUEUEBUFFERS qalSourceUnqueueBuffers;
	LPALGENBUFFERS qalGenBuffers;
	LPALDELETEBUFFERS qalDeleteBuffers;
	LPALISBUFFER qalIsBuffer;
	LPALBUFFERDATA qalBufferData;
	LPALGETBUFFERF qalGetBufferf;
	LPALGETBUFFERI qalGetBufferi;
	LPALDOPPLERFACTOR qalDopplerFactor;
	LPALDOPPLERVELOCITY qalDopplerVelocity;
	LPALDISTANCEMODEL qalDistanceModel;

	LPALGENEFFECTS qalGenEffects;
	LPALDELETEEFFECTS qalDeleteEffects;
	LPALISEFFECT qalIsEffect;
	LPALEFFECTI qalEffecti;
	LPALEFFECTIV qalEffectiv;
	LPALEFFECTF qalEffectf;
	LPALEFFECTFV qalEffectfv;
	LPALGETEFFECTI qalGetEffecti;
	LPALGETEFFECTIV qalGetEffectiv;
	LPALGETEFFECTF qalGetEffectf;
	LPALGETEFFECTFV qalGetEffectfv;
	LPALGENFILTERS qalGenFilters;
	LPALDELETEFILTERS qalDeleteFilters;
	LPALISFILTER qalIsFilter;
	LPALFILTERI qalFilteri;
	LPALFILTERIV qalFilteriv;
	LPALFILTERF qalFilterf;
	LPALFILTERFV qalFilterfv;
	LPALGETFILTERI qalGetFilteri;
	LPALGETFILTERIV qalGetFilteriv;
	LPALGETFILTERF qalGetFilterf;
	LPALGETFILTERFV qalGetFilterfv;
	LPALGENAUXILIARYEFFECTSLOTS qalGenAuxiliaryEffectSlots;
	LPALDELETEAUXILIARYEFFECTSLOTS qalDeleteAuxiliaryEffectSlots;
	LPALISAUXILIARYEFFECTSLOT qalIsAuxiliaryEffectSlot;
	LPALAUXILIARYEFFECTSLOTI qalAuxiliaryEffectSloti;
	LPALAUXILIARYEFFECTSLOTIV qalAuxiliaryEffectSlotiv;
	LPALAUXILIARYEFFECTSLOTF qalAuxiliaryEffectSlotf;
	LPALAUXILIARYEFFECTSLOTFV qalAuxiliaryEffectSlotfv;
	LPALGETAUXILIARYEFFECTSLOTI qalGetAuxiliaryEffectSloti;
	LPALGETAUXILIARYEFFECTSLOTIV qalGetAuxiliaryEffectSlotiv;
	LPALGETAUXILIARYEFFECTSLOTF qalGetAuxiliaryEffectSlotf;
	LPALGETAUXILIARYEFFECTSLOTFV qalGetAuxiliaryEffectSlotfv;

	// Mac OS X Extensions (only available with system OpenAL)
	// ALC_EXT_MAC_OSX
#if defined(__APPLE__) && !defined(OPENAL_SOFT)
	alcMacOSXRenderingQualityProcPtr qalcMacOSXRenderingQuality;
	alMacOSXRenderChannelCountProcPtr qalMacOSXRenderChannelCount;
	alcMacOSXMixerMaxiumumBussesProcPtr qalcMacOSXMixerMaxiumumBusses;
	alcMacOSXMixerOutputRateProcPtr qalcMacOSXMixerOutputRate;
	alcMacOSXGetRenderingQualityProcPtr qalcMacOSXGetRenderingQuality;
	alMacOSXGetRenderChannelCountProcPtr qalMacOSXGetRenderChannelCount;
	alcMacOSXGetMixerMaxiumumBussesProcPtr qalcMacOSXGetMixerMaxiumumBusses;
	alcMacOSXGetMixerOutputRateProcPtr qalcMacOSXGetMixerOutputRate;
#endif
	// ALC_EXT_ASA (only available with system OpenAL)
#if defined(__APPLE__) && !defined(OPENAL_SOFT)
	alcASAGetSourceProcPtr qalcASAGetSource;
	alcASASetSourceProcPtr qalcASASetSource;
	alcASAGetListenerProcPtr qalcASAGetListener;
	alcASASetListenerProcPtr qalcASASetListener;
#endif

	LPALCCREATECONTEXT qalcCreateContext;
	LPALCMAKECONTEXTCURRENT qalcMakeContextCurrent;
	LPALCPROCESSCONTEXT qalcProcessContext;
	LPALCSUSPENDCONTEXT qalcSuspendContext;
	LPALCDESTROYCONTEXT qalcDestroyContext;
	LPALCGETCURRENTCONTEXT qalcGetCurrentContext;
	LPALCGETCONTEXTSDEVICE qalcGetContextsDevice;
	LPALCOPENDEVICE qalcOpenDevice;
	LPALCCLOSEDEVICE qalcCloseDevice;
	LPALCGETERROR qalcGetError;
	LPALCISEXTENSIONPRESENT qalcIsExtensionPresent;
	LPALCGETPROCADDRESS qalcGetProcAddress;
	LPALCGETENUMVALUE qalcGetEnumValue;
	LPALCGETSTRING qalcGetString;
	LPALCGETINTEGERV qalcGetIntegerv;
	LPALCCAPTUREOPENDEVICE qalcCaptureOpenDevice;
	LPALCCAPTURECLOSEDEVICE qalcCaptureCloseDevice;
	LPALCCAPTURESTART qalcCaptureStart;
	LPALCCAPTURESTOP qalcCaptureStop;
	LPALCCAPTURESAMPLES qalcCaptureSamples;

#ifndef OPENAL_SOFT
	static spades::DynamicLibrary *alLibrary = NULL;
#endif

	static void *GPA(const char *str) {
		// The built-in OpenAL Soft does not exist as a separate shared library.
		// Resolve extensions from the active context before trying a dlopen.
		if (qalGetProcAddress) {
			void *v = qalGetProcAddress(str);
			if (v)
				return v;
		}
#ifdef OPENAL_SOFT
		SPRaise("OpenAL function not available: %s", str);
#else
		if (!alLibrary) {
			auto paths = spades::Split(s_alDriver, ";");
			std::string errors;
			for (const std::string &path : paths) {
				auto trimmedPath = spades::TrimSpaces(path);

				// Resolve @executable_path on macOS
				std::string resolvedPath = trimmedPath;
#ifdef __APPLE__
				if (trimmedPath.find("@executable_path") == 0) {
					char execPath[PATH_MAX];
					uint32_t size = sizeof(execPath);
					if (_NSGetExecutablePath(execPath, &size) == 0) {
						char *dir = dirname(execPath);
						resolvedPath = std::string(dir) + trimmedPath.substr(16); // Remove @executable_path
					}
				}
#endif

				try {
					alLibrary = new spades::DynamicLibrary(resolvedPath.c_str());
					if (alLibrary) {
						SPLog("'%s' loaded", resolvedPath.c_str());
						break;
					}
				} catch (const std::exception &ex) {
					errors += trimmedPath;
					errors += ":\n";
					errors += ex.what();
				}
			}
			if (!alLibrary) {
				SPRaise("Failed to load a OpenAL driver.\n%s", errors.c_str());
			}
		}

		return alLibrary->GetSymbol(str);
#endif
	}

#define L(name) *(void **)(&(q##name)) = GPA(#name)

	void InitEAX(void) {
		ALCdevice *pDevice = NULL;
		ALCcontext *pContext = NULL;

		pContext = qalcGetCurrentContext();
		pDevice = qalcGetContextsDevice(pContext);

		if (qalcIsExtensionPresent(pDevice, (ALCchar *)ALC_EXT_EFX_NAME)) {
			L(alGenEffects);
			L(alDeleteEffects);
			L(alIsEffect);
			L(alEffecti);
			L(alEffectiv);
			L(alEffectf);
			L(alEffectfv);
			L(alGetEffecti);
			L(alGetEffectiv);
			L(alGetEffectf);
			L(alGetEffectfv);
			L(alGenFilters);
			L(alDeleteFilters);
			L(alIsFilter);
			L(alFilteri);
			L(alFilteriv);
			L(alFilterf);
			L(alFilterfv);
			L(alGetFilteri);
			L(alGetFilteriv);
			L(alGetFilterf);
			L(alGetFilterfv);
			L(alGenAuxiliaryEffectSlots);
			L(alDeleteAuxiliaryEffectSlots);
			L(alIsAuxiliaryEffectSlot);
			L(alAuxiliaryEffectSloti);
			L(alAuxiliaryEffectSlotiv);
			L(alAuxiliaryEffectSlotf);
			L(alAuxiliaryEffectSlotfv);
			L(alGetAuxiliaryEffectSloti);
			L(alGetAuxiliaryEffectSlotiv);
			L(alGetAuxiliaryEffectSlotf);
			L(alGetAuxiliaryEffectSlotfv);
		} else {
			SPRaise("Extension not found: '%s'", ALC_EXT_EFX_NAME);
		}
	}

	void Link(void) {
#ifdef OPENAL_SOFT
		SPLog("Using OpenAL Soft - direct linking, initializing function pointers.");
		// Directly assign function pointers to OpenAL Soft functions
		qalEnable = alEnable;
		qalDisable = alDisable;
		qalIsEnabled = alIsEnabled;
		qalGetString = alGetString;
		qalGetBooleanv = alGetBooleanv;
		qalGetIntegerv = alGetIntegerv;
		qalGetFloatv = alGetFloatv;
		qalGetDoublev = alGetDoublev;
		qalGetBoolean = alGetBoolean;
		qalGetInteger = alGetInteger;
		qalGetFloat = alGetFloat;
		qalGetDouble = alGetDouble;
		qalGetError = alGetError;
		qalIsExtensionPresent = alIsExtensionPresent;
		qalGetProcAddress = alGetProcAddress;
		qalGetEnumValue = alGetEnumValue;
		qalListenerf = alListenerf;
		qalListener3f = alListener3f;
		qalListenerfv = alListenerfv;
		qalListeneri = alListeneri;
		qalGetListenerf = alGetListenerf;
		qalGetListener3f = alGetListener3f;
		qalGetListenerfv = alGetListenerfv;
		qalGetListeneri = alGetListeneri;
		qalGenSources = alGenSources;
		qalDeleteSources = alDeleteSources;
		qalIsSource = alIsSource;
		qalSourcef = alSourcef;
		qalSource3f = alSource3f;
		qalSourcefv = alSourcefv;
		qalSourcei = alSourcei;
		qalSource3i = alSource3i;
		qalGetSourcef = alGetSourcef;
		qalGetSource3f = alGetSource3f;
		qalGetSourcefv = alGetSourcefv;
		qalGetSourcei = alGetSourcei;
		qalSourcePlayv = alSourcePlayv;
		qalSourceStopv = alSourceStopv;
		qalSourceRewindv = alSourceRewindv;
		qalSourcePausev = alSourcePausev;
		qalSourcePlay = alSourcePlay;
		qalSourceStop = alSourceStop;
		qalSourceRewind = alSourceRewind;
		qalSourcePause = alSourcePause;
		qalSourceQueueBuffers = alSourceQueueBuffers;
		qalSourceUnqueueBuffers = alSourceUnqueueBuffers;
		qalGenBuffers = alGenBuffers;
		qalDeleteBuffers = alDeleteBuffers;
		qalIsBuffer = alIsBuffer;
		qalBufferData = alBufferData;
		qalGetBufferf = alGetBufferf;
		qalGetBufferi = alGetBufferi;
		qalDopplerFactor = alDopplerFactor;
		qalDopplerVelocity = alDopplerVelocity;
		qalDistanceModel = alDistanceModel;

		qalcCreateContext = alcCreateContext;
		qalcMakeContextCurrent = alcMakeContextCurrent;
		qalcProcessContext = alcProcessContext;
		qalcSuspendContext = alcSuspendContext;
		qalcDestroyContext = alcDestroyContext;
		qalcGetCurrentContext = alcGetCurrentContext;
		qalcGetContextsDevice = alcGetContextsDevice;
		qalcOpenDevice = alcOpenDevice;
		qalcCloseDevice = alcCloseDevice;
		qalcGetError = alcGetError;
		qalcIsExtensionPresent = alcIsExtensionPresent;
		qalcGetProcAddress = alcGetProcAddress;
		qalcGetEnumValue = alcGetEnumValue;
		qalcGetString = alcGetString;
		qalcGetIntegerv = alcGetIntegerv;
		return;
#else
		SPLog("Linking with OpenAL library.");
		L(alEnable);
		L(alDisable);
		L(alIsEnabled);
		L(alGetString);
		L(alGetBooleanv);
		L(alGetIntegerv);
		L(alGetFloatv);
		L(alGetDoublev);
		L(alGetBoolean);
		L(alGetInteger);
		L(alGetFloat);
		L(alGetDouble);
		L(alGetError);
		L(alIsExtensionPresent);
		L(alGetProcAddress);
		L(alGetEnumValue);
		L(alListenerf);
		L(alListener3f);
		L(alListenerfv);
		L(alListeneri);
		L(alGetListenerf);
		L(alGetListener3f);
		L(alGetListenerfv);
		L(alGetListeneri);
		L(alGenSources);
		L(alDeleteSources);
		L(alIsSource);
		L(alSourcef);
		L(alSource3f);
		L(alSourcefv);
		L(alSourcei);
		L(alSource3i);
		L(alGetSourcef);
		L(alGetSource3f);
		L(alGetSourcefv);
		L(alGetSourcei);
		L(alSourcePlayv);
		L(alSourceStopv);
		L(alSourceRewindv);
		L(alSourcePausev);
		L(alSourcePlay);
		L(alSourceStop);
		L(alSourceRewind);
		L(alSourcePause);
		L(alSourceQueueBuffers);
		L(alSourceUnqueueBuffers);
		L(alGenBuffers);
		L(alDeleteBuffers);
		L(alIsBuffer);
		L(alBufferData);
		L(alGetBufferf);
		L(alGetBufferi);
		L(alDopplerFactor);
		L(alDopplerVelocity);
		L(alDistanceModel);

		L(alcCreateContext);
		L(alcMakeContextCurrent);
		L(alcProcessContext);
		L(alcSuspendContext);
		L(alcDestroyContext);
		L(alcGetCurrentContext);
		L(alcGetContextsDevice);
		L(alcOpenDevice);
		L(alcCloseDevice);
		L(alcGetError);
		L(alcIsExtensionPresent);
		L(alcGetProcAddress);
		L(alcGetEnumValue);
		L(alcGetString);
		L(alcGetIntegerv);
#endif // !OPENAL_SOFT
	}

	const char *DescribeError(ALenum e) {
		switch (e) {
			case AL_NO_ERROR: return "No error";
			case AL_INVALID_NAME: return "Invalid name";
			case AL_INVALID_ENUM: return "Invalid enumerator";
			case AL_INVALID_VALUE: return "Invalid value";
			case AL_INVALID_OPERATION: return "Invalid operation";
			case AL_OUT_OF_MEMORY: return "Out of memory";
			default: return "Unknown error";
		}
	}

	void CheckError(void) {
		ALenum e;
		e = qalGetError();
		if (e != AL_NO_ERROR) {
			if (s_alErrorFatal)
				SPRaise("OpenAL error %d: %s", (int)e, DescribeError(e));
			else
				SPLog("OpenAL error %d: %s", (int)e, DescribeError(e));
		}
	}

	void CheckError(const char *source, const char *fun, int line) {
		ALenum e;
		e = qalGetError();
		if (e != AL_NO_ERROR) {
			if (s_alErrorFatal)
				SPRaise("[%s:%d] : %s : OpenAL error %d: %s", source, line, fun, (int)e,
				        DescribeError(e));
			else
				SPLog("[%s:%d] : %s : OpenAL error %d: %s", source, line, fun, (int)e,
				      DescribeError(e));
		}
	}
}
