#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "json.hpp"

// Pure logic of the test save sandbox (save_sandbox.cpp, re/db/subsystems/core.md#save-sandbox): the
// run token `swofty test` hands the game, the checks on the save device before its Steam remote storage
// interface is replaced, and the folder that stands in for Steam remote storage. No Windows calls, so
// the unit test runs on the host.

namespace runtime::save_sandbox {

constexpr const char *RUN_ENV = "SWOFTY_TEST_RUN";
constexpr const char *TOKEN_ENV = "SWOFTY_TEST_TOKEN";
constexpr const char *RUN_FILE = "run.json";
constexpr const char *REMOTE = "remote";
constexpr const char *TMP_SUFFIX = ".swofty-tmp";
constexpr size_t USER_DATA_CAP = 0x104;
constexpr size_t NAME_CAP = 260;
constexpr int SLOTS = 64;
constexpr int64_t QUOTA = int64_t(1) << 30;

// ISteamRemoteStorage (STEAMREMOTESTORAGE_INTERFACE_VERSION016) vtable slots, in declaration order.
enum Slot : int {
    FileWrite = 0,
    FileRead = 1,
    FileWriteAsync = 2,
    FileReadAsync = 3,
    FileReadAsyncComplete = 4,
    FileForget = 5,
    FileDelete = 6,
    FileShare = 7,
    SetSyncPlatforms = 8,
    FileWriteStreamOpen = 9,
    FileWriteStreamWriteChunk = 10,
    FileWriteStreamClose = 11,
    FileWriteStreamCancel = 12,
    FileExists = 13,
    FilePersisted = 14,
    GetFileSize = 15,
    GetFileTimestamp = 16,
    GetSyncPlatforms = 17,
    GetFileCount = 18,
    GetFileNameAndSize = 19,
    GetQuota = 20,
    IsCloudEnabledForAccount = 21,
    IsCloudEnabledForApp = 22,
    SetCloudEnabledForApp = 23,
    GetLocalFileChangeCount = 55,
    GetLocalFileChange = 56,
    BeginFileWriteBatch = 57,
    EndFileWriteBatch = 58,
};

inline const char *slot_name(int i)
{
    static const char *const first[] = {
        "FileWrite",          "FileRead",           "FileWriteAsync",
        "FileReadAsync",      "FileReadAsyncComplete", "FileForget",
        "FileDelete",         "FileShare",          "SetSyncPlatforms",
        "FileWriteStreamOpen", "FileWriteStreamWriteChunk", "FileWriteStreamClose",
        "FileWriteStreamCancel", "FileExists",      "FilePersisted",
        "GetFileSize",        "GetFileTimestamp",   "GetSyncPlatforms",
        "GetFileCount",       "GetFileNameAndSize", "GetQuota",
        "IsCloudEnabledForAccount", "IsCloudEnabledForApp", "SetCloudEnabledForApp"};
    if (i >= 0 && i < int(sizeof first / sizeof *first))
        return first[i];
    switch (i) {
    case GetLocalFileChangeCount:
        return "GetLocalFileChangeCount";
    case GetLocalFileChange:
        return "GetLocalFileChange";
    case BeginFileWriteBatch:
        return "BeginFileWriteBatch";
    case EndFileWriteBatch:
        return "EndFileWriteBatch";
    default:
        return i >= 24 && i <= 54 ? "UGC or workshop call" : "unknown";
    }
}

// The slots the sandbox serves from its folder. Every other slot answers 0 (false, no call, no file)
// and is logged; none is ever forwarded to Steam.
inline bool served(int i)
{
    switch (i) {
    case FileWrite:
    case FileRead:
    case FileForget:
    case FileDelete:
    case SetSyncPlatforms:
    case FileExists:
    case FilePersisted:
    case GetFileSize:
    case GetFileTimestamp:
    case GetSyncPlatforms:
    case GetFileCount:
    case GetFileNameAndSize:
    case GetQuota:
    case IsCloudEnabledForAccount:
    case IsCloudEnabledForApp:
    case SetCloudEnabledForApp:
    case GetLocalFileChangeCount:
    case BeginFileWriteBatch:
    case EndFileWriteBatch:
        return true;
    default:
        return false;
    }
}

struct Run {
    bool ok = false;
    std::string why, id, token, saves, exe_sha256;
    // The save sandbox is required: run.json names a saves folder.
    bool sandbox = false;
    // The test agent's protocol and its test DLLs, relative to the run's agent/ folder.
    int agent_v = 0;
    std::vector<std::string> dlls;
};

// A test DLL path in run.json: relative, inside the run's agent folder.
inline bool valid_dll(std::string_view d)
{
    return !d.empty() && d.size() <= 200 && d.find("..") == std::string_view::npos && d[0] != '/' && d[0] != '\\' &&
           d.find(':') == std::string_view::npos;
}

inline bool simple_name(std::string_view n)
{
    if (n.empty() || n.size() > 64 || n == "." || n == "..")
        return false;
    return std::all_of(n.begin(), n.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.';
    });
}

