import copy
import json
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from compat import __main__ as cli
from compat import harness, report

LOG = """2026-10-29 01:00:00.001 [  10] swofty loader (version.dll proxy) attached, pid 9
2026-10-29 01:00:00.002 [  10] trigger fired on thread 4 (early stage)
2026-10-29 01:00:00.003 [  10] addon swofty_core.dll: provides the mixin API v9, stage handlers
2026-10-29 01:00:00.004 [  10] addon swofty_core.dll: loaded at 0x7ffa00001000, calling SwoftyAddonInit (API v9)
2026-10-29 01:00:00.005 [  10] addon x.dll: loaded at 0x7ffa00020000, calling SwoftyAddonInit (API v9)
2026-10-29 01:00:00.006 [  10] [core] addon x.dll: registered 2 mixin(s)
2026-10-29 01:00:00.007 [  10] x: ready at 0x7ffa00020abc in 12 ms
2026-10-29 01:00:00.008 [  10] addon y.dll: loaded at 0x7ffa00030000, calling SwoftyAddonInit (API v9)
2026-10-29 01:00:00.009 [  10] [core] addon y.dll: registered 1 mixin(s)
2026-10-29 01:00:00.010 [  10] [core] addon y.dll needs wine:Missing, which does not resolve on this build
2026-10-29 01:00:00.011 [  10] addons: loading the addons without "early": true at the window stage
2026-10-29 01:00:00.012 [  10] addon z.dll: loaded at 0x7ffa00040000, API v1 plugin, SwoftyModInit runs after the mixin commit
2026-10-29 01:00:00.013 [  10] [core] commit: 2 of 3 mixin addon(s) active; 1 wrap(s)
2026-10-29 01:00:00.014 [  10] [core] wrap Game_Handle at 0x7ff600001000 (base+0x1000) installed: y.dll(10) > x.dll(0, overwrite)
2026-10-29 01:00:00.015 [  10] [core] addon w.dll dropped: installing its wrap on Game_Other failed; none of its mixins install
"""


def parsed(text=LOG):
    return harness.parse_log(text)


class Parsing(unittest.TestCase):
    def test_dlls_in_order_with_stage(self):
        p = parsed()
        self.assertEqual([(d["dll"], d["stage"], d["api"]) for d in p["dlls"]], [
            ("swofty_core.dll", "early", "v2+"), ("x.dll", "early", "v2+"), ("y.dll", "early", "v2+"),
            ("z.dll", "window", "v1")])

    def test_chain_order_priority_and_overwrite(self):
        p = parsed()
        self.assertEqual(p["chains"]["wrap Game_Handle"], [
            {"addon": "y.dll", "priority": 10, "overwrite": False},
            {"addon": "x.dll", "priority": 0, "overwrite": True}])

    def test_drops_registrations_and_unresolved(self):
        p = parsed()
        self.assertEqual(p["drops"], ["w.dll: installing its wrap on Game_Other failed; none of its mixins install"])
        self.assertEqual(p["registered"], {"x.dll": 2, "y.dll": 1})
        self.assertEqual(list(p["unresolved"]), ["y.dll"])

    def test_start_lines_are_normalised_and_attributed(self):
        p = parsed()
        self.assertEqual(p["start"]["x.dll"], ["x: ready at <ptr> in <ms>"])

    def test_timestamps_thread_ids_pointers_and_paths_are_taken_out(self):
        a = harness.parse_log("2026-10-29 01:00:00.005 [  10] addon x.dll: loaded at 0x7ffa00020000, calling SwoftyAddonInit (API v9)\n"
                              "2026-10-29 01:00:00.006 [  10] x reads Z:\\tmp\\g1\\swofty\\a.json\n", ["/tmp/g1"])
        b = harness.parse_log("2026-10-29 02:00:09.995 [ 777] addon x.dll: loaded at 0x7ffb00021000, calling SwoftyAddonInit (API v9)\n"
                              "2026-10-29 02:00:09.996 [ 777] x reads Z:\\tmp\\g2\\swofty\\a.json\n", ["/tmp/g2"])
        self.assertEqual(a, b)


class Crashes(unittest.TestCase):
    def test_a_crash_line_is_the_same_apart_from_names_times_and_loader_offsets(self):
        one = ("[crash] crash report swofty\\crash-20261029-052647.txt: unhandled ACCESS_VIOLATION reading 0x10 at x.dll+0x1f23; "
               "stack x.dll+0x1f23 .text fn +0x1f00+0x23 < VERSION.dll+0x6005 .text fn +0x5fe0+0x25; written in 0.1 s")
        two = one.replace("052647", "052655").replace("0x6005", "0x65c5").replace("+0x5fe0+0x25", "+0x65a0+0x25")
        self.assertEqual(harness.clean(one), harness.clean(two))
        self.assertIn("x.dll+0x1f23", harness.clean(one))


