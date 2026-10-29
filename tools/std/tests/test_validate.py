import io
import json
import shutil
import unittest
from contextlib import redirect_stdout
from pathlib import Path

from helpers import FIXTURES, TempRegistries

import stdschema as S
import validate as V


def errors(schema, value, patch=False):
    return S.Validator(schema).errors(value, patch=patch)


class Subset(unittest.TestCase):
    def test_types(self):
        self.assertEqual(errors({"type": "integer"}, 3), [])
        self.assertEqual(errors({"type": "integer"}, 3.0), [])
        self.assertTrue(errors({"type": "integer"}, 3.5))
        self.assertTrue(errors({"type": "integer"}, True))
        self.assertTrue(errors({"type": "number"}, False))
        self.assertEqual(errors({"type": "number"}, 3), [])
        self.assertEqual(errors({"type": ["string", "null"]}, None), [])
        self.assertTrue(errors({"type": "string"}, None))

    def test_enum_and_const(self):
        self.assertEqual(errors({"enum": [1, "a"]}, 1.0), [])
        self.assertTrue(errors({"enum": [1]}, True))
        self.assertTrue(errors({"const": "a"}, "b"))

    def test_ranges(self):
        s = {"type": "number", "minimum": 1, "maximum": 3, "exclusiveMaximum": 3}
        self.assertEqual(errors(s, 2), [])
        self.assertTrue(errors(s, 0))
        self.assertTrue(errors(s, 3))

    def test_strings(self):
        s = {"type": "string", "minLength": 2, "maxLength": 3, "pattern": "^[a-z]+$"}
        self.assertEqual(errors(s, "ab"), [])
        self.assertTrue(errors(s, "a"))
        self.assertTrue(errors(s, "abcd"))
        self.assertTrue(errors(s, "AB"))

    def test_objects(self):
        s = {"type": "object", "required": ["a"], "properties": {"a": {"type": "integer"}}, "additionalProperties": False}
        self.assertEqual(errors(s, {"a": 1}), [])
        self.assertEqual(errors(s, {}), [("/a", "is required")])
        self.assertEqual(errors(s, {"a": 1, "b": 2}), [("/b", "is not a field of this record")])
        self.assertEqual(errors({"type": "object"}, {"anything": [1]}), [])

    def test_arrays(self):
        s = {"type": "array", "items": {"type": "integer"}, "minItems": 1, "maxItems": 2, "uniqueItems": True}
        self.assertEqual(errors(s, [1, 2]), [])
        self.assertTrue(errors(s, []))
        self.assertTrue(errors(s, [1, 2, 3]))
        self.assertTrue(errors(s, [1, 1]))
        self.assertEqual(errors(s, ["x"])[0][0], "/0")

    def test_combinators(self):
        self.assertEqual(errors({"anyOf": [{"type": "string"}, {"type": "integer"}]}, 1), [])
        self.assertTrue(errors({"anyOf": [{"type": "string"}, {"type": "integer"}]}, None))
        self.assertTrue(errors({"oneOf": [{"type": "number"}, {"type": "integer"}]}, 1))
        self.assertTrue(errors({"allOf": [{"minimum": 1}, {"maximum": 0}]}, 1))

    def test_refs(self):
        s = {"$defs": {"n": {"type": "integer"}}, "type": "object", "properties": {"a": {"$ref": "#/$defs/n"}}}
        self.assertEqual(errors(s, {"a": 1}), [])
        self.assertTrue(errors(s, {"a": "x"}))
        self.assertIn("schema error", errors({"$ref": "#/nope"}, 1)[0][1])

    def test_record_refs(self):
        plain = {"type": "string", "x-ref": "driver.identity"}
        for value in ("game/lewis_hamilton", "swofty.classic-seasons-2017/f1_2017", "venue/sochi?", "venue/sepang|venue/bahrain",
                      "venue/a|venue/b?"):
            self.assertEqual(errors(plain, value), [], value)
        for value in ("lewis_hamilton", "game/", "/x", "game/a b", "game/a?|game/b", "game/*", "Game/x"):
            self.assertTrue(errors(plain, value), value)
        strict = dict(plain, **{"x-missing": {"default": "required"}})
        self.assertEqual(errors(strict, "game/a"), [])
        self.assertTrue(errors(strict, "game/a?"))
        self.assertTrue(errors(strict, "game/a|game/b"))
        fallback = dict(plain, **{"x-missing": {"default": "fallback", "to": "/donor"}})
        self.assertEqual(errors(fallback, "game/a|game/b"), [])
        self.assertTrue(errors({"x-ref": "driver.identity"}, 3))

    def test_keyed(self):
        s = {"type": "array", "x-key": ["round", "slot"],
             "items": {"type": "object", "required": ["round", "slot", "kind"],
                       "properties": {"round": {"type": "integer"}, "slot": {"type": "integer"}, "kind": {"type": "string"}}}}
        self.assertEqual(errors(s, [{"round": 1, "slot": 1, "kind": "a"}, {"round": 1, "slot": 2, "kind": "a"}]), [])
        self.assertIn("repeats key", errors(s, [{"round": 1, "slot": 1, "kind": "a"}, {"round": 1, "slot": 1.0, "kind": "b"}])[0][1])
        self.assertIn("no key field slot", errors(s, [{"round": 1, "kind": "a"}])[0][1])
        self.assertIn("holds objects", errors(s, [3])[0][1])
        self.assertEqual(errors(s, [{"round": 1, "slot": 1}], patch=True), [])
        self.assertTrue(errors(s, [{"round": 1, "slot": 1}]))
        self.assertEqual(errors(s, [{"round": 1, "slot": 1, "$delete": True}], patch=True), [])
        self.assertTrue(errors(s, [{"round": 1, "slot": 1, "$delete": False}], patch=True))
        self.assertTrue(errors(s, [{"round": 1, "slot": 1, "kind": "x", "$delete": True}], patch=True))
        self.assertTrue(errors(s, [{"round": 1, "slot": 1, "kind": "x", "$delete": True}]))

    def test_patch(self):
        s = {"type": "object", "required": ["a"], "properties": {"a": {"type": "integer"}, "b": {"type": "integer"},
             "c": {"type": "array", "maxItems": 1, "items": {"type": "object", "required": ["x"]}}}}
        self.assertEqual(errors(s, {"b": 1}, patch=True), [])
        self.assertEqual(errors(s, {"b": None}, patch=True), [])
        self.assertEqual(errors(s, {"a": None}, patch=True), [("/a", "null removes a required field")])
        self.assertTrue(errors(s, {"c": [{}]}, patch=True), "a list that isn't keyed is replaced whole")
        self.assertTrue(errors(s, {"c": [{"x": 1}, {"x": 2}]}, patch=True))


