#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static uint32_t pe_u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t pe_u16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int pe_rva_to_offset(const uint8_t *img, size_t size, uint32_t sections, uint16_t count, uint32_t rva,
                            uint32_t *off)
{
    for (uint16_t i = 0; i < count; i++) {
        uint32_t h = sections + i * 40u;
        if ((size_t)h + 40 > size)
            return 0;
        uint32_t vsize = pe_u32(img + h + 8), va = pe_u32(img + h + 12), raw_size = pe_u32(img + h + 16),
                 raw = pe_u32(img + h + 20);
        uint32_t span = vsize > raw_size ? vsize : raw_size;
        if (rva >= va && rva - va < span) {
            if (rva - va >= raw_size)
                return 0;
            *off = raw + (rva - va);
            return *off < size;
        }
    }
    return 0;
}

static int pe_exports_name(const uint8_t *img, size_t size, const char *name)
{
    if (size < 0x40 || img[0] != 'M' || img[1] != 'Z')
        return 0;
    uint32_t nt = pe_u32(img + 0x3c);
    if ((size_t)nt + 24 > size || memcmp(img + nt, "PE\0\0", 4))
        return 0;
    uint16_t count = pe_u16(img + nt + 6), opt_size = pe_u16(img + nt + 20);
    uint32_t opt = nt + 24;
    if ((size_t)opt + opt_size > size || opt_size < 2)
        return 0;
    uint16_t magic = pe_u16(img + opt);
    uint32_t dirs = magic == 0x20b ? opt + 112 : magic == 0x10b ? opt + 96 : 0;
    if (!dirs || dirs + 8 > opt + opt_size)
        return 0;
    uint32_t exp_rva = pe_u32(img + dirs), exp_size = pe_u32(img + dirs + 4);
    if (!exp_rva || !exp_size)
        return 0;
    uint32_t sections = opt + opt_size, exp;
    if (!pe_rva_to_offset(img, size, sections, count, exp_rva, &exp) || (size_t)exp + 40 > size)
        return 0;
    uint32_t names = pe_u32(img + exp + 24), names_rva = pe_u32(img + exp + 32), names_off;
    if (!names || !pe_rva_to_offset(img, size, sections, count, names_rva, &names_off))
        return 0;
    size_t want = strlen(name);
    for (uint32_t i = 0; i < names && (size_t)names_off + 4u * (i + 1) <= size; i++) {
        uint32_t off;
        if (!pe_rva_to_offset(img, size, sections, count, pe_u32(img + names_off + 4u * i), &off))
            continue;
        if ((size_t)off + want + 1 <= size && !memcmp(img + off, name, want) && img[off + want] == 0)
            return 1;
    }
    return 0;
}
