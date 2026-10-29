#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

#include "json.hpp"

// Netplay command logic without the game: parsing a launcher command, deciding whether the game's
// state allows it, and the JSON the reply and status file carry. netplay_drive.cpp feeds it the flow
// states and screens it sees; the host test drives it directly.

namespace runtime::drive {

constexpr uint32_t djb2(const char *s)
{
    uint32_t h = 5381;
    for (; *s; ++s)
        h = h * 33 + static_cast<uint8_t>(*s);
    return h;
}

constexpr uint32_t STATE_FRONTEND = djb2("StateFrontend");
constexpr uint32_t STATE_ONLINE_LOBBY = djb2("StateOnlineLobby");
constexpr uint32_t STATE_READY_MONITOR = djb2("StateReadyMonitor");

constexpr int GAME_PORT = 21414;

static_assert(STATE_FRONTEND == 0x957880c6u, "state name hashing drifted");
static_assert(STATE_ONLINE_LOBBY == 0x5e31bae3u, "state name hashing drifted");

enum class Op { none, status, configure, clear, host, join, leave, cancel };
enum class Category { none, f1_2026, f1_2025 };
enum class Screen { main_menu, lan_lobby, other };
enum class Role { none, host, client };

struct Command {
    uint64_t seq = 0;
    Op op = Op::none;
    Category category = Category::none;
    std::string ip;
    int self = 0, base = 0;
    std::string name;
    std::string error;
};

// The game keeps a presence name in 128-byte buffers (the fixed name resolver's slot and the presence
// name message, Ne_PresenceNameResolverFixed_AddName 0x14412a320), so a driver name is at most 127 bytes.
constexpr size_t DRIVER_NAME_BYTES = 127;

// One UTF-8 sequence at s[i]: its length with the code point in cp, or 0 for an invalid, overlong or
// surrogate sequence.
inline size_t utf8_at(const std::string &s, size_t i, uint32_t &cp)
{
    auto c = static_cast<unsigned char>(s[i]);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
    if (!n || i + n > s.size())
        return 0;
    cp = n == 1 ? c : c & (0x7fu >> n);
    for (size_t k = 1; k < n; k++) {
        auto d = static_cast<unsigned char>(s[i + k]);
        if ((d & 0xc0) != 0x80)
            return 0;
        cp = cp << 6 | (d & 0x3fu);
    }
    static constexpr uint32_t least[] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < least[n] || cp > 0x10ffff || (cp >= 0xd800 && cp < 0xe000))
        return 0;
    return n;
}

// Control characters, line and paragraph separators, and characters that draw nothing: soft hyphen,
// zero-width and bidi marks, fillers, variation selectors, tags.
inline bool invisible(uint32_t cp)
{
    return cp < 0x20 || (cp >= 0x7f && cp <= 0x9f) || cp == 0xad || cp == 0x34f || cp == 0x61c || cp == 0x115f ||
           cp == 0x1160 || cp == 0x17b4 || cp == 0x17b5 || (cp >= 0x180b && cp <= 0x180f) ||
           (cp >= 0x200b && cp <= 0x200f) || (cp >= 0x2028 && cp <= 0x202e) || (cp >= 0x2060 && cp <= 0x206f) ||
           cp == 0x3164 || (cp >= 0xfe00 && cp <= 0xfe0f) || cp == 0xfeff || cp == 0xffa0 ||
           (cp >= 0xfff9 && cp <= 0xfffb) || (cp >= 0x1d173 && cp <= 0x1d17a) || (cp >= 0xe0000 && cp <= 0xe0fff);
}

inline bool blank(uint32_t cp)
{
    return cp == 0x20 || cp == 0xa0 || cp == 0x1680 || (cp >= 0x2000 && cp <= 0x200a) || cp == 0x202f ||
           cp == 0x205f || cp == 0x3000;
}

// The signed-in name as the game's driver carries it: invalid UTF-8, control and invisible characters
// dropped, spaces trimmed and runs of them made one space, cut to DRIVER_NAME_BYTES at a character
// boundary. Empty keeps the game's own name.
inline std::string driver_name(const std::string &raw)
{
    std::string out;
    bool space = false;
    for (size_t i = 0; i < raw.size();) {
        uint32_t cp = 0;
        size_t n = utf8_at(raw, i, cp);
        if (!n) {
            i++;
            continue;
        }
        if (invisible(cp) || blank(cp)) {
            space = space || (blank(cp) && !out.empty());
            i += n;
            continue;
        }
        if (out.size() + (space ? 1 : 0) + n > DRIVER_NAME_BYTES)
            break;
        if (space)
            out += ' ';
        space = false;
        out.append(raw, i, n);
        i += n;
    }
    return out;
}

