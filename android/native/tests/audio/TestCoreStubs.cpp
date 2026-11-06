// Minimal engine services for exercising the real loader and audio code in isolation.
#include <Core/Debug.h>
#include <Core/Exception.h>
#include <Core/Settings.h>
#include <Core/Math.h>
#include <cstdarg>
#include <cstdlib>
#include <cstdio>

namespace spades {
    reflection::BacktraceEntryAdder::BacktraceEntryAdder(const reflection::BacktraceEntry&) : bt(nullptr) {}
    reflection::BacktraceEntryAdder::~BacktraceEntryAdder() = default;
    Exception::Exception(const char* format, ...) {
        char buffer[2048]; va_list args; va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
        shortMessage = message = buffer;
    }
    Exception::Exception(const char*, int, const char* format, ...) {
        char buffer[2048]; va_list args; va_start(args, format);
        vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
        shortMessage = message = buffer;
    }
    Exception::~Exception() throw() = default;
    const char* Exception::what() const throw() { return message.c_str(); }
    void LogMessage(const char*, int, const char*, ...) {}
    Settings::ItemHandle::ItemHandle(const std::string&, const SettingItemDescriptor*) : item(nullptr) {}
    Settings::ItemHandle::operator bool() { return true; }
    Settings::ItemHandle::operator std::string() {
        const char* path = std::getenv("ZS_TEST_OPENAL_PATH");
        return path ? path : "";
    }
    std::vector<std::string> Split(const std::string& s, const std::string&) { return {s}; }
    std::string TrimSpaces(const std::string& s) { return s; }
}