class Merge(unittest.TestCase):
    SCHEMA = {"type": "object", "properties": {
        "teams": {"type": "array", "x-key": "slot", "items": {"$ref": "#/$defs/t"}},
        "seats": {"type": "array"}, "rules": {"type": "object"}},
        "$defs": {"t": {"type": "object", "required": ["slot"], "properties": {"slot": {"type": "integer"}, "n": {"type": "object"}}}}}

    def merge(self, base, patch):
        return S.merge_patch(self.SCHEMA, self.SCHEMA, base, patch)

    def test_rfc7396(self):
        self.assertEqual(self.merge({"a": 1, "b": {"c": 1, "d": 2}}, {"a": None, "b": {"c": 3}, "e": 4}),
                         {"b": {"c": 3, "d": 2}, "e": 4})

    def test_lists_without_key_are_replaced(self):
        self.assertEqual(self.merge({"seats": [1, 2]}, {"seats": [3]}), {"seats": [3]})

    def test_keyed_lists_merge_by_key(self):
        base = {"teams": [{"slot": 1, "n": {"a": 1, "b": 2}}, {"slot": 2, "n": {}}]}
        patch = {"teams": [{"slot": 1, "n": {"b": None, "c": 3}}, {"slot": 2, "$delete": True}, {"slot": 9, "n": None}]}
        self.assertEqual(self.merge(base, patch), {"teams": [{"slot": 1, "n": {"a": 1, "c": 3}}, {"slot": 9}]})

    def test_empty_keyed_list_changes_nothing(self):
        base = {"teams": [{"slot": 1}, {"slot": 2}]}
        self.assertEqual(self.merge(base, {"teams": []}), base)
        self.assertEqual(self.merge({}, {"teams": []}), {"teams": []})

    def test_element_without_its_key_is_an_error(self):
        base = {"teams": [{"slot": 1}]}
        for element in ({"n": {}}, {"slot": None}, {"slot": [1]}, 3):
            with self.assertRaises(S.MergeError, msg=element):
                self.merge(base, {"teams": [{"slot": 2}, element]})
        self.assertEqual(base, {"teams": [{"slot": 1}]}, "a failed merge changes nothing")

    def test_delete(self):
        base = {"teams": [{"slot": 1}, {"slot": 2}]}
        self.assertEqual(self.merge(base, {"teams": [{"slot": 1, "$delete": True}, {"slot": 7, "$delete": True}]}),
                         {"teams": [{"slot": 2}]})
        self.assertEqual(self.merge(base, {"teams": [{"slot": 1, "$delete": False, "n": {}}]}),
                         {"teams": [{"slot": 1, "n": {}}, {"slot": 2}]})
        with self.assertRaises(S.MergeError):
            self.merge(base, {"teams": [{"slot": 1, "$delete": "yes"}]})

    def test_lists_without_key_replace_whole(self):
        self.assertEqual(self.merge({"seats": [1, 2]}, {"seats": []}), {"seats": []})
        self.assertEqual(self.merge({"seats": [1, 2]}, {"seats": [{"a": 1}]}), {"seats": [{"a": 1}]})

    def test_keys_with_slashes(self):
        for key in ("2017/game/lewis_hamilton", "f1_2017/monaco", "f1_1990/r01", "v10_screamer", "2017"):
            self.assertTrue(S.valid_key(key), key)
        for key in ("", "/x", "x/", "a//b", "a b", "a?", "a|b", "*", "$select"):
            self.assertFalse(S.valid_key(key), key)


