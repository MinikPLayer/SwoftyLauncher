/* Stands in for swofty_core.dll in toolkit_test: the SDK reads the overlay version from this export. */
#include <stdint.h>
__declspec(dllexport) uint32_t SwoftyCoreOverlayVersion(void)
{
    return 3;
}