// %XX escapes decoded; false for a malformed escape.
inline bool percent_decode(const std::string &in, std::string &out)
{
    auto hex = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
    };
    out.clear();
    for (size_t i = 0; i < in.size(); i++) {
        if (in[i] != '%') {
            out += in[i];
            continue;
        }
        int hi = i + 2 < in.size() ? hex(in[i + 1]) : -1, lo = hi >= 0 ? hex(in[i + 2]) : -1;
        if (lo < 0)
            return false;
        out += char(hi << 4 | lo);
        i += 2;
    }
    return true;
}

inline const char *op_name(Op o)
{
    switch (o) {
    case Op::status: return "status";
    case Op::configure: return "configure";
    case Op::clear: return "clear";
    case Op::host: return "host";
    case Op::join: return "join";
    case Op::leave: return "leave";
    case Op::cancel: return "cancel";
    default: return "none";
    }
}

inline const char *category_name(Category c)
{
    return c == Category::f1_2026 ? "f1-2026" : c == Category::f1_2025 ? "f1-2025" : "none";
}

inline const char *screen_name(Screen s)
{
    return s == Screen::main_menu ? "main_menu" : s == Screen::lan_lobby ? "lan_lobby" : "other";
}

inline const char *role_name(Role r)
{
    return r == Role::host ? "host" : r == Role::client ? "client" : "none";
}

inline Category parse_category(const std::string &s)
{
    if (s == "f1-2026" || s == "f1_2026" || s == "2026")
        return Category::f1_2026;
    if (s == "f1-2025" || s == "f1_2025" || s == "2025")
        return Category::f1_2025;
    return Category::none;
}

// Dotted IPv4 only, four decimal parts 0-255 with no leading zeros, the form inet_addr reads the
// same everywhere. Join via IP (0x14328deb0) passes the text straight to inet_addr.
inline bool valid_ipv4(const std::string &s)
{
    int parts = 0;
    size_t i = 0;
    while (parts < 4) {
        size_t start = i;
        unsigned v = 0;
        while (i < s.size() && s[i] >= '0' && s[i] <= '9' && i - start < 3)
            v = v * 10 + unsigned(s[i++] - '0');
        size_t len = i - start;
        if (!len || v > 255 || (len > 1 && s[start] == '0'))
            return false;
        if (++parts < 4) {
            if (i >= s.size() || s[i] != '.')
                return false;
            ++i;
        }
    }
    return i == s.size() && s != "0.0.0.0" && s != "255.255.255.255";
}

inline Command parse(const std::string &text)
{
    Command c;
    json::Value doc;
    if (!json::parse(text, doc) || doc.type != json::Value::Obj) {
        c.error = "not a JSON object";
        return c;
    }
    const json::Value &seq = doc["seq"];
    if (seq.type == json::Value::Num && seq.n >= 1)
        c.seq = uint64_t(seq.n);
    std::string op = doc["op"].str();
    if (op == "status")
        c.op = Op::status;
    else if (op == "configure")
        c.op = Op::configure;
    else if (op == "clear")
        c.op = Op::clear;
    else if (op == "host")
        c.op = Op::host;
    else if (op == "join")
        c.op = Op::join;
    else if (op == "leave")
        c.op = Op::leave;
    else if (op == "cancel")
        c.op = Op::cancel;
    else {
        c.error = op.empty() ? "no op" : "unknown op '" + op + "'";
        return c;
    }
    const json::Value &args = doc["args"];
    if (c.op == Op::host) {
        std::string cat = args["category"].str();
        c.category = parse_category(cat);
        if (c.category == Category::none)
            c.error = cat.empty() ? "host needs args.category (f1-2026 or f1-2025)"
                                  : "unknown category '" + cat + "' (f1-2026 or f1-2025)";
    } else if (c.op == Op::configure) {
        const json::Value &self = args["self"], &base = args["base"];
        c.self = self.type == json::Value::Num && self.n == double(int(self.n)) ? int(self.n) : -1;
        c.base = base.type == json::Value::Num && base.n == double(int(base.n)) ? int(base.n) : -1;
        if (c.self < 1 || c.self > 254)
            c.error = "configure needs args.self, a member number from 1 to 254";
        else if (c.base < 1 || c.base > 65281)
            c.error = "configure needs args.base, a port from 1 to 65281";
        else if (GAME_PORT > c.base && GAME_PORT <= c.base + 254)
            c.error = "args.base " + std::to_string(c.base) + " puts the game port 21414 inside the bridge's ports (base+1 to base+254)";
        const json::Value &name = args["name"];
        if (name.type == json::Value::Str)
            c.name = driver_name(name.s);
        else if (name.type != json::Value::Null && c.error.empty())
            c.error = "args.name, the driver name, must be a string";
    } else if (c.op == Op::join) {
        c.ip = args["ip"].str();
        if (!valid_ipv4(c.ip))
            c.error = c.ip.empty() ? "join needs args.ip (dotted IPv4)" : "'" + c.ip + "' is not a dotted IPv4 address";
    }
    return c;
}

