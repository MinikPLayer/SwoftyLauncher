import copy
import unittest

from compat import compare, report


def snap():
    return {
        "schema": 1,
        "steps": [{"step": "install x@1.0.0", "rc": 0, "out": ["installed x 1.0.0"]},
                  {"step": "enable x", "rc": 0, "out": ["enabled x in profile compat"]}],
        "build": {"rc": 0, "stderr": [], "stdout": ["profile compat: swofty.core -> x", "overlay 0123456789abcdef: <overlay>"]},
        "buildhash": "0123456789abcdef",
        "order": ["swofty.core", "x"],
        "files": {
            "version.dll": {"kind": "loader", "source": "<loader>/version.dll", "class": "loader", "size": 8, "sha256": "aa", "raw": "aa"},
            "swofty/addons/x/native/x.dll": {"kind": "loader", "source": "<data>/addons/x/native/x.dll", "class": "addon",
                                             "addon": "x", "size": 8, "sha256": "bb", "raw": "bb"},
            "swofty/settings.json": {"kind": "loader", "source": "<overlay>/swofty/settings.json", "class": "overlay",
                                     "size": 30, "sha256": "cc", "raw": "c1", "normalised": True},
        },
        "plan": {"tracks": [], "files": {"version.dll": {"kind": "loader", "source": "<loader>/version.dll"}}},
        "lines": ["database: unchanged", "native: 1 plugin(s)"],
        "warnings": [],
        "db": {"runtime": {"db_edits": 374}, "database": ["database: unchanged"], "assets_db_edits": 374},
        "parts": {"x": {"native": "n1"}, "swofty.core": {"Database": "d1"}},
        "conflicts": {"conflicts": [], "report": {"order": ["swofty.core", "x"]}},
        "waiting": [],
        "registry_override": True,
    }