// run.json in the run folder (docs/design/std-2/test-suite.md "The test run"):
// {"schema":1,"run":"<id>","token":"<hex>","saves":"saves","exe_sha256":"...","agent":{"v":1,"dlls":[...]}}.
// The token in the environment must equal the file's, so a stale variable alone never switches test
// mode on and a run folder alone never does either. "saves" asks for the save sandbox; without it the
// run is the test agent's only.
inline Run parse_run(std::string_view text, std::string_view token)
{
    Run r;
    json::Value doc;
    if (text.empty())
        return r.why = "the run folder has no readable run.json", r;
    if (!json::parse(std::string(text), doc) || doc.type != json::Value::Obj)
        return r.why = "run.json does not parse as an object", r;
    const json::Value &schema = doc["schema"];
    if (schema.type != json::Value::Num || schema.n != 1)
        return r.why = "run.json has no schema 1", r;
    const json::Value &t = doc["token"];
    if (t.type != json::Value::Str || t.s.size() < 16)
        return r.why = "run.json has no token", r;
    if (token.empty())
        return r.why = std::string("the environment has no ") + TOKEN_ENV, r;
    if (t.s != token)
        return r.why = std::string("the token in ") + TOKEN_ENV + " is not this run's", r;
    const json::Value &saves = doc["saves"];
    if (saves.type != json::Value::Null && (saves.type != json::Value::Str || !simple_name(saves.s)))
        return r.why = "run.json's saves is not a plain folder name", r;
    const json::Value &agent = doc["agent"];
    if (agent.type == json::Value::Obj) {
        r.agent_v = int(agent["v"].num(0));
        for (auto &d : agent["dlls"].a) {
            if (d.type != json::Value::Str || !valid_dll(d.s))
                return r.why = "run.json names a test DLL outside the run's agent folder: " + d.str(), r;
            r.dlls.push_back(d.s);
        }
    }
    r.token = t.s;
    r.sandbox = saves.type == json::Value::Str;
    r.saves = r.sandbox ? saves.s : std::string();
    if (doc["run"].type == json::Value::Str)
        r.id = doc["run"].s;
    if (!simple_name(r.id))
        return r.why = "run.json's run is not a plain name", r;
    if (doc["exe_sha256"].type == json::Value::Str)
        r.exe_sha256 = doc["exe_sha256"].s;
    r.ok = true;
    return r;
}

// Whether run.json still holds the run's token: `swofty test` removes it when the run ends, which
// stops the agent.
inline bool still_armed(std::string_view text, std::string_view token)
{
    json::Value doc;
    return !text.empty() && json::parse(std::string(text), doc) && doc.type == json::Value::Obj &&
           doc["token"].type == json::Value::Str && !token.empty() && doc["token"].s == token;
}

// A path the game can open: a Unix path (the CLI under Proton) maps to Wine's Z: drive.
inline std::string windows_path(std::string p)
{
    if (!p.empty() && p[0] == '/')
        p = "Z:" + p;
    std::replace(p.begin(), p.end(), '/', '\\');
    while (p.size() > 3 && p.back() == '\\')
        p.pop_back();
    return p;
}

inline bool ansi_path(std::string_view p)
{
    return !p.empty() && std::all_of(p.begin(), p.end(), [](char c) { return c >= 0x20 && c < 0x7f; });
}