struct State {
    bool armed = false;
    bool configured = false;
    int self = 0, base = 0;
    bool frontend = false;
    bool lobby = false;
    bool home_screen = false;
    bool busy = false;
    Role role = Role::none;

    Screen screen() const
    {
        if (lobby)
            return Screen::lan_lobby;
        if (frontend && home_screen && !busy)
            return Screen::main_menu;
        return Screen::other;
    }

    bool in_session() const { return lobby; }
};

// Empty when the command may run now, else the reason it is refused. status, clear and cancel always
// run, also while a host or join is running.
inline std::string refuse(const Command &c, const State &s)
{
    if (!c.error.empty())
        return c.error;
    if (c.op == Op::status || c.op == Op::clear || c.op == Op::cancel)
        return {};
    if (!s.armed)
        return "netplay is not armed for this game session (SWOFTY_NETPLAY is unset)";
    if (s.busy)
        return "a netplay command is still running";
    switch (c.op) {
    case Op::configure:
        if (s.screen() != Screen::main_menu && s.screen() != Screen::lan_lobby)
            return "the room can be set only on the main menu or in a LAN lobby";
        return {};
    case Op::host:
    case Op::join:
        if (!s.configured)
            return "no room is set; send configure first";
        if (s.in_session())
            return "already in a LAN session; leave it first";
        if (s.screen() != Screen::main_menu)
            return "the game is not on its main menu";
        return {};
    case Op::leave:
        if (!s.in_session())
            return "not in a LAN session";
        return {};
    default:
        return "unknown op";
    }
}

inline void quote(std::string &out, const std::string &s)
{
    out += '"';
    for (unsigned char ch : s) {
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += char(ch);
        } else if (ch < 0x20) {
            char b[8];
            std::snprintf(b, sizeof b, "\\u%04x", ch);
            out += b;
        } else {
            out += char(ch);
        }
    }
    out += '"';
}

inline std::string status_json(const State &s)
{
    std::string out = "{\"screen\":\"";
    out += screen_name(s.screen());
    out += "\",\"in_session\":";
    out += s.in_session() ? "true" : "false";
    out += ",\"role\":\"";
    out += role_name(s.in_session() ? s.role : Role::none);
    out += "\",\"armed\":";
    out += s.armed ? "true" : "false";
    out += ",\"configured\":";
    out += s.configured ? "true" : "false";
    out += ",\"self\":" + std::to_string(s.configured ? s.self : 0);
    out += ",\"base\":" + std::to_string(s.configured ? s.base : 0);
    out += '}';
    return out;
}

inline std::string reply_json(const Command &c, const State &s, const std::string &error, uint64_t t,
                              const std::string &detail = {})
{
    std::string out = "{\"seq\":" + std::to_string(c.seq) + ",\"op\":\"" + op_name(c.op) + "\",\"ok\":";
    out += error.empty() ? "true" : "false";
    out += ",\"error\":";
    if (error.empty())
        out += "null";
    else
        quote(out, error);
    if (!detail.empty()) {
        out += ",\"detail\":";
        quote(out, detail);
    }
    out += ",\"status\":" + status_json(s) + ",\"t\":" + std::to_string(t) + '}';
    return out;
}

// The steps host and join take, the way the menus go: home fires local_multiplayer, the LAN tile's
// getAction switches the game to LAN mode and Local Multiplayer fires lan, which opens
// multiplayer_transition and its LAN Game dialog (multiplayer_lan_msg). The dialog is answered with
// the button the player would press (open_lan: Host LAN game, join_via_ip: Join via IP address); for
// join the IP prompt (set_join_via_ip) is answered with ok once the address is in
// ui_menu_data.lan_ip_address. The job then waits for StateOnlineLobby. Every wait has a deadline,
// and a popup whose id names an error while the job runs ends it with that id.
enum class Step { none, open_local_mp, wait_local_mp, switch_lan, check_lan, fire_lan, wait_dialog, answer_dialog,
                  wait_ip_prompt, answer_ip, wait_lobby, finished };
enum class Do { nothing, fire_local_mp, mode_switch, fire_lan, answer_dialog, answer_ip };

inline const char *do_name(Do d)
{
    switch (d) {
    case Do::nothing: return "nothing";
    case Do::fire_local_mp: return "open Local Multiplayer";
    case Do::mode_switch: return "switch to LAN mode";
    case Do::fire_lan: return "open LAN Game";
    case Do::answer_dialog: return "answer the LAN Game dialog";
    case Do::answer_ip: return "enter the host IP";
    default: return "unknown";
    }
}

