#pragma once

#include <atomic>
#include <cstdint>

// The launcher arms netplay by launching the game with SWOFTY_NETPLAY set (hooks installed, pass-through)
// and configures a room at runtime through the netplay command channel; the bridge hooks read this.
// Member number and port base share one atomic so a hook never sees one room's member with another's base.
namespace runtime::netplay_session {

constexpr int game_port = 21414;

inline std::atomic<uint32_t> packed{0};

inline bool valid(int self, int base)
{
    return self >= 1 && self <= 254 && base > 0 && base + 254 <= 65535 &&
           !(game_port > base && game_port <= base + 254);
}

inline bool snapshot(int &self, int &base)
{
    uint32_t v = packed.load(std::memory_order_acquire);
    self = static_cast<int>(v >> 16);
    base = static_cast<int>(v & 0xffff);
    return valid(self, base);
}

inline bool configured()
{
    int self, base;
    return snapshot(self, base);
}

inline bool configure(int self, int base)
{
    if (!valid(self, base))
        return false;
    packed.store(static_cast<uint32_t>(self) << 16 | static_cast<uint32_t>(base), std::memory_order_release);
    return true;
}

inline void clear()
{
    packed.store(0, std::memory_order_release);
}

}