class Variants(TempRegistries):
    def data(self, doc, addon=None, files=None):
        paths = []
        for name, content in (files or {"seasons.json": doc}).items():
            f = self.tmp / name
            f.write_text(json.dumps(content))
            paths.append(str(f))
        out = io.StringIO()
        with redirect_stdout(out):
            code = V.main(["--repo", str(self.tmp), "--registries", "registries", "--data",
                           *(["--addon", addon] if addon else []), *paths])
        return code, out.getvalue()

    def base(self):
        return {"data_group": "g", "year": 2017, "teams": [{"slot": 1, "seats": ["game/a"]}]}

    def test_variant_needs_no_base_fields(self):
        code, out = self.data({"career.season": {"v": {"extends": "game/f1_2025", "teams": [{"slot": 2}]}}})
        self.assertEqual(code, 0, out)

    def test_variant_patch_is_still_checked(self):
        code, out = self.data({"career.season": {"v": {"extends": "game/f1_2025", "year": "x"}}})
        self.assertEqual(code, 1)
        self.assertIn("/career.season/v/year", out)

    def test_variant_of_a_base_in_the_same_file(self):
        doc = {"career.season": {"b": self.base(), "v": {"extends": "mod.a/b", "teams": [{"slot": 2}]}}}
        code, out = self.data(doc, addon="mod.a")
        self.assertEqual(code, 1)
        self.assertIn("/career.season/v/teams/1/seats: is required (merged onto mod.a/b)", out)
        doc["career.season"]["v"]["teams"][0]["seats"] = []
        self.assertEqual(self.data(doc, addon="mod.a")[0], 0)

    def test_variant_of_a_base_in_another_file(self):
        files = {"a.json": {"career.season": {"b": self.base()}},
                 "b.json": {"career.season": {"v": {"extends": "mod.a/b", "teams": [{"slot": 1, "$delete": True}, {"slot": 2}]}}}}
        code, out = self.data(None, addon="mod.a", files=files)
        self.assertEqual(code, 1)
        self.assertIn("merged onto mod.a/b", out)

    def test_variant_of_another_addon_is_only_a_patch(self):
        doc = {"career.season": {"b": self.base(), "v": {"extends": "mod.other/b", "teams": [{"slot": 2}]}}}
        self.assertEqual(self.data(doc, addon="mod.a")[0], 0)

    def test_variant_loop(self):
        doc = {"career.season": {"a": {"extends": "mod.a/b"}, "b": {"extends": "mod.a/a"}}}
        code, out = self.data(doc, addon="mod.a")
        self.assertEqual(code, 1)
        self.assertIn("extends loops", out)

    def test_variant_of_variant(self):
        doc = {"career.season": {"b": self.base(), "v1": {"extends": "mod.a/b", "year": 2018},
                                 "v2": {"extends": "mod.a/v1", "data_group": None}}}
        code, out = self.data(doc, addon="mod.a")
        self.assertEqual(code, 1)
        self.assertIn("/career.season/v2/data_group: null removes a required field", out)

    def test_slash_keys_in_records(self):
        doc = {"career.season": {"2017/game/lewis_hamilton": self.base(), "2017 x": self.base()}}
        code, out = self.data(doc)
        self.assertEqual(code, 1)
        self.assertNotIn("lewis_hamilton' is not a record key", out)
        self.assertIn("'2017 x' is not a record key", out)

    def test_example_variant_found_by_key(self):
        entries = {Path(e["file"]).name: e for e in V.run_examples(self.root, "registries")}
        self.assertTrue(entries["variants.json"]["ok"])
        self.assertIn("merged onto swofty.progressive/what_if_2017",
                      "\n".join(V.describe(entries["variant-merged-missing-seats.json"])))