// A Steam remote storage file name: one flat name, as the save device builds them
// (<title dir>@<save>#<file>). Anything that could leave the folder is refused.
inline bool valid_name(std::string_view n)
{
    if (n.empty() || n.size() > NAME_CAP || n == "." || n == "..")
        return false;
    if (n.size() > strlen(TMP_SUFFIX) && n.substr(n.size() - strlen(TMP_SUFFIX)) == TMP_SUFFIX)
        return false;
    for (char c : n)
        if (static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|')
            return false;
    return n.find("..") == std::string_view::npos;
}

// What the core reads from the save device at Ne_SaveSystem2Win_Start, before it changes anything.
struct Device {
    bool readable = false;
    uintptr_t vtable = 0, start_slot = 0, start = 0;
    uintptr_t helper = 0, iface = 0, iface_vtable = 0;
    bool iface_vtable_in_game = false, iface_slots_ok = false;
    uint8_t store_in_cloud = 0;
    size_t user_data_len = 0;
};

inline std::string check_device(const Device &d)
{
    if (!d.readable)
        return "the save device is not readable";
    if (!d.start || d.start_slot != d.start)
        return "the save device's vtable slot +0x08 is not Ne_SaveSystem2Win_Start: not the NeSaveSystem2Win device";
    if (!d.helper)
        return "the save device has no Steam helper (+0x30): this build saves locally, which the sandbox does not cover";
    if (d.store_in_cloud != 1)
        return "the save device's m_storeInCloud (+0x3e8) is off: it would read and write the player's local save "
               "folder (Steam.cloudEnabled is false), which the sandbox does not cover";
    if (!d.iface)
        return "the Steam helper holds no remote storage interface (+0x108)";
    if (d.iface_vtable_in_game)
        return "the remote storage interface's vtable lies in the game image: not Steam's interface";
    if (!d.iface_slots_ok)
        return "the remote storage interface's first slots are not code";
    if (d.user_data_len >= USER_DATA_CAP)
        return "the user data folder (+0x00) is not a terminated string";
    return {};
}

// The folder that stands in for Steam remote storage: one file per remote name, written whole through a
// temporary file and a rename, read back byte for byte. The save device's files (the encrypted data file
// and its security index) are opaque bytes here.
class RemoteDir {
public:
    RemoteDir() = default;
    explicit RemoteDir(std::filesystem::path root) : root_(std::move(root)) {}

    const std::filesystem::path &root() const { return root_; }

    bool write(std::string_view name, const void *data, int32_t size) const
    {
        if (!valid_name(name) || size < 0 || (size && !data))
            return false;
        std::error_code ec;
        std::filesystem::create_directories(root_, ec);
        auto final = path(name), tmp = path(std::string(name) + TMP_SUFFIX);
        {
            std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
            if (!f)
                return false;
            f.write(static_cast<const char *>(data), size);
            if (!f.flush())
                return false;
        }
        std::filesystem::rename(tmp, final, ec);
        if (ec) {
            std::filesystem::remove(tmp, ec);
            return false;
        }
        return true;
    }

    int32_t read(std::string_view name, void *out, int32_t cap) const
    {
        if (!valid_name(name) || cap <= 0 || !out)
            return 0;
        std::ifstream f(path(name), std::ios::binary);
        if (!f)
            return 0;
        f.read(static_cast<char *>(out), cap);
        return int32_t(f.gcount());
    }

    bool remove(std::string_view name) const
    {
        std::error_code ec;
        return valid_name(name) && std::filesystem::remove(path(name), ec) && !ec;
    }

    bool exists(std::string_view name) const
    {
        std::error_code ec;
        return valid_name(name) && std::filesystem::is_regular_file(path(name), ec);
    }

    int32_t size(std::string_view name) const
    {
        std::error_code ec;
        if (!exists(name))
            return 0;
        auto n = std::filesystem::file_size(path(name), ec);
        return ec || n > uint64_t(INT32_MAX) ? 0 : int32_t(n);
    }

    int64_t timestamp(std::string_view name) const
    {
        std::error_code ec;
        if (!exists(name))
            return 0;
        auto t = std::filesystem::last_write_time(path(name), ec);
        if (ec)
            return 0;
        auto sys = std::chrono::time_point_cast<std::chrono::seconds>(
            t - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return int64_t(sys.time_since_epoch().count());
    }

    uint64_t used() const
    {
        uint64_t n = 0;
        for (auto &[name, size] : scan())
            n += uint64_t(size);
        return n;
    }

    // GetFileCount takes a snapshot that GetFileNameAndSize indexes, as Steam's own list does.
    int32_t count()
    {
        listing_ = scan();
        return int32_t(listing_.size());
    }

    const char *name_at(int32_t i, int32_t *size)
    {
        if (listing_.empty())
            listing_ = scan();
        if (i < 0 || size_t(i) >= listing_.size()) {
            if (size)
                *size = 0;
            return "";
        }
        if (size)
            *size = listing_[size_t(i)].second;
        return listing_[size_t(i)].first.c_str();
    }

private:
    std::filesystem::path path(std::string_view name) const { return root_ / std::filesystem::path(std::string(name)); }

    std::vector<std::pair<std::string, int32_t>> scan() const
    {
        std::vector<std::pair<std::string, int32_t>> out;
        std::error_code ec;
        for (auto it = std::filesystem::directory_iterator(root_, ec); !ec && it != std::filesystem::directory_iterator();
             it.increment(ec)) {
            std::error_code e2;
            if (!it->is_regular_file(e2))
                continue;
            std::string n = it->path().filename().string();
            if (!valid_name(n))
                continue;
            auto sz = it->file_size(e2);
            out.emplace_back(n, e2 || sz > uint64_t(INT32_MAX) ? 0 : int32_t(sz));
        }
        std::sort(out.begin(), out.end());
        return out;
    }

    std::filesystem::path root_;
    std::vector<std::pair<std::string, int32_t>> listing_;
};

}
