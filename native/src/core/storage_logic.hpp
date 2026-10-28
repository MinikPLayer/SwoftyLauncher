#pragma once
#include <algorithm>
#include <string>
#include <string_view>

// Pure path logic of the storage library (storage.cpp): the relative path an addon names, the root in the
// data folder (loader/data_folder.h) its files move to when the game can't create files in
// swofty/addons/<id>/, and the listing of a folder whose files are split between that root and the game
// folder. No Windows calls, so the unit test runs on the host.

namespace runtime::storage_logic {

constexpr const char *PROBE = ".swofty-write-test";
constexpr const char *MARK = ".swofty-data-folder";

inline bool reserved(std::string_view name)
{
    return name == PROBE || name == MARK;
}

inline bool inside(const char *path, bool allow_root, std::string &rel)
{
    rel = path ? path : "";
    std::replace(rel.begin(), rel.end(), '\\', '/');
    if (allow_root && (rel.empty() || rel == ".")) {
        rel.clear();
        return true;
    }
    return !rel.empty() && rel[0] != '/' && rel.find(':') == std::string::npos &&
           ("/" + rel + "/").find("/../") == std::string::npos && !reserved(rel);
}

inline std::wstring data_root(std::wstring folder, const std::wstring &id)
{
    while (!folder.empty() && (folder.back() == L'\\' || folder.back() == L'/'))
        folder.pop_back();
    if (folder.empty() || id.empty())
        return {};
    return folder + L"\\addon-storage\\" + id + L"\\";
}

inline std::wstring join(const std::wstring &root, std::wstring rel)
{
    std::replace(rel.begin(), rel.end(), L'/', L'\\');
    return root + rel;
}

inline std::string_view bare(std::string_view name)
{
    return !name.empty() && name.back() == '/' ? name.substr(0, name.size() - 1) : name;
}

inline std::string without_reserved(const std::string &listing)
{
    std::string out;
    for (size_t at = 0; at < listing.size();) {
        size_t nl = listing.find('\n', at);
        std::string_view name(listing.data() + at, (nl == std::string::npos ? listing.size() : nl) - at);
        at = nl == std::string::npos ? listing.size() : nl + 1;
        if (!name.empty() && !reserved(bare(name))) {
            out.append(name);
            out += '\n';
        }
    }
    return out;
}

inline std::string merge_listing(const std::string &primary, const std::string &fallback)
{
    std::string out = without_reserved(primary);
    auto has = [&](std::string_view name) {
        for (size_t at = 0; at < primary.size();) {
            size_t nl = primary.find('\n', at);
            std::string_view mine(primary.data() + at, (nl == std::string::npos ? primary.size() : nl) - at);
            if (bare(mine) == bare(name))
                return true;
            at = nl == std::string::npos ? primary.size() : nl + 1;
        }
        return false;
    };
    for (size_t at = 0; at < fallback.size();) {
        size_t nl = fallback.find('\n', at);
        std::string_view name(fallback.data() + at, (nl == std::string::npos ? fallback.size() : nl) - at);
        at = nl == std::string::npos ? fallback.size() : nl + 1;
        if (name.empty() || reserved(bare(name)) || has(name))
            continue;
        out.append(name);
        out += '\n';
    }
    return out;
}

}