class Examples(TempRegistries):
    def run_examples(self):
        return V.run_examples(self.root, "registries")

    def test_fixture_examples(self):
        entries = self.run_examples()
        self.assertTrue(entries)
        self.assertEqual([e["file"] for e in entries if not e["ok"]], [])
        self.assertEqual({e["expect"] for e in entries}, {"valid", "invalid"})

    def test_kinds_and_versions(self):
        entries = {Path(e["file"]).name: e for e in self.run_examples()}
        self.assertEqual(entries["2016.json"]["kind"], "record")
        self.assertEqual(sorted(entries["2016.json"]["versions"]), ["1.0", "1.1"])
        self.assertEqual(sorted(entries["2017@2.json"]["versions"]), ["2.0"])
        self.assertEqual(sorted(entries["reserve@1.1.json"]["versions"]), ["1.1"])
        self.assertEqual(entries["records.json"]["kind"], "records")
        self.assertEqual(entries["whatif-layer.json"]["kind"], "layers")
        self.assertEqual(sorted(entries["select-layers.json"]["versions"]), ["1.0", "1.1"])
        self.assertEqual(sorted(entries["promote@1.0.json"]["versions"]), ["1.0"])

    def test_invalid_example_fails(self):
        shutil.copy(self.reg / "examples/must-fail/slot-out-of-range.json", self.reg / "examples/slot.json")
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertEqual([Path(e["file"]).name for e in bad], ["slot.json"])
        self.assertIn("12 is above the maximum 11", "\n".join(V.describe(bad[0])))

    def test_must_fail_that_passes(self):
        shutil.copy(self.reg / "examples/2016.json", self.reg / "examples/must-fail/2016.json")
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertEqual([Path(e["file"]).name for e in bad], ["2016.json"])
        self.assertEqual(bad[0]["expect"], "invalid")
        self.assertIn("accepted by 1.0, 1.1", "\n".join(V.describe(bad[0])))

    def test_must_fail_accepted_by_one_minor(self):
        shutil.copy(self.reg / "examples/must-fail/promote@1.0.json", self.reg / "examples/must-fail/promote.json")
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertEqual([e["accepted_by"] for e in bad], [["1.1"]])

    def test_missing_major(self):
        shutil.copy(self.reg / "examples/2016.json", self.reg / "examples/2016@3.json")
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertIn("career.season has no major 3", "\n".join(V.describe(bad[0])))

    def test_missing_minor_in_must_fail(self):
        shutil.copy(self.reg / "examples/must-fail/promote@1.0.json", self.reg / "examples/must-fail/promote@1.7.json")
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertEqual([Path(e["file"]).name for e in bad], ["promote@1.7.json"])

    def test_example_of_another_registry(self):
        (self.reg / "examples/other.json").write_text(json.dumps({"vehicle": {"x": {}}}))
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertEqual([Path(e["file"]).name for e in bad], ["other.json"])

    def test_not_json(self):
        (self.reg / "examples/must-fail/broken.json").write_text("{")
        bad = [e for e in self.run_examples() if not e["ok"]]
        self.assertIn("not JSON", "\n".join(V.describe(bad[0])))

    def test_json_report(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = V.main(["--repo", str(self.tmp), "--registries", "registries", "--json"])
        self.assertEqual(code, 0)
        report = json.loads(out.getvalue())
        self.assertEqual(report["failed"], 0)
        entry = next(e for e in report["examples"] if e["file"].endswith("must-fail/slot-out-of-range.json"))
        self.assertEqual(entry["versions"]["1.0"], [["/teams/0/slot", "12 is above the maximum 11"]])

    def test_cli_lines(self):
        out = io.StringIO()
        with redirect_stdout(out):
            code = V.main(["--repo", str(self.tmp), "--registries", "registries"])
        self.assertEqual(code, 0)
        self.assertIn("examples: 7 valid, 25 must-fail, 0 wrong", out.getvalue())

    def test_schema_mode(self):
        out = io.StringIO()
        with redirect_stdout(out):
            ok = V.main(["--schema", str(self.reg / "1.0.json"), str(self.reg / "examples/2016.json")])
            bad = V.main(["--schema", str(self.reg / "1.0.json"), str(self.reg / "examples/must-fail/wrong-type.json")])
        self.assertEqual((ok, bad), (0, 1))

    def test_data_mode_skips_addon_registries(self):
        f = self.tmp / "sounds.json"
        f.write_text(json.dumps({"tails.whatif-history.notes": {"a": {"x": 1}},
                                 "career.season": {"what_if": json.loads((self.reg / "examples/2016.json").read_text())}}))
        out = io.StringIO()
        with redirect_stdout(out):
            code = V.main(["--repo", str(self.tmp), "--registries", "registries", "--data", str(f)])
        self.assertEqual(code, 0, out.getvalue())
        self.assertIn("(1.0, 1.1)", out.getvalue())


class Agreement(unittest.TestCase):
    def test_every_fixture_has_a_result_per_version(self):
        for e in V.run_examples(FIXTURES / "registries", "registries"):
            self.assertTrue(e["versions"] or e.get("rejections"), e["file"])


if __name__ == "__main__":
    unittest.main()
