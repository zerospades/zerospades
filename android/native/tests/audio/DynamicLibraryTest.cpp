#include <Core/DynamicLibrary.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>

static bool provideBase = false;
static int frees = 0;
extern "C" char* SDL_GetBasePath() { return provideBase ? strdup("/tmp") : nullptr; }
extern "C" void SDL_free(void* p) { ++frees; free(p); }

int main() {
    for (bool base : {false, true}) {
        provideBase = base;
        try {
            spades::DynamicLibrary missing("no-such-zerospades-library-78432.so");
            assert(false && "missing library should throw");
        } catch (const std::exception& e) {
            assert(std::strstr(e.what(), "Failed to dlload") != nullptr);
        }
    }
    assert(frees == 1); // SDL base paths are owned allocations.
    spades::DynamicLibrary libc("libc.so.6");
    assert(libc.GetSymbol("strlen") != nullptr);
    try {
        libc.GetSymbol("no_such_zerospades_symbol_78432");
        assert(false && "missing symbol should throw");
    } catch (const std::exception& e) {
        assert(std::strstr(e.what(), "Failed to find symbol") != nullptr);
    }
    std::cout << "DynamicLibrary null-base and missing-symbol checks passed\n";
}
