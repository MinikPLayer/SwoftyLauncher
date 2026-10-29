#include <stdint.h>
__declspec(dllexport) const uint32_t SwoftyAddonApi = 8;
__declspec(dllexport) int SwoftyAddonInit(const void *api) { return api != 0; }
__declspec(dllexport) int SwoftyAddonRegisterX(const void *api) { return api != 0; }
