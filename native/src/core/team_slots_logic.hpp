#pragma once

#include <cstddef>
#include <cstdint>

namespace team_slots {

constexpr std::size_t VECTOR_BEGIN = 0x08, VECTOR_END = 0x10, ENTRY_SIZE = 0x10, ENTRY_RECORD = 0x08;
constexpr std::size_t MAX_STAT = 0xffff;

inline uint16_t stat_of(uint32_t key)
{
    return static_cast<uint16_t>(key >> 16);
}

inline bool database_key(uint32_t key)
{
    return (key & 0xffffu) == 0;
}

template <class Read> uintptr_t record_in(uintptr_t vector, uint16_t stat, Read &&read)
{
    uintptr_t begin = 0, end = 0, record = 0;
    if (!vector || !read(vector + VECTOR_BEGIN, begin) || !read(vector + VECTOR_END, end) || !begin || end <= begin)
        return 0;
    if ((end - begin) % ENTRY_SIZE || (end - begin) / ENTRY_SIZE <= stat)
        return 0;
    if (!read(begin + stat * ENTRY_SIZE + ENTRY_RECORD, record))
        return 0;
    return record;
}

}
