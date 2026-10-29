#include "logos.hpp"

#include "../files_api.hpp"
#include "../../runtime.hpp"
#include "logo_build.hpp"
#include "png_straight.hpp"

#include <swofty/mixin.hpp>

#include <algorithm>
#include <map>
#include <memory>
#include <set>

namespace swofty_core::apply::content {

namespace {

constexpr const char *CACHE_ADDON = "swofty.logos";
constexpr const char *ROUTE_ID = "logos";

struct FileSource {
    HANDLE h = INVALID_HANDLE_VALUE;
    erpfile::Source src;
    ~FileSource()
    {
        if (h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};

std::wstring game_file(const std::string &rel)
{
    std::string p = rel;
    std::replace(p.begin(), p.end(), '/', '\\');
    return runtime::game_root() + L"\\" + runtime::wide(p);
}

void list_erps(const std::wstring &dir, const std::string &rel, std::vector<std::string> &out, unsigned depth)
{
    if (depth > 4)
        return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE)
        return;
    do {
        std::wstring n = fd.cFileName;
        if (n == L"." || n == L"..")
            continue;
        std::string r = rel + runtime::narrow(n);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            list_erps(dir + n + L"\\", r + "/", out, depth + 1);
        else if (r.size() > 4 && runtime::lower(r.substr(r.size() - 4)) == ".erp")
            out.push_back(r);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(out.begin(), out.end());
}

uint64_t stamp(uint64_t h, const std::string &s)
{
    for (unsigned char c : s)
        h = (h ^ c) * 0x100000001b3ull;
    return h * 0x100000001b3ull + 0x9e3779b97f4a7c15ull;
}

std::string flat(const std::string &rel)
{
    std::string s = rel.substr(std::string(logo_build::UI_FOLDER).size());
    std::replace(s.begin(), s.end(), '/', '_');
    return s;
}

}

void deliver_logos(const std::vector<LogoImage> &logos, LogoReport &report)
{
    std::vector<logo_build::Want> wants;
    std::set<std::string> seen;
    uint64_t key = 0xcbf29ce484222325ull;
    for (auto &l : logos) {
        if (!seen.insert(l.array + "|" + l.key).second)
            continue;
        logo_build::Want w;
        w.array = l.array;
        w.key = l.key;
        key = stamp(key, l.array + "|" + l.key + "|" + l.stock + (l.fallback ? "|f" : ""));
        if (l.stock.empty()) {
            std::string bytes = runtime::read_file(runtime::under_swofty("addons/" + l.addon + "/" + l.file), size_t(64) << 20), why;
            if (bytes.empty() || !png::decode(bytes, w.rgba, w.width, w.height, why)) {
                report.lines.push_back("team.logo " + l.id + ": " + l.context + " image " + l.file + (why.empty() ? " is empty" : ": " + why));
                report.refused++;
                continue;
            }
            key = stamp(key, bytes);
        } else {
            w.donor = l.stock;
            w.fallback = l.fallback;
        }
        wants.push_back(std::move(w));
    }
    if (wants.empty())
        return;
    std::vector<std::string> files;
    list_erps(game_file(logo_build::UI_FOLDER), logo_build::UI_FOLDER, files, 0);
    for (auto &rel : files) {
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (GetFileAttributesExW(game_file(rel).c_str(), GetFileExInfoStandard, &a))
            key = stamp(key, rel + "|" + std::to_string(a.nFileSizeLow) + "|" + std::to_string(a.nFileSizeHigh) + "|" +
                                 std::to_string(a.ftLastWriteTime.dwLowDateTime) + "|" + std::to_string(a.ftLastWriteTime.dwHighDateTime));
    }
    std::string id = runtime::hex16(key).substr(2);
    std::string dir = "addons/" + std::string(CACHE_ADDON) + "/";
    std::wstring dirw = runtime::under_swofty("addons");
    CreateDirectoryW(dirw.c_str(), nullptr);
    CreateDirectoryW(runtime::under_swofty(dir).c_str(), nullptr);
    std::string manifest_name = "logos_" + id + ".manifest";
    std::string manifest = runtime::read_file(runtime::under_swofty(dir + manifest_name), size_t(1) << 20);
    std::vector<std::pair<std::string, std::string>> routes;
    if (!manifest.empty()) {
        size_t at = 0;
        while (at < manifest.size()) {
            size_t end = manifest.find('\n', at);
            if (end == std::string::npos)
                end = manifest.size();
            std::string line = manifest.substr(at, end - at);
            size_t tab = line.find('\t');
            if (tab != std::string::npos)
                routes.emplace_back(line.substr(0, tab), line.substr(tab + 1));
            at = end + 1;
        }
        report.lines.push_back("team.logo: " + std::to_string(routes.size()) + " cached archive(s) of " + id + " reused");
    } else {
        DWORD t0 = GetTickCount();
        logo_build::Env env;
        env.list = [&] { return files; };
        env.open = [&](const std::string &rel) -> std::shared_ptr<erpfile::Source> {
            auto fs = std::make_shared<FileSource>();
            fs->h = CreateFileW(game_file(rel).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            LARGE_INTEGER size;
            if (fs->h == INVALID_HANDLE_VALUE || !GetFileSizeEx(fs->h, &size))
                return nullptr;
            fs->src.size = uint64_t(size.QuadPart);
            HANDLE h = fs->h;
            fs->src.read = [h](uint64_t off, size_t n, char *out) {
                LARGE_INTEGER at;
                at.QuadPart = LONGLONG(off);
                DWORD got = 0;
                return SetFilePointerEx(h, at, nullptr, FILE_BEGIN) && ReadFile(h, out, DWORD(n), &got, nullptr) && got == n;
            };
            return std::shared_ptr<erpfile::Source>(fs, &fs->src);
        };
        logo_build::Result res;
        logo_build::build(env, wants, res);
        for (auto &l : res.lines)
            report.lines.push_back(l);
        report.refused += res.refused;
        std::string out_manifest;
        for (auto &[rel, changes] : res.changes) {
            auto src = env.open(rel);
            erpfile::Archive arc;
            std::string why;
            std::string file = "logos_" + id + "_" + flat(rel);
            std::wstring path = runtime::under_swofty(dir + file), tmp = path + L".tmp";
            HANDLE out = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            bool ok = src && out != INVALID_HANDLE_VALUE && erpfile::open(*src, arc, why) &&
                      erpfile::rewrite(*src, arc, changes, [&](const char *p, size_t n) {
                          DWORD put = 0;
                          while (n) {
                              DWORD want = DWORD(std::min<size_t>(n, size_t(1) << 24));
                              if (!WriteFile(out, p, want, &put, nullptr) || !put)
                                  return false;
                              p += put;
                              n -= put;
                          }
                          return true;
                      }, why);
            if (out != INVALID_HANDLE_VALUE)
                CloseHandle(out);
            if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                DeleteFileW(tmp.c_str());
                report.lines.push_back("team.logo: " + rel + " could not be rewritten" + (why.empty() ? "" : ": " + why));
                report.refused++;
                continue;
            }
            routes.emplace_back(rel, file);
            out_manifest += rel + "\t" + file + "\n";
        }
        if (routes.size() == res.changes.size() && !routes.empty())
            runtime::write_file(runtime::under_swofty(dir + manifest_name), out_manifest);
        report.built = res.built;
        report.lines.push_back("team.logo: " + std::to_string(res.built) + " slice(s) built into " + std::to_string(routes.size()) +
                               " archive(s) in " + std::to_string(GetTickCount() - t0) + " ms");
    }
    std::set<std::string> keep;
    for (auto &r : routes)
        keep.insert(r.second);
    keep.insert(manifest_name);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((runtime::under_swofty(dir) + L"logos_*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::string n = runtime::narrow(fd.cFileName);
            if (!keep.count(n))
                DeleteFileW((runtime::under_swofty(dir) + runtime::wide(n)).c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    for (auto &[game, file] : routes) {
        auto o = files::redirect(game, CACHE_ADDON, file, "team.logo", ROUTE_ID, "boot");
        if (o.refused())
            report.refused++;
    }
}

}