class Comparing(unittest.TestCase):
    def test_identical_snapshots_are_the_same(self):
        out = compare.compare(snap(), snap())
        self.assertTrue(out["same"])
        self.assertEqual(out["differences"], [])

    def test_raw_bytes_differing_only_in_paths_are_the_same(self):
        b = snap()
        b["files"]["swofty/settings.json"]["raw"] = "c2"
        self.assertTrue(compare.compare(snap(), b)["same"])

    def changed(self, mutate, allow=()):
        b = snap()
        mutate(b)
        return compare.compare(snap(), b, allow=allow)

    def categories(self, out):
        return [d["category"] for d in out["differences"]]

    def test_a_changed_addon_file(self):
        out = self.changed(lambda b: b["files"]["swofty/addons/x/native/x.dll"].update(sha256="zz"))
        self.assertEqual(self.categories(out), ["file"])
        self.assertEqual(out["differences"][0]["addon"], "x")

    def test_added_and_removed_files(self):
        def mutate(b):
            del b["files"]["swofty/settings.json"]
            b["files"]["swofty/new.json"] = {"kind": "loader", "source": "<overlay>/swofty/new.json", "class": "overlay", "sha256": "dd"}
        out = self.changed(mutate)
        whats = [d["what"] for d in out["differences"]]
        self.assertEqual(whats, ["swofty/new.json: only in b", "swofty/settings.json: only in a"])

    def test_loader_files_are_their_own_category_and_can_be_allowed(self):
        out = self.changed(lambda b: b["files"]["version.dll"].update(sha256="ff"))
        self.assertEqual(self.categories(out), ["loader"])
        allowed = self.changed(lambda b: b["files"]["version.dll"].update(sha256="ff"), allow={"loader"})
        self.assertTrue(allowed["same"])
        self.assertEqual(len(allowed["allowed"]), 1)

    def test_build_hash_order_and_exit_codes(self):
        def mutate(b):
            b["buildhash"] = "fedcba9876543210"
            b["order"] = ["x", "swofty.core"]
            b["build"]["rc"] = 1
            b["steps"][1]["rc"] = 1
        self.assertEqual(sorted(self.categories(self.changed(mutate))), ["build", "buildhash", "order", "step"])

    def test_lines_warnings_and_reordering(self):
        out = self.changed(lambda b: b.update(lines=["native: 1 plugin(s)", "database: unchanged"]))
        self.assertEqual(out["differences"][0]["what"], "build lines: same entries in a different order")
        out = self.changed(lambda b: b.update(warnings=["x: new warning"]))
        self.assertEqual(out["differences"][0]["b"], ["x: new warning"])
        self.assertEqual(out["differences"][0]["a"], [])

    def test_database_counts(self):
        out = self.changed(lambda b: b["db"].update(assets_db_edits=375))
        self.assertEqual(self.categories(out), ["db"])
        self.assertEqual(out["differences"][0]["what"], "database assets_db_edits")

    def test_per_addon_parts(self):
        out = self.changed(lambda b: b["parts"]["x"].update(native="n2"))
        self.assertEqual(out["differences"][0]["what"], "x: native output differs")
        out = self.changed(lambda b: b["parts"].pop("x"))
        self.assertEqual(out["differences"][0]["what"], "x: outputs recorded on one side only")

    def test_plan_placement(self):
        out = self.changed(lambda b: b["plan"]["files"]["version.dll"].update(kind="replace"))
        self.assertEqual(self.categories(out), ["plan"])

    def test_failed_builds_compare_their_output(self):
        a, b = snap(), snap()
        for s in (a, b):
            s.update(lines=None, files={}, plan=None, buildhash=None)
            s["build"] = {"rc": 1, "stderr": ["swofty: boom"], "stdout": ["profile compat: x"]}
        self.assertTrue(compare.compare(a, b)["same"])
        b["build"]["stderr"] = ["swofty: bang"]
        self.assertEqual(self.categories(compare.compare(a, b)), ["build"])


    def test_a_hash_that_only_covers_build_time_file_stamps_is_explained(self):
        a, b = snap(), snap()
        a["generated"] = b["generated"] = {"cache/addons/x/tab/copy.json": "c0"}
        b["buildhash"] = "fedcba9876543210"
        out = compare.compare(a, b)
        self.assertTrue(out["same"])
        self.assertIn("explained", out["allowed"][0])

    def test_different_build_program_files_with_the_same_outputs_are_explained(self):
        a, b = snap(), snap()
        a["generated"] = {"cache/addons/x/tab/copy.json": "c0"}
        b["generated"] = {"cache/addons/x/tab/copy.json": "c1"}
        b["buildhash"] = "fedcba9876543210"
        out = compare.compare(a, b)
        self.assertTrue(out["same"])
        self.assertEqual(sorted(d["category"] for d in out["allowed"]), ["buildhash", "generated"])
        self.assertIn("wrote 1 file(s) differently", out["allowed"][0]["explained"])

    def test_build_program_files_are_explained_next_to_allowed_categories(self):
        a, b = snap(), snap()
        a["generated"] = {"cache/addons/x/tab/copy.json": "c0"}
        b["generated"] = {"cache/addons/x/tab/copy.json": "c1"}
        b["files"]["version.dll"]["sha256"] = "ff"
        self.assertFalse(compare.compare(a, b)["same"])
        out = compare.compare(a, b, allow={"loader"})
        self.assertTrue(out["same"])
        self.assertEqual(sorted(d["category"] for d in out["allowed"]), ["generated", "loader"])

    def test_different_build_program_files_with_different_outputs_count(self):
        a, b = snap(), snap()
        a["generated"] = {"cache/addons/x/tab/copy.json": "c0"}
        b["generated"] = {"cache/addons/x/tab/copy.json": "c1"}
        b["files"]["swofty/addons/x/native/x.dll"]["sha256"] = "zz"
        self.assertEqual(sorted(self.categories(compare.compare(a, b))), ["file", "generated"])

    def test_a_hash_with_other_differences_is_not_explained(self):
        a, b = snap(), snap()
        a["generated"] = b["generated"] = {"cache/addons/x/tab/copy.json": "c0"}
        b["buildhash"] = "fedcba9876543210"
        b["warnings"] = ["new"]
        self.assertEqual(sorted(self.categories(compare.compare(a, b))), ["buildhash", "warnings"])

    def test_without_generated_files_a_hash_difference_counts(self):
        b = snap()
        b["buildhash"] = "fedcba9876543210"
        self.assertEqual(self.categories(compare.compare(snap(), b)), ["buildhash"])


class Reporting(unittest.TestCase):
    def result(self, name, a, b, error=None):
        r = {"set": name, "comparison": compare.compare(a, b)}
        if error:
            r["error"] = error
        return r

    def test_exit_codes(self):
        same = self.result("x@1", snap(), snap())
        b = snap()
        b["buildhash"] = "other"
        diff = self.result("y@1", snap(), b)
        err = {"set": "z@1", "error": "download failed"}
        self.assertEqual(report.build({}, [copy.deepcopy(same)])["exit"], 0)
        self.assertEqual(report.build({}, [copy.deepcopy(same), copy.deepcopy(diff)])["exit"], 1)
        self.assertEqual(report.build({}, [same, diff, err])["exit"], 2)

    def test_summary_lines(self):
        b = snap()
        b["buildhash"] = "other"
        b["order"] = ["x"]
        rep = report.build({"a": {"version": "1.5.23"}, "b": {"version": "1.6.0"}},
                           [self.result("x@1", snap(), snap()), self.result("y@1", snap(), b),
                            {"set": "z@1", "error": "download failed"}])
        text = report.summary(rep)
        self.assertRegex(text, r"\nPASS  x@1\n")
        self.assertRegex(text, r"\nDIFF  y@1 +buildhash: build hash \(\+1 more\)\n")
        self.assertRegex(text, r"\nERROR z@1 +download failed\n")
        self.assertIn("1 pass, 1 with differences, 1 tool errors; exit 2", text)
        self.assertIn("\ny@1:\n  buildhash: build hash\n    a: 0123456789abcdef\n    b: other\n", text)


if __name__ == "__main__":
    unittest.main()
