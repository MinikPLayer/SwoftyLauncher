/* SPDX-License-Identifier: MIT */
/* swofty_core.dll for the recorder: the three core exports an addon looks for before it uses the registry,
   hooks and services tables, answering with the recorder's tables. */
#include <swofty_api.h>

static const SwoftyRegistry *registry_table;
static const SwoftyHooks *hooks_table;
static const SwoftyServices *services_table;

SWOFTY_EXPORT const SwoftyRegistry *SwoftyCoreRegistry(void) { return registry_table; }
SWOFTY_EXPORT const SwoftyHooks *SwoftyCoreHooks(void) { return hooks_table; }
SWOFTY_EXPORT const SwoftyServices *SwoftyCoreServices(void) { return services_table; }

SWOFTY_EXPORT void SwoftyRecorderTables(const SwoftyRegistry *r, const SwoftyHooks *h, const SwoftyServices *s)
{
    registry_table = r;
    hooks_table = h;
    services_table = s;
}
