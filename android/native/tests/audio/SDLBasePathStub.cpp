#include <cstdlib>
extern "C" char* SDL_GetBasePath() { return nullptr; }
extern "C" void SDL_free(void* p) { free(p); }
