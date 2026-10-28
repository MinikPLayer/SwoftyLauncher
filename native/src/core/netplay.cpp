#include "runtime.hpp"

#include <swofty/mixin.hpp>

namespace {

const runtime::Sig S_HOST_GATE{"Game_SessionCreate_HostGate",
                               "48 8B 4C 24 30 83 E1 01 48 39 C1 0F 85 ?? ?? ?? ??", 0x30ae4a2,
                               runtime::Sig::addr, 0, 0xB, 0};

const runtime::Sig S_ENCRYPT_SELECT{"Net_Socket_EncryptionSelect",
                                    "48 85 C0 0F 84 ?? ?? ?? ?? 44 0F B6 7F 58", 0x405fa6d,
                                    runtime::Sig::addr, 0, 9, 0};

// Lobby-enter privilege re-check. While a session is active (session object +0x1a8 set, i.e. the
// lobby has been entered), the session-update tick (base+0x30e0560) re-queries the same privilege
// provider as the host gate above (session+0x4a8, vtbl+0x130); offline it answers "not granted",
// so the tick hands reason 10 to the session error setter (base+0x30670c0), which surfaces as
// network_error_kicked_for_cheating and leaves the lobby. The anchored je is the provider-granted
// skip; making it an unconditional jmp turns the re-check off while the provider query itself, the
// remote-kick reasons and the content reconciliation stay untouched.
const runtime::Sig S_LOBBY_KICK{"Net_SessionTick_KickForCheating",
                                 "FF 90 30 01 00 00 0F B6 84 24 E0 02 00 00 48 8B 8C 24 E8 02 00 00"
                                 " 83 E1 01 48 39 C1 74 ??",
                                 0x30e6122, runtime::Sig::addr, 0, 0x1C, 0};

// Local Multiplayer list. FeTile_Build_List (base+0x173df80) builds menu_kind 2 as Splitscreen
// (base+0x1747f90, button_1) and then an inline LAN tile (id "lan", item vtable base+0x5704740,
// button_2) that the shared tail pushes into the item vector. The anchor is the instruction after the
// Splitscreen call; a jmp there to the tail the adder-only menus jump to (base+0x1745016, after the
// temporaries' cleanup, at the item init/populate loop) leaves Splitscreen as the only entry. No
// branch lands in the 9 replaced bytes. The LAN tile's getAction was what switched the game to LAN
// mode on the way to the LAN dialog, so netplay commands do that switch themselves.
const runtime::Sig S_LAN_TILE{"FeTile_LocalMp_LanTile",
                              "4C 8D 05 ?? ?? ?? ?? 4C 89 F1 B2 01 E8 ?? ?? ?? ?? 48 C7 44 24 38 00 00 00 00",
                              0x173e7a7, runtime::Sig::addr, 0, 0x11, 0};
const runtime::Sig S_TILE_TAIL{"FeTile_Build_List_ItemsTail", "4C 89 F1 E8 ?? ?? ?? ?? E9 ?? ?? ?? ?? E8 ?? ?? ?? ?? B2 01",
                               0x1745016, runtime::Sig::rel32, 8, 9, 5};

bool patch(uintptr_t at, const uint8_t *bytes, size_t n)
{
    if (!at)
        return false;
    DWORD old;
    auto p = reinterpret_cast<void *>(at);
    if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(p, bytes, n);
    VirtualProtect(p, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, n);
    return true;
}

// The LAN Game entry is hidden for everyone; the session patches only go in when the launcher arms the
// game for a player with multiplayer access (SWOFTY_NETPLAY set), so other games run as before.
bool armed()
{
    char v[256] = {};
    DWORD n = GetEnvironmentVariableA("SWOFTY_NETPLAY", v, sizeof v);
    return n > 0 && n < sizeof v;
}

void session_patches()
{
    static const uint8_t open_gate[] = {0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    static const uint8_t force_null_ssl[] = {0x45, 0x31, 0xFF, 0x90, 0x90};
    static const uint8_t skip_lobby_kick[] = {0xEB};

    bool gate = patch(runtime::find(S_HOST_GATE, "netplay", "netplay"), open_gate, sizeof open_gate);
    swofty::log("[netplay] host-session gate %s", gate ? "opened" : "not patched (signature did not resolve)");

    bool enc = patch(runtime::find(S_ENCRYPT_SELECT, "netplay", "netplay"), force_null_ssl, sizeof force_null_ssl);
    swofty::log("[netplay] transport encryption %s",
                enc ? "forced to the null-SSL provider" : "left unchanged (signature did not resolve)");

    bool lobby = patch(runtime::find(S_LOBBY_KICK, "netplay", "netplay"), skip_lobby_kick, sizeof skip_lobby_kick);
    swofty::log("[netplay] lobby privilege re-check %s",
                lobby ? "neutralised (no kicked-for-cheating at lobby enter)"
                      : "left unchanged (signature did not resolve)");
}

void setup()
{
    uintptr_t lan = runtime::find(S_LAN_TILE, "core", "core"), tail = runtime::find(S_TILE_TAIL, "core", "core");
    bool hidden = false;
    if (lan && tail && tail > lan && tail - lan < 0x10000) {
        uint8_t skip_lan[9] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90, 0x90};
        int32_t rel = int32_t(tail - (lan + 5));
        memcpy(skip_lan + 1, &rel, sizeof rel);
        hidden = patch(lan, skip_lan, sizeof skip_lan);
    }
    swofty::log("[core] LAN Game entry in Local Multiplayer %s",
                hidden ? "hidden (Splitscreen only)" : "left in place (signature did not resolve)");

    if (armed())
        session_patches();

    runtime::netplay_bridge_setup();
}

}

namespace runtime {

void netplay_setup()
{
    setup();
}

}
