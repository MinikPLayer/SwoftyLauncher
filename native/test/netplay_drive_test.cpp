// Host unit test for the netplay command logic (netplay_drive_logic.hpp): command parsing, the IPv4
// check, state gating and the reply JSON. run_events_test.sh builds and runs it.
#include "../src/core/netplay_drive_logic.hpp"

#include <cstdio>
#include <string>

namespace drive = runtime::drive;

static int fails;

static void expect(bool ok, const std::string &what)
{
    printf("%s: %s\n", ok ? "ok  " : "FAIL", what.c_str());
    fails += !ok;
}

static drive::State main_menu(bool armed = true, bool configured = true)
{
    drive::State s;
    s.armed = armed;
    s.configured = configured;
    s.self = configured ? 2 : 0;
    s.base = configured ? 40000 : 0;
    s.frontend = true;
    s.home_screen = true;
    return s;
}

int main()
{
    auto c = drive::parse(R"({"seq": 12, "op": "host", "args": {"category": "f1-2026"}})");
    expect(c.error.empty() && c.op == drive::Op::host && c.seq == 12 && c.category == drive::Category::f1_2026,
           "host with a category parses");
    expect(drive::parse(R"({"seq":1,"op":"host","args":{"category":"f1-2025"}})").category == drive::Category::f1_2025,
           "f1-2025 parses");
    expect(!drive::parse(R"({"seq":1,"op":"host"})").error.empty(), "host without a category is an error");
    expect(drive::parse(R"({"seq":1,"op":"host","args":{"category":"f2"}})").error.find("unknown category") == 0,
           "an unknown category names itself");
    c = drive::parse(R"({"seq":3,"op":"join","args":{"ip":"10.77.0.1"}})");
    expect(c.error.empty() && c.op == drive::Op::join && c.ip == "10.77.0.1", "join with an IPv4 parses");
    expect(!drive::parse(R"({"seq":3,"op":"join","args":{"ip":"10.77.0"}})").error.empty(), "a short address is refused");
    expect(!drive::parse(R"({"seq":3,"op":"join"})").error.empty(), "join without an ip is an error");
    expect(drive::parse(R"({"seq":4,"op":"status"})").op == drive::Op::status, "status parses");
    expect(drive::parse(R"({"seq":5,"op":"leave"})").op == drive::Op::leave, "leave parses");
    expect(drive::parse(R"({"seq":5,"op":"dance"})").error == "unknown op 'dance'", "an unknown op is named");
    expect(drive::parse("[1,2]").error == "not a JSON object", "a non-object is refused");
    expect(drive::parse(R"({"op":"status"})").seq == 0, "a missing seq stays 0");

    expect(drive::valid_ipv4("192.168.1.20"), "a LAN address is valid");
    expect(drive::valid_ipv4("10.77.0.254"), "a virtual address is valid");
    expect(!drive::valid_ipv4("256.1.1.1"), "a part over 255 is invalid");
    expect(!drive::valid_ipv4("01.2.3.4"), "a leading zero is invalid");
    expect(!drive::valid_ipv4("1.2.3.4.5"), "five parts are invalid");
    expect(!drive::valid_ipv4("1.2.3.4 "), "trailing text is invalid");
    expect(!drive::valid_ipv4("255.255.255.255"), "the broadcast address is invalid");
    expect(!drive::valid_ipv4("0.0.0.0"), "the any address is invalid");
    expect(!drive::valid_ipv4("host.lan"), "a name is invalid");

    auto host = drive::parse(R"({"seq":1,"op":"host","args":{"category":"f1-2026"}})");
    auto join = drive::parse(R"({"seq":2,"op":"join","args":{"ip":"10.77.0.1"}})");
    auto leave = drive::parse(R"({"seq":3,"op":"leave"})");
    auto status = drive::parse(R"({"seq":4,"op":"status"})");
    expect(drive::refuse(host, main_menu()).empty(), "host runs from the main menu");
    expect(drive::refuse(join, main_menu()).empty(), "join runs from the main menu");
    expect(drive::refuse(host, main_menu(false)).find("not armed") != std::string::npos,
           "host is refused without SWOFTY_NETPLAY");
    expect(drive::refuse(join, main_menu(true, false)) == "no room is set; send configure first",
           "join is refused before configure");
    expect(drive::refuse(status, main_menu(false)).empty(), "status runs without SWOFTY_NETPLAY");
    drive::State sub = main_menu();
    sub.home_screen = false;
    expect(sub.screen() == drive::Screen::other, "a front end sub-screen is other");
    expect(drive::refuse(host, sub) == "the game is not on its main menu", "host is refused off the main menu");
    drive::State in_game;
    in_game.armed = true;
    in_game.home_screen = true;
    expect(in_game.screen() == drive::Screen::other, "fe_home without StateFrontend is other");
    drive::State lobby = main_menu();
    lobby.lobby = true;
    lobby.role = drive::Role::host;
    expect(lobby.screen() == drive::Screen::lan_lobby && lobby.in_session(), "StateOnlineLobby is the LAN lobby");
    expect(drive::refuse(join, lobby).find("already in a LAN session") == 0, "join is refused in a lobby");
    expect(drive::refuse(leave, lobby).empty(), "leave runs in a lobby");
    expect(drive::refuse(leave, main_menu()) == "not in a LAN session", "leave is refused outside a session");
    drive::State busy = main_menu();
    busy.busy = true;
    expect(drive::refuse(host, busy) == "a netplay command is still running", "a running command blocks the next");

    expect(drive::status_json(lobby) ==
               R"({"screen":"lan_lobby","in_session":true,"role":"host","armed":true,"configured":true,"self":2,"base":40000})",
           "status JSON in a hosted lobby");
    expect(drive::status_json(main_menu(false, false)) ==
               R"({"screen":"main_menu","in_session":false,"role":"none","armed":false,"configured":false,"self":0,"base":0})",
           "status JSON on the main menu");

    auto conf = drive::parse(R"({"seq":20,"op":"configure","args":{"self":3,"base":40000}})");
    expect(conf.error.empty() && conf.op == drive::Op::configure && conf.self == 3 && conf.base == 40000,
           "configure parses");
    expect(!drive::parse(R"({"seq":20,"op":"configure","args":{"self":0,"base":40000}})").error.empty(), "self 0 is refused");
    expect(!drive::parse(R"({"seq":20,"op":"configure","args":{"self":255,"base":40000}})").error.empty(), "self 255 is refused");
    expect(!drive::parse(R"({"seq":20,"op":"configure","args":{"self":2,"base":65282}})").error.empty(),
           "a base that leaves no room for 254 members is refused");
    expect(drive::parse(R"({"seq":20,"op":"configure","args":{"self":254,"base":65281}})").error.empty(), "the top values parse");
    expect(drive::parse(R"({"seq":20,"op":"configure","args":{"self":2,"base":21300}})").error.find("game port") != std::string::npos,
           "a base whose ports cover 21414 is refused");
    expect(drive::parse(R"({"seq":20,"op":"configure","args":{"self":2,"base":21414}})").error.empty(),
           "a base just at the game port leaves it outside the range");
    expect(!drive::parse(R"({"seq":20,"op":"configure","args":{"self":2.5,"base":40000}})").error.empty(), "a fraction is refused");
    expect(!drive::parse(R"({"seq":20,"op":"configure","args":{"self":2}})").error.empty(), "configure needs a base");
    auto named = drive::parse(R"({"seq":22,"op":"configure","args":{"self":3,"base":40000,"name":"  Swofty\u0007\u200b  One "}})");
    expect(named.error.empty() && named.name == "Swofty One", "configure carries the signed-in name, trimmed and cleaned");
    expect(drive::parse(R"({"seq":22,"op":"configure","args":{"self":3,"base":40000}})").name.empty(),
           "configure without a name keeps the game's own");
    expect(drive::parse(R"({"seq":22,"op":"configure","args":{"self":3,"base":40000,"name":null}})").error.empty(),
           "a null name is no name");
    expect(drive::parse(R"({"seq":22,"op":"configure","args":{"self":3,"base":40000,"name":7}})").error.find("args.name") == 0,
           "a name that is not a string is refused");
    expect(drive::parse(R"({"seq":22,"op":"configure","args":{"self":3,"base":40000,"name":" \u00a0\u200d "}})").name.empty(),
           "a name of only spaces and invisible characters is empty");

    expect(drive::driver_name("xSoul") == "xSoul", "a plain name is kept");
    expect(drive::driver_name("\tSloths\r\n") == "Sloths", "control characters go");
    expect(drive::driver_name("A\xe2\x80\xae" "B\xef\xbb\xbf" "C\xc2\xad" "D") == "ABCD", "bidi, BOM and soft hyphen go");
    expect(drive::driver_name("Bad\xff\xc3(Name\xed\xa0\x80") == "Bad(Name", "invalid UTF-8 and surrogates go");
    expect(drive::driver_name("\xc0\xafx") == "x", "an overlong sequence goes");
    expect(drive::driver_name("Jos\xc3\xa9 \xe3\x80\x80 \xf0\x9f\x8f\x8e") == "Jos\xc3\xa9 \xf0\x9f\x8f\x8e",
           "accents and emoji stay, wide spaces become one space");
    std::string longest(drive::DRIVER_NAME_BYTES - 1, 'a');
    expect(drive::driver_name(longest + "\xc3\xa9") == longest, "a character that would pass 127 bytes is cut whole");
    expect(drive::driver_name(longest + "b\xc3\xa9") == longest + "b", "the cut keeps 127 bytes");
    expect(drive::driver_name(std::string(126, 'a') + " b") == std::string(126, 'a'), "no trailing space after the cut");
    expect(drive::driver_name(std::string(300, 'z')).size() == drive::DRIVER_NAME_BYTES, "a long name is capped");
    expect(drive::driver_name("").empty() && drive::driver_name("\x01\x02").empty(), "nothing left is empty");

    std::string decoded;
    expect(drive::percent_decode("Sw%C3%B6fty%20%3b1", decoded) && decoded == "Sw\xc3\xb6" "fty ;1", "percent escapes decode");
    expect(!drive::percent_decode("50%", decoded) && !drive::percent_decode("%zz", decoded) && !drive::percent_decode("%4", decoded),
           "a malformed escape is refused");

    auto clear = drive::parse(R"({"seq":21,"op":"clear"})");
    expect(clear.op == drive::Op::clear && clear.error.empty(), "clear parses");
    expect(drive::refuse(conf, main_menu(true, false)).empty(), "configure runs on the main menu");
    expect(drive::refuse(conf, lobby).empty(), "configure runs in a LAN lobby");
    expect(drive::refuse(conf, sub) == "the room can be set only on the main menu or in a LAN lobby",
           "configure is refused elsewhere");
    expect(drive::refuse(conf, main_menu(false, false)).find("not armed") != std::string::npos,
           "configure needs SWOFTY_NETPLAY");
    expect(drive::refuse(clear, sub).empty() && drive::refuse(clear, main_menu(false, false)).empty(),
           "clear runs anywhere");
    std::string r = drive::reply_json(join, lobby, "already in a \"LAN\" session", 7);
    expect(r == R"({"seq":2,"op":"join","ok":false,"error":"already in a \"LAN\" session","status":{"screen":"lan_lobby","in_session":true,"role":"host","armed":true,"configured":true,"self":2,"base":40000},"t":7})",
           "a refused reply carries the escaped error and the status");
    json::Value v;
    expect(json::parse(drive::reply_json(status, main_menu(), "", 9), v) && v["ok"].type == json::Value::Bool && v["ok"].b,
           "an ok reply parses back");

    {
        drive::Job j;
        drive::Facts f;
        j.start(host, 1000);
        expect(j.active() && j.tick(f, 1000) == drive::Do::fire_local_mp, "host starts by firing local_multiplayer");
        expect(j.tick(f, 2000) == drive::Do::nothing && j.active(), "host waits for Local Multiplayer");
        f.local_mp = true;
        j.tick(f, 3000);
        expect(j.tick(f, 3016) == drive::Do::mode_switch, "then switches to LAN mode");
        f.lan_mode = true;
        j.tick(f, 3032);
        expect(j.tick(f, 3048) == drive::Do::fire_lan, "then fires lan");
        expect(!j.popup("multiplayer_lan_msg_other", 3050), "another popup is not taken");
        expect(j.popup(drive::LAN_DIALOG, 3060), "the LAN dialog is taken");
        expect(j.tick(f, 3070) == drive::Do::answer_dialog && std::string(j.button()) == "open_lan",
               "host answers Host LAN game");
        expect(!j.popup(drive::IP_PROMPT, 3080), "host takes no IP prompt");
        expect(j.tick(f, 50000) == drive::Do::nothing && j.active(), "host waits for the lobby while the player picks");
        f.lobby = true;
        j.tick(f, 60000);
        expect(j.finished() && j.error.empty() && !j.detail.empty(), "host finishes in the lobby with a detail");
    }
    {
        drive::Job j;
        drive::Facts f;
        f.local_mp = f.lan_mode = true;
        j.start(join, 0);
        j.tick(f, 0);
        j.tick(f, 1);
        j.tick(f, 2);
        j.tick(f, 3);
        j.tick(f, 4);
        expect(j.popup(drive::LAN_DIALOG, 5), "join takes the LAN dialog");
        expect(j.tick(f, 6) == drive::Do::answer_dialog && std::string(j.button()) == "join_via_ip",
               "join answers Join via IP address");
        expect(j.popup(drive::IP_PROMPT, 7), "join takes the IP prompt");
        expect(j.tick(f, 8) == drive::Do::answer_ip, "join answers the prompt");
        expect(!j.popup("lan_join_via_ip_error", 9) && j.finished() && j.error.find("lan_join_via_ip_error") != std::string::npos,
               "an error popup ends the job and is shown");
    }
    {
        drive::Job j;
        drive::Facts f;
        j.start(join, 0);
        j.tick(f, 0);
        j.tick(f, drive::OPEN_MS + 1);
        expect(j.finished() && j.error == "Local Multiplayer did not open", "a screen that never opens times out");
        j.reset();
        expect(!j.active() && !j.popup(drive::LAN_DIALOG, 0), "an idle job takes no popup");
        f.local_mp = true;
        j.start(host, 0);
        j.tick(f, 0);
        j.tick(f, 1);
        j.tick(f, 2);
        j.tick(f, 3);
        expect(j.finished() && j.error == "the game did not switch to LAN mode", "a mode switch that does not stick fails");
        drive::Job k;
        k.start(join, 0);
        k.fail("no home view model");
        expect(k.finished() && k.error == "no home view model", "an executor failure ends the job");
        drive::Job m;
        expect(!m.cancel() && !m.active() && !m.finished(), "cancel with nothing running changes nothing");
        m.start(host, 0);
        m.tick(f, 0);
        expect(m.cancel() && m.finished() && m.error == "cancelled by the launcher", "cancel ends a running host");
        expect(m.tick(f, 1) == drive::Do::nothing, "a cancelled job takes no more steps");
    }
    {
        drive::Job j;
        drive::Facts f;
        f.local_mp = f.lan_mode = true;
        j.start(host, 0);
        for (uint64_t t = 0; t < 5; t++)
            j.tick(f, t);
        expect(j.popup(drive::LAN_DIALOG, 5) && j.tick(f, 6) == drive::Do::answer_dialog, "host answers the dialog");
        expect(j.tick(f, 1000) == drive::Do::nothing && j.active(), "host waits on the Car Category screen");
        f.home = true;
        j.tick(f, 2000);
        expect(j.finished() && j.error == drive::BACK_HOME, "back on the main menu ends the wait at once");
    }
    expect(drive::reply_json(host, main_menu(), "", 1, "picked by hand").find(R"("detail":"picked by hand")") != std::string::npos,
           "a reply carries its detail");

    auto cancel = drive::parse(R"({"seq":9,"op":"cancel","args":{}})");
    expect(cancel.error.empty() && cancel.op == drive::Op::cancel, "cancel parses");
    drive::State running = main_menu();
    running.busy = true;
    expect(drive::refuse(cancel, running).empty(), "cancel runs while a host or join is running");
    expect(drive::refuse(cancel, main_menu(false, false)).empty(), "cancel runs unarmed and without a room");
    expect(!drive::refuse(drive::parse(R"({"seq":9,"op":"leave"})"), running).empty(), "leave waits for a running job");

    uint64_t seq = 0;
    expect(drive::inbox_name("1792467946485.json", &seq) && seq == 1792467946485ull, "a numeric inbox name is read");
    expect(!drive::inbox_name("12.json.tmp", &seq), "a temp file is ignored");
    expect(!drive::inbox_name("x1.json", &seq), "a non-numeric name is ignored");
    expect(!drive::inbox_name("0.json", &seq), "seq 0 is ignored");
    expect(std::string(drive::do_name(drive::Do::answer_ip)) == "enter the host IP" &&
               std::string(drive::do_name(drive::Do::mode_switch)) == "switch to LAN mode",
           "each job step has a name for loader.log");

    printf("%s\n", fails ? "FAILED" : "all ok");
    return fails ? 1 : 0;
}
