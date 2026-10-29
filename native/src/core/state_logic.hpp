#pragma once
#include <algorithm>
#include <string>

namespace runtime::state_logic {

constexpr const char *FILE = "state.json";
constexpr const char *KEY = "state_dir";
constexpr const char *ENV = "SWOFTY_STATE_DIR";

inline bool absolute(const std::wstring &p)
{
    if (p.size() >= 3 && p[1] == L':' && (p[2] == L'\\' || p[2] == L'/') &&
        ((p[0] >= L'A' && p[0] <= L'Z') || (p[0] >= L'a' && p[0] <= L'z')))
        return true;
    return p.size() >= 3 && p[0] == L'\\' && p[1] == L'\\' && p[2] != L'\\';
}

inline std::wstring root(std::wstring dir)
{
    while (!dir.empty() && (dir.front() == L' ' || dir.front() == L'"'))
        dir.erase(dir.begin());
    while (!dir.empty() && (dir.back() == L' ' || dir.back() == L'"'))
        dir.pop_back();
    std::replace(dir.begin(), dir.end(), L'/', L'\\');
    if (!absolute(dir))
        return {};
    while (dir.size() > 3 && dir.back() == L'\\')
        dir.pop_back();
    if (dir.back() != L'\\')
        dir += L'\\';
    return dir;
}

inline std::wstring join(const std::wstring &root, std::wstring rel)
{
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    return root + rel;
}

}
