#pragma once
#include <windows.h>

#include <cstdint>
#include <cstdio>

// Walks the exe's import slots for one DLL. The game imports some DLLs through more than one
// descriptor (ws2_32 twice: the clear code's table and the one the .data1 bodies call through, the
// second by ordinal), so every descriptor naming the DLL is visited; `table` counts them from 1 in
// directory order. `fn` is what the import resolves to in `lib`, so a slot is matched by what it
// imports, not by what it holds now: a slot another hook already took still matches.

namespace runtime {

struct ImportSlot {
    int table;
    ULONG_PTR *slot;
    void *fn;
    uintptr_t rva;
    char how[24];
};

template <class F> int each_import(HMODULE lib, const char *dll, F &&visit)
{
    auto base = reinterpret_cast<uint8_t *>(GetModuleHandleW(nullptr));
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS *>(base + reinterpret_cast<IMAGE_DOS_HEADER *>(base)->e_lfanew);
    auto &dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return 0;
    int tables = 0;
    for (auto *d = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR *>(base + dir.VirtualAddress); d->Name; d++) {
        if (_stricmp(reinterpret_cast<const char *>(base + d->Name), dll))
            continue;
        tables++;
        auto *iat = reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->FirstThunk);
        auto *names = d->OriginalFirstThunk ? reinterpret_cast<IMAGE_THUNK_DATA *>(base + d->OriginalFirstThunk) : iat;
        for (; names->u1.AddressOfData; names++, iat++) {
            ImportSlot s{tables, &iat->u1.Function, nullptr, 0, {}};
            s.rva = uintptr_t(reinterpret_cast<uint8_t *>(&iat->u1.Function) - base);
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                s.fn = reinterpret_cast<void *>(GetProcAddress(lib, MAKEINTRESOURCEA(IMAGE_ORDINAL(names->u1.Ordinal))));
                snprintf(s.how, sizeof s.how, "ordinal %u", unsigned(IMAGE_ORDINAL(names->u1.Ordinal)));
            } else if (d->OriginalFirstThunk) {
                auto *by = reinterpret_cast<IMAGE_IMPORT_BY_NAME *>(base + names->u1.AddressOfData);
                s.fn = reinterpret_cast<void *>(GetProcAddress(lib, reinterpret_cast<const char *>(by->Name)));
                snprintf(s.how, sizeof s.how, "name");
            } else {
                s.fn = reinterpret_cast<void *>(iat->u1.Function);
                snprintf(s.how, sizeof s.how, "bound");
            }
            visit(s);
        }
    }
    return tables;
}

inline bool write_slot(ULONG_PTR *slot, void *value)
{
    DWORD old;
    if (!VirtualProtect(slot, sizeof *slot, PAGE_READWRITE, &old))
        return false;
    *slot = reinterpret_cast<ULONG_PTR>(value);
    VirtualProtect(slot, sizeof *slot, old, &old);
    return true;
}

}
