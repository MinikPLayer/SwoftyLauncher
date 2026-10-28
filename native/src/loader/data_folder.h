#pragma once
#include <wchar.h>
#include <windows.h>

/* The launcher's data folder as the game sees it, %LOCALAPPDATA%\Swofty, written to out without a trailing
   backslash. The loader logs there when the game folder refuses its log, and the core keeps an addon's
   storage there when the game can't create files in the addon's folder. Returns the length, or 0 when
   LOCALAPPDATA is unset or the path doesn't fit in cap characters. */
static inline DWORD swofty_data_folder(wchar_t *out, DWORD cap)
{
    static const wchar_t SUB[] = L"\\Swofty";
    const DWORD sub = sizeof SUB / sizeof *SUB - 1;
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", out, cap);
    if (!n || n >= cap)
        return 0;
    while (n && (out[n - 1] == L'\\' || out[n - 1] == L'/'))
        n--;
    if (!n || n + sub >= cap)
        return 0;
    wmemcpy(out + n, SUB, sub + 1);
    return n + sub;
}