struct Facts {
    bool local_mp = false;
    bool lan_mode = false;
    bool lobby = false;
    bool home = false;
};

constexpr const char *BACK_HOME = "the game went back to its main menu before the LAN lobby opened";

constexpr const char *LAN_DIALOG = "multiplayer_lan_msg";
constexpr const char *IP_PROMPT = "set_join_via_ip";
constexpr uint64_t OPEN_MS = 10000, PROMPT_MS = 5000, JOIN_LOBBY_MS = 30000, HOST_LOBBY_MS = 120000;

class Job {
public:
    void start(const Command &c, uint64_t now)
    {
        cmd = c;
        step = Step::open_local_mp;
        error.clear();
        detail.clear();
        started = now;
        deadline = 0;
    }

    bool active() const { return step != Step::none && step != Step::finished; }
    bool finished() const { return step == Step::finished; }
    void reset() { step = Step::none; }

    void fail(const std::string &why)
    {
        error = why;
        step = Step::finished;
    }

    // Abort a running host or join: it ends at its next tick and answers with this error. False when
    // nothing was running.
    bool cancel()
    {
        if (!active())
            return false;
        fail("cancelled by the launcher");
        return true;
    }

    // A popup the game is about to show; true when the job answers it instead.
    bool popup(const std::string &id, uint64_t now)
    {
        if (!active())
            return false;
        if (step == Step::wait_dialog && id == LAN_DIALOG) {
            step = Step::answer_dialog;
            return true;
        }
        if (step == Step::wait_ip_prompt && id == IP_PROMPT) {
            step = Step::answer_ip;
            return true;
        }
        if (id.find("error") != std::string::npos || id.find("banned") != std::string::npos) {
            fail("the game showed '" + id + "'");
            (void)now;
        }
        return false;
    }

    const char *button() const { return cmd.op == Op::host ? "open_lan" : "join_via_ip"; }

    Do tick(const Facts &f, uint64_t now)
    {
        auto wait = [&](uint64_t ms) { deadline = now + ms; };
        auto late = [&](const char *what) {
            if (now < deadline)
                return false;
            fail(what);
            return true;
        };
        switch (step) {
        case Step::open_local_mp:
            step = Step::wait_local_mp;
            wait(OPEN_MS);
            return Do::fire_local_mp;
        case Step::wait_local_mp:
            if (f.local_mp)
                step = Step::switch_lan;
            else
                late("Local Multiplayer did not open");
            return Do::nothing;
        case Step::switch_lan:
            step = Step::check_lan;
            return Do::mode_switch;
        case Step::check_lan:
            if (!f.lan_mode) {
                fail("the game did not switch to LAN mode");
                return Do::nothing;
            }
            step = Step::fire_lan;
            return Do::nothing;
        case Step::fire_lan:
            step = Step::wait_dialog;
            wait(OPEN_MS);
            return Do::fire_lan;
        case Step::wait_dialog:
            late("the LAN Game dialog did not open");
            return Do::nothing;
        case Step::answer_dialog:
            if (cmd.op == Op::host) {
                step = Step::wait_lobby;
                wait(HOST_LOBBY_MS);
                detail = "Host LAN game chosen; the car category is picked on the game's Car Category screen";
            } else {
                step = Step::wait_ip_prompt;
                wait(PROMPT_MS);
            }
            return Do::answer_dialog;
        case Step::wait_ip_prompt:
            if (f.home)
                fail(BACK_HOME);
            else
                late("the Join via IP prompt did not open");
            return Do::nothing;
        case Step::answer_ip:
            step = Step::wait_lobby;
            wait(JOIN_LOBBY_MS);
            return Do::answer_ip;
        case Step::wait_lobby:
            if (f.lobby)
                step = Step::finished;
            else if (f.home)
                fail(BACK_HOME);
            else
                late(cmd.op == Op::host ? "the LAN lobby did not open" : "the host's LAN lobby did not open");
            return Do::nothing;
        default:
            return Do::nothing;
        }
    }

    Command cmd;
    Step step = Step::none;
    std::string error, detail;
    uint64_t started = 0, deadline = 0;
};

// Names of inbox files the launcher writes: <digits>.json, written as .tmp and renamed.
inline bool inbox_name(const std::string &name, uint64_t *seq)
{
    if (name.size() < 6 || name.size() > 25 || name.compare(name.size() - 5, 5, ".json") != 0)
        return false;
    uint64_t v = 0;
    for (size_t i = 0; i + 5 < name.size(); i++) {
        if (name[i] < '0' || name[i] > '9')
            return false;
        v = v * 10 + uint64_t(name[i] - '0');
    }
    *seq = v;
    return v != 0;
}

}
