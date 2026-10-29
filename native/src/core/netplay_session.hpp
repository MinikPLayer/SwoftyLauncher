#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>

// The launcher arms netplay by launching the game with SWOFTY_NETPLAY set (hooks installed, pass-through)
// and configures a room at runtime through the netplay command channel; the bridge hooks read this.
// Member number and port base share one atomic so a hook never sees one room's member with another's base.
// The room may carry this player's signed-in name for the game's own driver; it applies only while the room
// is configured, and an empty name keeps the game's own.
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

inline std::mutex name_lock;
inline std::string name_now;
inline std::atomic<uint32_t> name_generation{0};

inline void set_name(const std::string &name)
{
    std::lock_guard<std::mutex> g(name_lock);
    if (name == name_now)
        return;
    name_now = name;
    name_generation.fetch_add(1, std::memory_order_release);
}

inline std::string driver_name(uint32_t *generation = nullptr)
{
    if (!configured())
        return {};
    std::lock_guard<std::mutex> g(name_lock);
    if (generation)
        *generation = name_generation.load(std::memory_order_acquire);
    return name_now;
}

inline void clear()
{
    packed.store(0, std::memory_order_release);
    set_name({});
}

}