class Comparing(unittest.TestCase):
    def pair(self, mutate):
        b = parsed()
        mutate(b)
        return harness.compare_logs(parsed(), b)

    def test_the_same_log_is_the_same(self):
        out = harness.compare_logs(parsed(), parsed())
        self.assertTrue(out["same"])
        self.assertEqual(out["allowed"], [])

    def test_a_missing_dll_is_a_difference(self):
        out = self.pair(lambda b: b["dlls"].pop())
        self.assertEqual([d["category"] for d in out["differences"]], ["dll"])

    def test_a_dll_loading_in_a_later_stage_is_a_difference(self):
        out = self.pair(lambda b: b["dlls"][1].update(stage="window"))
        self.assertEqual([d["category"] for d in out["differences"]], ["dll"])

    def test_the_new_core_dll_alone_is_not_a_difference(self):
        a = parsed()
        a["dlls"] = [d for d in a["dlls"] if d["dll"] != harness.CORE_DLL]
        out = harness.compare_logs(a, parsed())
        self.assertTrue(out["same"])

    def test_a_new_drop_on_b(self):
        out = self.pair(lambda b: b["drops"].append("x.dll: dropped"))
        self.assertEqual([d["category"] for d in out["differences"]], ["drop"])

    def test_a_drop_only_on_a_still_shows(self):
        a = parsed()
        b = parsed()
        b["drops"] = []
        out = harness.compare_logs(a, b)
        self.assertEqual([d["category"] for d in out["differences"]], ["drop"])

    def test_a_reordered_chain_is_a_difference(self):
        out = self.pair(lambda b: b["chains"]["wrap Game_Handle"].reverse())
        self.assertEqual([d["category"] for d in out["differences"]], ["chain"])
        self.assertIn("Game_Handle", out["differences"][0]["what"])

    def test_a_changed_priority_is_a_difference(self):
        out = self.pair(lambda b: b["chains"]["wrap Game_Handle"][0].update(priority=5))
        self.assertEqual([d["category"] for d in out["differences"]], ["chain"])

    def test_an_addon_missing_from_a_chain_is_a_difference(self):
        out = self.pair(lambda b: b["chains"]["wrap Game_Handle"].pop())
        self.assertEqual([d["category"] for d in out["differences"]], ["chain"])

    def core(self):
        return {"addon": harness.CORE_DLL, "priority": 0, "overwrite": False}

    def test_a_core_handler_joining_a_chain_is_expected(self):
        out = self.pair(lambda b: b["chains"]["wrap Game_Handle"].insert(1, self.core()))
        self.assertTrue(out["same"])
        self.assertEqual(len(out["allowed"]), 1)
        self.assertIn("hook point subscriber", out["allowed"][0]["explained"])

    def test_a_chain_only_the_core_installs_is_expected(self):
        out = self.pair(lambda b: b["chains"].update({"wrap Core_Only": [self.core()]}))
        self.assertTrue(out["same"])
        self.assertEqual(len(out["allowed"]), 1)

    def test_a_new_chain_with_an_addon_is_not_expected(self):
        out = self.pair(lambda b: b["chains"].update({"wrap New": [{"addon": "x.dll", "priority": 0, "overwrite": False}]}))
        self.assertEqual([d["category"] for d in out["differences"]], ["chain"])

    def test_a_chain_that_vanishes_is_not_expected(self):
        out = self.pair(lambda b: b["chains"].pop("wrap Game_Handle"))
        self.assertEqual([d["category"] for d in out["differences"]], ["chain"])

    def test_the_conflict_pass(self):
        out = self.pair(lambda b: b["conflicts"].append("[core] conflict: two overwrites on T: x.dll, y.dll"))
        self.assertEqual([d["category"] for d in out["differences"]], ["conflict"])

    def test_unresolved_and_registered_and_start(self):
        def mutate(b):
            b["unresolved"]["y.dll"].append("needs more")
            b["registered"]["x.dll"] = 3
            b["start"]["x.dll"].append("x: extra line")
        out = self.pair(mutate)
        self.assertEqual(sorted(d["category"] for d in out["differences"]), ["register", "start", "unresolved"])

    def test_registry_notes_added_on_b_are_expected_but_other_new_lines_are_not(self):
        def mutate(b):
            b["start"]["x.dll"].insert(0, "[registry] career.content x/c: provided by x (career::content::add)")
        out = self.pair(mutate)
        self.assertTrue(out["same"])
        self.assertEqual(len(out["allowed"]), 1)
        self.assertIn("registry notes", out["allowed"][0]["explained"])

        def mutate_more(b):
            mutate(b)
            b["start"]["x.dll"].append("x: something else")
        out = self.pair(mutate_more)
        self.assertEqual([d["category"] for d in out["differences"]], ["start"])
        self.assertEqual(out["differences"][0]["b"], ["x: something else"])

    def test_allow_moves_a_category_to_allowed(self):
        b = parsed()
        b["chains"]["wrap Game_Handle"].reverse()
        out = harness.compare_logs(parsed(), b, allow={"chain"})
        self.assertTrue(out["same"])
        self.assertEqual(len(out["allowed"]), 1)


