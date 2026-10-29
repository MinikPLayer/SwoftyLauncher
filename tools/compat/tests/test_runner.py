import json
import os
import stat
import sys
import tempfile
import time
import unittest
import zipfile
from pathlib import Path

from compat import compare, runner

HERE = Path(__file__).resolve().parent


def fake_cli(dir, env=None):
    script = Path(dir) / "swofty"
    exports = "".join(f"export {k}='{v}'\n" for k, v in (env or {}).items())
    script.write_text(f"#!/bin/sh\n{exports}exec '{sys.executable}' '{HERE / 'fake_swofty.py'}' \"$@\"\n")
    script.chmod(script.stat().st_mode | stat.S_IXUSR)
    return script


def addon_zip(dir, addon_id="x.addon", version="1.0.0"):
    path = Path(dir) / "downloads" / addon_id / version / "0000.swoftyaddon"
    path.parent.mkdir(parents=True)
    with zipfile.ZipFile(path, "w") as z:
        z.writestr("swofty.json", json.dumps({"id": addon_id, "version": version}))
        z.writestr("native/x.dll", b"MZ addon")
    return path


class Running(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.loader = self.root / "loader"
        self.loader.mkdir()
        (self.loader / "version.dll").write_bytes(b"MZ proxy")
        self.game = self.root / "game"
        self.game.mkdir()
        (self.game / "F1_25.exe").write_bytes(b"MZ")
        self.zip = addon_zip(self.root)

    def tearDown(self):
        self.tmp.cleanup()

    def side(self, name, env=None):
        d = self.root / f"cli-{name}"
        d.mkdir(exist_ok=True)
        return runner.Side(name, fake_cli(d, env), loader=self.loader)

    def build(self, side, work="work/s0000/data"):
        addons = {"zips": [self.zip], "enable": ["x.addon"]}
        return runner.build_set(side, addons, self.root / work, self.game, pin=False, cache=self.root)

    def test_the_same_cli_twice_reports_nothing(self):
        a, b = self.build(self.side("a")), self.build(self.side("b"))
        out = compare.compare(a, b)
        self.assertEqual(out["differences"], [])
        self.assertTrue(a["files"]["swofty/settings.json"]["normalised"])
        self.assertEqual(a["buildhash"], b["buildhash"])

    def test_install_times_do_not_reach_the_hash(self):
        a = self.build(self.side("a"))
        time.sleep(0.02)
        b = self.build(self.side("b"))
        self.assertEqual(a["buildhash"], b["buildhash"])

    def test_a_hash_that_takes_the_data_dir_path_differs_between_paths(self):
        a = self.build(self.side("a"), "work/s0000/data")
        b = self.build(self.side("b"), "work/s0001/data")
        self.assertNotEqual(a["buildhash"], b["buildhash"])

    def test_a_different_output_is_reported(self):
        a = self.build(self.side("a"))
        b = self.build(self.side("b", {"FAKE_EXTRA_EDIT": "2", "FAKE_WARNING": "x.addon: changed"}))
        cats = sorted({d["category"] for d in compare.compare(a, b)["differences"]})
        self.assertEqual(cats, ["db", "file", "warnings"])

    def test_build_time_files_explain_a_hash_difference(self):
        a = self.build(self.side("a", {"FAKE_GENERATES": "1"}))
        b = self.build(self.side("b", {"FAKE_GENERATES": "1"}))
        self.assertNotEqual(a["buildhash"], b["buildhash"])
        self.assertEqual(list(a["generated"]), ["cache/addons/x.addon/made.json"])
        out = compare.compare(a, b)
        self.assertTrue(out["same"])
        self.assertEqual(out["allowed"][0]["category"], "buildhash")

    def test_cli_version(self):
        self.assertEqual(runner.cli_version(self.side("a", {"FAKE_VERSION": "1.6.0"}).cli), "1.6.0")

    def test_a_cli_that_prints_no_version_is_a_tool_error(self):
        silent = self.root / "silent"
        silent.write_text("#!/bin/sh\nexit 0\n")
        silent.chmod(0o755)
        with self.assertRaises(runner.ToolError):
            runner.cli_version(silent)


class Generated(unittest.TestCase):
    def test_json_written_during_the_build_compares_without_key_order(self):
        from compat import snapshot

        with tempfile.TemporaryDirectory() as tmp:
            norm = snapshot.Normaliser({"data": tmp})
            out = []
            for i, text in enumerate(['{"b": 1, "a": [1, 2]}', '{"a": [1, 2], "b": 1}', '{"a": [2, 1], "b": 1}']):
                d = Path(tmp) / str(i) / "cache/addons/x"
                d.mkdir(parents=True)
                (d / "made.json").write_text(text)
                out.append(runner.generated_inputs(Path(tmp) / str(i), 0, norm)["cache/addons/x/made.json"])
            self.assertEqual(out[0], out[1])
            self.assertNotEqual(out[0], out[2])
            (Path(tmp) / "0/cache/addons/x/raw.txt").write_text("not json")
            self.assertFalse(runner.generated_inputs(Path(tmp) / "0", 0, norm)["cache/addons/x/raw.txt"].startswith("json:"))


class Files(unittest.TestCase):
    def test_pin_mtimes(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "a/b").mkdir(parents=True)
            (Path(tmp) / "a/b/c.txt").write_text("x")
            runner.pin_mtimes(tmp)
            for p in (Path(tmp), Path(tmp) / "a", Path(tmp) / "a/b", Path(tmp) / "a/b/c.txt"):
                self.assertEqual(p.stat().st_mtime_ns, runner.FIXED_MTIME_NS)

    def test_seed_baseline_links_objects_and_copies_manifests(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "data/baseline"
            (src / "25251167-x").mkdir(parents=True)
            (src / "25251167-x/baseline.json").write_text("{}")
            (src / "objects/40").mkdir(parents=True)
            (src / "objects/40/40ab").write_bytes(b"stock")
            seed = runner.seed_baseline(Path(tmp) / "data", Path(tmp) / "seed")
            self.assertEqual((seed / "objects/40/40ab").stat().st_ino, (src / "objects/40/40ab").stat().st_ino)
            self.assertNotEqual((seed / "25251167-x/baseline.json").stat().st_ino,
                                (src / "25251167-x/baseline.json").stat().st_ino)
            self.assertIsNone(runner.seed_baseline(Path(tmp) / "nothing", Path(tmp) / "seed2"))

    def test_game_state_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            (Path(tmp) / "f").write_text("a")
            before = runner.game_state(tmp)
            (Path(tmp) / "g").write_text("b")
            changed, n = runner.state_changes(before, runner.game_state(tmp))
            self.assertEqual((changed, n), (["g"], 1))

    def test_side_env_turns_off_network_reporting(self):
        os.environ["SWOFTY_DATA_DIR"] = "/should/not/leak"
        try:
            env = runner.Side("a", Path("/bin/true"), loader=Path("/l")).env()
        finally:
            del os.environ["SWOFTY_DATA_DIR"]
        self.assertNotIn("SWOFTY_DATA_DIR", env)
        self.assertEqual(env["SWOFTY_USAGE"], "0")
        self.assertEqual(env["SWOFTY_LOADER_DIR"], "/l")


if __name__ == "__main__":
    unittest.main()


class Gate(unittest.TestCase):
    def test_run_and_recompare_end_to_end(self):
        import hashlib
        import io
        from contextlib import redirect_stdout

        from compat.__main__ import main

        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            (root / "loader").mkdir()
            (root / "loader/version.dll").write_bytes(b"MZ proxy")
            (root / "game").mkdir()
            (root / "game/F1_25.exe").write_bytes(b"MZ")
            built = addon_zip(root / "src")
            data = built.read_bytes()
            sha = hashlib.sha256(data).hexdigest()
            cache = root / "cache"
            dst = cache / "downloads/x.addon/1.0.0" / f"{sha}.swoftyaddon"
            dst.parent.mkdir(parents=True)
            dst.write_bytes(data)
            catalog = {"addons": [{"id": "x.addon", "name": "X", "versions": [
                {"version": "1.0.0", "launcher": ">=1.5.1", "size": len(data), "sha256": sha, "url": "http://127.0.0.1:9/x"}]}]}
            (cache / "catalog.json").write_text(json.dumps(catalog))
            (root / "cli").mkdir()
            cli = fake_cli(root / "cli")
            out = root / "out"
            args = ["run", "--cli-a", str(cli), "--cli-b", str(cli), "--loader-a", str(root / "loader"),
                    "--loader-b", str(root / "loader"), "--game", str(root / "game"), "--cache", str(cache),
                    "--out", str(out), "--offline", "--no-r2", "--no-pin", "--baseline-from", str(root / "none"), "--min-free-gb", "0"]
            with redirect_stdout(io.StringIO()) as printed:
                self.assertEqual(main(args), 0)
            self.assertIn("PASS  x.addon@1.0.0", printed.getvalue())
            rep = json.loads((out / "report.json").read_text())
            self.assertEqual(rep["counts"], {"pass": 1, "diff": 0, "error": 0})
            self.assertFalse((out / "work").exists())
            snap = json.loads((out / "snapshots/x.addon@1.0.0.b.json").read_text())
            snap["warnings"] = ["x.addon: something new"]
            (out / "snapshots/x.addon@1.0.0.b.json").write_text(json.dumps(snap))
            with redirect_stdout(io.StringIO()) as printed:
                self.assertEqual(main(["recompare", str(out)]), 1)
            self.assertIn("warnings: warnings", printed.getvalue())
            with redirect_stdout(io.StringIO()):
                self.assertEqual(main(["recompare", str(out), "--allow", "warnings"]), 0)

    def gate(self, root, env_a, extra=()):
        import hashlib
        import io
        from contextlib import redirect_stdout

        from compat.__main__ import main

        (root / "loader").mkdir()
        (root / "loader/version.dll").write_bytes(b"MZ proxy")
        (root / "game").mkdir()
        (root / "game/F1_25.exe").write_bytes(b"MZ")
        data = addon_zip(root / "src").read_bytes()
        sha = hashlib.sha256(data).hexdigest()
        cache = root / "cache"
        (cache / "downloads/x.addon/1.0.0").mkdir(parents=True)
        (cache / "downloads/x.addon/1.0.0" / f"{sha}.swoftyaddon").write_bytes(data)
        (cache / "catalog.json").write_text(json.dumps({"addons": [{"id": "x.addon", "name": "X", "versions": [
            {"version": "1.0.0", "size": len(data), "sha256": sha, "url": ""}]}]}))
        (root / "ca").mkdir()
        (root / "cb").mkdir()
        args = ["run", "--cli-a", str(fake_cli(root / "ca", env_a)), "--cli-b", str(fake_cli(root / "cb")),
                "--loader-a", str(root / "loader"), "--loader-b", str(root / "loader"), "--game", str(root / "game"),
                "--cache", str(cache), "--out", str(root / "out"), "--offline", "--no-r2", "--no-pin",
                "--baseline-from", str(root / "none"), "--min-free-gb", "0", *extra]
        with redirect_stdout(io.StringIO()):
            code = main(args)
        return code, json.loads((root / "out/report.json").read_text())["results"][0]

    def test_a_set_is_built_again_when_the_game_folder_changes_under_it(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            code, result = self.gate(root, {"FAKE_TOUCH_GAME_ONCE": str(root / "touched")})
            self.assertEqual(code, 0)
            self.assertEqual(result["status"], "pass")
            self.assertIn("the game folder changed during attempt 1", result["notes"][0])

    def test_a_set_whose_game_folder_keeps_changing_is_inconclusive(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            code, result = self.gate(root, {"FAKE_TOUCH_GAME_ONCE": str(root / "touched")}, ["--retries", "0"])
            self.assertEqual(code, 2)
            self.assertIn("inconclusive", result["error"])

    def test_tool_errors_exit_2(self):
        import io
        from contextlib import redirect_stderr

        from compat.__main__ import main

        with tempfile.TemporaryDirectory() as tmp, redirect_stderr(io.StringIO()):
            self.assertEqual(main(["run", "--cli-a", f"{tmp}/nope", "--cli-b", f"{tmp}/nope", "--offline"]), 2)
            self.assertEqual(main(["harness", str(Path(tmp) / "nowhere")]), 2)
            silent = Path(tmp) / "silent"
            silent.write_text("#!/bin/sh\nexit 0\n")
            silent.chmod(0o755)
            self.assertEqual(main(["run", "--cli-a", str(silent), "--cli-b", str(silent), "--offline"]), 2)

    def test_an_unexpected_exception_exits_2(self):
        import io
        from contextlib import redirect_stderr
        from unittest import mock

        from compat import __main__ as cli

        with mock.patch.object(cli, "cmd_compare", side_effect=KeyError("boom")), redirect_stderr(io.StringIO()) as err:
            self.assertEqual(cli.main(["compare", "a.json", "b.json"]), 2)
        self.assertIn("KeyError", err.getvalue())

    def test_the_baseline_seed_is_reused_until_the_baseline_changes(self):
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "data/baseline"
            (src / "b1").mkdir(parents=True)
            (src / "b1/baseline.json").write_text('{"files": 1}')
            (src / "objects/aa").mkdir(parents=True)
            (src / "objects/aa/aa1").write_bytes(b"one")
            seed = runner.seed_baseline(Path(tmp) / "data", Path(tmp) / "seed")
            (src / "objects/aa/aa2").write_bytes(b"two")
            runner.seed_baseline(Path(tmp) / "data", seed)
            self.assertTrue((seed / "objects/aa/aa2").is_file())
            (src / "b1/baseline.json").write_text('{"files": 2}')
            runner.seed_baseline(Path(tmp) / "data", seed)
            self.assertEqual((seed / "b1/baseline.json").read_text(), '{"files": 2}')