class FakeClock:
    def __init__(self):
        self.t = 0.0

    def now(self):
        return self.t

    def sleep(self, n):
        self.t += n


class Locking(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.root = Path(self.dir.name)
        self.lock_path = self.root / "lock.d"
        self.queue = self.root / "queue"
        self.clock = FakeClock()

    def tearDown(self):
        self.dir.cleanup()

    def lock(self, wait=60, hold=540):
        return harness.Lock(self.lock_path, self.queue, "me", wait, hold, self.clock.sleep, self.clock.now)

    def test_takes_a_free_lock_and_names_the_owner(self):
        lk = self.lock()
        lk.take()
        self.assertIn("gate2 wine", (self.lock_path / "owner").read_text())
        lk.release()
        self.assertFalse(self.lock_path.exists())

    def test_a_held_lock_times_out_and_leaves_it_alone(self):
        self.lock_path.mkdir()
        (self.lock_path / "owner").write_text("swofty playing\n")
        lk = self.lock(wait=20)
        with self.assertRaises(harness.LockWait) as e:
            lk.take()
        self.assertIn("swofty playing", str(e.exception))
        self.assertTrue(self.lock_path.exists())
        lk.release()
        self.assertTrue(self.lock_path.exists())

    def test_a_queue_line_makes_it_wait_even_when_free(self):
        self.queue.write_text("other 02:42\n")
        lk = self.lock(wait=20)
        with self.assertRaises(harness.LockWait):
            lk.take()
        self.assertFalse(self.lock_path.exists())
        self.assertEqual(self.queue.read_text(), "other 02:42\n")

    def test_it_queues_behind_others_and_goes_when_its_turn_comes(self):
        self.queue.write_text("other 02:42\n")
        lk = self.lock(wait=600)
        steps = []
        real_sleep = self.clock.sleep

        def sleep(n):
            steps.append(self.queue.read_text())
            if len(steps) == 2:
                self.queue.write_text("".join(l for l in self.queue.read_text().splitlines(True) if not l.startswith("other")))
            real_sleep(n)

        lk.sleep = sleep
        lk.take()
        self.assertTrue(any(l.startswith("std16-gate2") or l.startswith("me ") for l in steps[0].splitlines()))
        self.assertTrue(self.lock_path.exists())
        self.assertEqual(self.queue.read_text().strip(), "")

    def test_hold_expires(self):
        lk = self.lock(hold=540)
        lk.take()
        self.assertFalse(lk.expired())
        self.clock.t += 541
        self.assertTrue(lk.expired())

    def test_no_lock_folder_means_no_lock(self):
        lk = harness.Lock(self.root / "missing" / "lock.d", self.queue, "me", 5, 5, self.clock.sleep, self.clock.now)
        lk.take()
        lk.release()


def plan_tree(root, hash_, *, chain_extra=""):
    data = Path(root) / "data"
    src = data / "addons" / "x" / "native"
    src.mkdir(parents=True)
    (src / "x.dll").write_bytes(b"MZx")
    (data / "big.bin").write_bytes(b"0" * 100)
    plan = {"replace": {"data/big.bin": str(data / "big.bin")}, "add": {},
            "loader": {"swofty/addons/x/native/x.dll": str(src / "x.dll"),
                       "version.dll": str(data / "addons" / "x" / "native" / "x.dll")}}
    d = data / "cache" / "overlay" / harness.PROFILE / hash_
    d.mkdir(parents=True)
    (d / "plan.json").write_text(json.dumps(plan))
    return data


class Preparing(unittest.TestCase):
    def test_copies_every_plan_file_and_the_exe_and_remaps_sources(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            plan_tree(t / "out" / "a" / "s0000", "h1")
            data, plan = harness.load_plan(t / "out", "a", 0, "h1")
            exe = t / "F1_25.exe"
            exe.write_bytes(b"MZexe")
            work_data = str(t / "out" / "work" / "s0000" / "data")
            plan = json.loads(json.dumps(plan).replace(str(data), work_data))
            mapping = harness.side_mapping(t / "out", "a", 0)
            info = harness.prepare_game(plan, mapping, t / "game", exe)
            self.assertEqual(info["missing"], [])
            self.assertEqual((t / "game" / "swofty/addons/x/native/x.dll").read_bytes(), b"MZx")
            self.assertTrue((t / "game" / "F1_25.exe").is_file())
            self.assertFalse((t / "game" / "data" / "big.bin").is_symlink())

    def test_the_exported_session_settings_become_swofty_settings_json(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            data = plan_tree(t / "out" / "a" / "s0000", "h1")
            sp = harness.settings_path(data, None, "h1")
            sp.parent.mkdir(parents=True)
            sp.write_text('{"plugins": ["x"]}')
            _, plan = harness.load_plan(t / "out", "a", 0, "h1")
            exe = t / "F1_25.exe"
            exe.write_bytes(b"MZ")
            harness.prepare_game(plan, [], t / "game", exe, settings=sp)
            self.assertEqual(json.loads((t / "game" / "swofty" / "settings.json").read_text()), {"plugins": ["x"]})

    def test_big_non_loader_files_are_skipped_and_missing_ones_listed(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            data = plan_tree(t / "out" / "a" / "s0000", "h1")
            _, plan = harness.load_plan(t / "out", "a", 0, "h1")
            plan["loader"]["swofty/gone.json"] = str(data / "nope")
            exe = t / "F1_25.exe"
            exe.write_bytes(b"MZ")
            info = harness.prepare_game(plan, [], t / "game", exe, max_copy=10)
            self.assertEqual(info["skipped"], ["data/big.bin"])
            self.assertEqual(info["missing"], ["swofty/gone.json"])


class Synthesising(unittest.TestCase):
    def doc(self):
        return {"schema": 1, "bindings": [
            {"id": "F", "kind": "func", "section": ".xtls", "pattern": "41 56", "anchor": 3, "resolve": {"kind": "riprel"},
             "expect": None, "rva": "0x10", "match": "0x10"},
            {"id": "S", "kind": "site", "section": ".xtls", "pattern": "41 56", "anchor": 3, "resolve": {"kind": "addr"},
             "expect": None, "rva": "0x20"},
            {"id": "D", "kind": "data", "section": ".xtls", "pattern": "41 56", "anchor": 0, "resolve": {"kind": "addr"}}]}

    def test_func_and_site_bindings_point_at_their_own_stub_and_others_are_left_alone(self):
        with tempfile.TemporaryDirectory() as t:
            core = Path(t) / "swofty" / "addons" / "core"
            core.mkdir(parents=True)
            (core / "bindings.json").write_text(json.dumps(self.doc()))
            self.assertEqual(harness.synth_bindings(t), 2)
            f, s, d = json.loads((core / "bindings.json").read_text())["bindings"]
            self.assertEqual(f["pattern"], "48 B8 " + " ".join(f"{x:02X}" for x in harness.stub_tag("F")))
            self.assertEqual((f["section"], f["anchor"], f["resolve"], f["expect"]), (".text", 0, {"kind": "addr"}, None))
            self.assertNotIn("rva", f)
            self.assertEqual((s["anchor"], s["expect"]), (10, harness.SITE_EXPECT))
            self.assertEqual(d["pattern"], "41 56")

    def test_the_same_input_gives_the_same_stubs_on_both_sides(self):
        self.assertEqual(harness.stub_source({"b", "a"}), harness.stub_source({"a", "b"}))
        self.assertNotEqual(harness.stub_tag("a"), harness.stub_tag("b"))

    def test_ids_are_the_union_of_the_func_and_site_bindings(self):
        with tempfile.TemporaryDirectory() as t:
            a, b = Path(t) / "a.json", Path(t) / "b.json"
            a.write_text(json.dumps(self.doc()))
            other = self.doc()
            other["bindings"].append({"id": "N", "kind": "site"})
            b.write_text(json.dumps(other))
            self.assertEqual(harness.synth_ids([a, b, Path(t) / "missing"]), {"F", "S", "N"})


class Slimming(unittest.TestCase):
    def test_keeps_the_plan_loader_files_and_small_game_files_only(self):
        with tempfile.TemporaryDirectory() as t:
            t = Path(t)
            data = plan_tree(t / "out" / "a" / "s0000", "h1")
            (data / "addons" / "x" / "junk.bin").write_bytes(b"j" * 50)
            (data / "baseline").mkdir()
            (data / "baseline" / "o.bin").write_bytes(b"o")
            mapping = [(str(t / "work" / "data"), str(data))]
            plan_path = data / "cache" / "overlay" / harness.PROFILE / "h1" / "plan.json"
            plan = json.loads(plan_path.read_text())
            plan = json.loads(json.dumps(plan).replace(str(data), str(t / "work" / "data")))
            plan_path.write_text(json.dumps(plan))
            sp = harness.settings_path(data, None, "h1")
            sp.parent.mkdir(parents=True)
            sp.write_text("{}")
            harness.slim_data(data, "h1", mapping, cap=10)
            self.assertTrue(sp.is_file())
            self.assertTrue((data / "addons" / "x" / "native" / "x.dll").is_file())
            self.assertTrue(plan_path.is_file())
            self.assertFalse((data / "big.bin").exists())
            self.assertFalse((data / "addons" / "x" / "junk.bin").exists())
            self.assertFalse((data / "baseline").exists())


class Running(unittest.TestCase):
    def kept_run(self, root, sets):
        results = []
        for i, (name, status) in enumerate(sets):
            hashes = {}
            for side in ("a", "b"):
                if status == "pass":
                    plan_tree(root / side / f"s{i:04d}", f"h{i}{side}")
                hashes[side] = f"h{i}{side}"
            results.append({"set": name, "addons": ["x@1"], "notes": [], "status": status, "buildhash": hashes})
        (root / "report.json").write_text(json.dumps({"meta": {"a": {"version": "1.5"}, "b": {"version": "1.6"}},
                                                      "results": results}))

    def run_cli(self, root, texts, extra=()):
        calls = iter(texts)
        exe = root / "fake.exe"
        exe.write_bytes(b"MZ")

        def fake(game, prefix, mode, timeout, pin=True):
            text = next(calls)
            return 0, text, "out"

        argv = ["harness", str(root), "--fake-exe", str(exe), "--lock", str(root / "nolock" / "l.d"),
                "--queue", str(root / "nolock" / "q"), *extra]
        with mock.patch.object(harness, "run_wine", fake):
            return cli.main(argv)

    def test_same_logs_exit_zero_and_skip_sets_that_failed_the_build(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            self.kept_run(root, [("x@1", "pass"), ("y@1", "diff")])
            self.assertEqual(self.run_cli(root, [LOG, LOG]), 0)
            rep = json.loads((root / "harness" / "report.json").read_text())
            self.assertEqual(rep["counts"]["pass"], 1)
            self.assertEqual(rep["counts"]["skip"], 1)
            self.assertIn("SKIP  y@1", (root / "harness" / "summary.txt").read_text())

    def test_a_changed_chain_exits_one(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            self.kept_run(root, [("x@1", "pass")])
            changed = LOG.replace("y.dll(10) > x.dll(0, overwrite)", "x.dll(0, overwrite) > y.dll(10)")
            self.assertEqual(self.run_cli(root, [LOG, changed]), 1)
            self.assertIn("DIFF  x@1", (root / "harness" / "summary.txt").read_text())

    def test_a_missing_loader_log_is_a_tool_error(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            self.kept_run(root, [("x@1", "pass")])
            self.assertEqual(self.run_cli(root, ["", ""]), 2)

    def test_a_held_lock_exits_three(self):
        with tempfile.TemporaryDirectory() as t:
            root = Path(t)
            self.kept_run(root, [("x@1", "pass")])
            (root / "lock.d").mkdir()
            exe = root / "fake.exe"
            exe.write_bytes(b"MZ")
            argv = ["harness", str(root), "--fake-exe", str(exe), "--lock", str(root / "lock.d"),
                    "--queue", str(root / "q"), "--lock-wait", "0"]
            with mock.patch.object(harness, "run_wine", side_effect=AssertionError("must not run")):
                self.assertEqual(cli.main(argv), 3)

    def test_no_kept_run_is_a_tool_error(self):
        with tempfile.TemporaryDirectory() as t:
            self.assertEqual(cli.main(["harness", t]), 2)


if __name__ == "__main__":
    unittest.main()
