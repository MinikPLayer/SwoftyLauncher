import io
import json
import shutil
import subprocess
import unittest
from contextlib import redirect_stdout

from helpers import FIXTURES, TempRegistries

import schemacheck


def team(s):
    return s["$defs"]["SeasonTeam"]["properties"]


def change(s):
    return s["$defs"]["SeasonChange"]["properties"]


class Rules(TempRegistries):
    def violations(self, released=None):
        return schemacheck.Checker(self.root).run(released)

    def rules(self, released=None):
        return sorted({v.rule for v in self.violations(released)})

    def assertRule(self, rule, text=None):
        found = self.violations()
        self.assertTrue(found, "expected a violation")
        self.assertEqual({v.rule for v in found}, {rule}, "\n".join(map(str, found)))
        if text:
            self.assertTrue(any(text in str(v) for v in found), "\n".join(map(str, found)))

    def test_fixture_passes(self):
        self.assertEqual(self.violations(), [])

    def test_minor_may_add_optional_field(self):
        self.edit("1.1", lambda s: team(s).__setitem__("number", {"type": "integer"}))
        self.assertEqual(self.violations(), [])

    def test_minor_may_add_enum_value(self):
        self.edit("1.1", lambda s: change(s)["kind"]["enum"].append("retire"))
        self.assertEqual(self.violations(), [])

    def test_minor_may_loosen(self):
        def loosen(s):
            s["properties"]["year"]["maximum"] = 2200
            s["properties"]["year"]["minimum"] = 1900
            s["properties"]["year"]["type"] = "number"
            team(s)["seats"].pop("maxItems")
            s["required"].remove("teams")
            s["properties"]["data_group"].pop("pattern")
            team(s)["team"]["x-missing"]["allow"].append("fallback")
        self.edit("1.1", loosen)
        self.assertEqual(self.violations(), [])

    def test_removed_field(self):
        self.edit("1.1", lambda s: s["properties"].pop("changes"))
        self.assertRule("minor", "/changes: is removed (or renamed)")

    def test_renamed_field(self):
        self.edit("1.1", lambda s: s["properties"].__setitem__("season_year", s["properties"].pop("year")) or
                  s["required"].__setitem__(1, "season_year"))
        self.assertRule("minor", "/year: is removed")

    def test_new_required_field(self):
        def add(s):
            team(s)["number"] = {"type": "integer"}
            s["$defs"]["SeasonTeam"]["required"].append("number")
        self.edit("1.1", add)
        self.assertRule("minor", "is a new required field")

    def test_optional_made_required(self):
        self.edit("1.1", lambda s: s["required"].append("changes"))
        self.assertRule("minor", "was optional and is now required")

    def test_type_changed(self):
        self.edit("1.1", lambda s: s["properties"]["year"].__setitem__("type", "string"))
        self.assertRule("minor", "changes type")

    def test_enum_value_removed(self):
        self.edit("1.1", lambda s: change(s)["kind"]["enum"].remove("stand_in"))
        self.assertRule("minor", "removes enum value")

    def test_range_tightened(self):
        self.edit("1.1", lambda s: s["properties"]["year"].__setitem__("maximum", 2050))
        self.assertRule("minor", "tightens maximum")

    def test_pattern_added(self):
        self.edit("1.1", lambda s: team(s)["seats"]["items"].__setitem__("pattern", "^game/"))
        self.assertRule("minor", "adds pattern")

    def test_additional_properties_closed(self):
        self.edit("1.1", lambda s: s.__setitem__("additionalProperties", False))
        self.assertRule("minor", "additionalProperties")

    def test_key_changed(self):
        self.edit("1.1", lambda s: s["properties"]["changes"].__setitem__("x-key", ["round", "slot"]))
        self.assertRule("minor", "changes x-key")

    def test_key_added(self):
        self.edit("1.0", lambda s: s["properties"]["changes"].pop("x-key"))
        self.assertRule("minor", "adds x-key")

    def test_ref_target_changed(self):
        self.edit("1.1", lambda s: team(s)["team"].__setitem__("x-ref", "team.season_identity"))
        self.assertRule("minor", "changes x-ref")

    def test_missing_policy_narrowed(self):
        self.edit("1.1", lambda s: team(s)["team"]["x-missing"].__setitem__("allow", ["required"]))
        self.assertRule("minor", "no longer allows optional")

    def test_applies_changed(self):
        self.edit("1.1", lambda s: s.__setitem__("x-applies", "boot"))
        self.assertRule("minor", "changes x-applies")

    def test_snapshot_budget_shrunk(self):
        self.edit("1.1", lambda s: s.__setitem__("x-snapshot-budget", 1024))
        self.assertRule("minor", "shrinks x-snapshot-budget")

    def test_default_changed(self):
        self.edit("1.0", lambda s: s["properties"]["year"].__setitem__("default", 2016))
        self.edit("1.1", lambda s: s["properties"]["year"].__setitem__("default", 2017))
        self.assertRule("minor", "changes default")

    def test_change_behind_ref_is_seen(self):
        self.edit("1.1", lambda s: s["$defs"]["SeasonChange"]["properties"]["seat"].__setitem__("maximum", 0))
        self.assertRule("minor", "/changes/items/seat: tightens maximum")

    def test_message_names_the_major(self):
        self.edit("1.1", lambda s: s["properties"].pop("changes"))
        self.assertIn("needs 2.0.json with an adapter", str(self.violations()[0]))

    def test_unknown_keyword_is_never_compared(self):
        self.edit("1.0", lambda s: s["properties"]["year"].__setitem__("x-unit", "year"))
        self.edit("1.1", lambda s: s["properties"]["year"].__setitem__("x-unit", "season"))
        self.assertEqual(self.rules(), ["subset"])

    def test_unknown_keyword_change_is_major(self):
        found = schemacheck.compare({"type": "object", "x-unit": "year"}, {"type": "object", "x-unit": "season"})
        self.assertEqual(found, [("/", 'changes x-unit from "year" to "season"')])

    def test_major_without_adapter(self):
        self.edit("2.0", lambda s: s.pop("x-adapter"))
        self.assertRule("major", "\"x-adapter\": \"career.season@1\"")

    def test_major_wrong_adapter(self):
        self.edit("2.0", lambda s: s.__setitem__("x-adapter", "career.season@3"))
        self.assertRule("major", "expected 'career.season@1'")

    def test_major_may_break(self):
        self.edit("2.0", lambda s: s["properties"].pop("changes"))
        self.assertEqual(self.violations(), [])

    def test_major_removes_old_major(self):
        (self.reg / "1.0.json").unlink()
        (self.reg / "1.1.json").unlink()
        self.assertEqual(self.rules(), ["sequence"])

    def test_major_gap(self):
        (self.reg / "2.0.json").rename(self.reg / "3.0.json")
        self.edit("3.0", lambda s: s.update({"x-version": "3.0", "x-adapter": "career.season@2"}))
        self.assertIn("sequence", self.rules())

    def test_minor_gap(self):
        (self.reg / "1.1.json").rename(self.reg / "1.2.json")
        self.edit("1.2", lambda s: s.__setitem__("x-version", "1.2"))
        self.assertIn("sequence", self.rules())

    def test_major_zero(self):
        shutil.copy(self.reg / "1.0.json", self.reg / "0.1.json")
        self.assertIn("layout", self.rules())

    def test_stray_json(self):
        (self.reg / "schema.json").write_text("{}")
        self.assertRule("layout", "not a schema file name")

    def test_bad_folder_name(self):
        shutil.copytree(self.reg, self.root / "Career Season")
        self.assertIn("layout", self.rules())

    def test_empty_registry(self):
        (self.root / "career.calendar").mkdir()
        self.assertRule("layout", "has no schema file")

    def test_registry_name_mismatch(self):
        self.edit("1.1", lambda s: s.__setitem__("x-registry", "career.seasons"))
        self.assertIn("ids", self.rules())

    def test_version_mismatch(self):
        self.edit("1.1", lambda s: s.__setitem__("x-version", "1.2"))
        self.assertRule("meta", "x-version is 1.2 but the file is 1.1.json")

    def test_version_object_form(self):
        self.edit("1.1", lambda s: s.__setitem__("x-version", {"major": 1, "minor": 1}))
        self.assertEqual(self.violations(), [])

    def test_bad_applies(self):
        self.edit("1.0", lambda s: s.__setitem__("x-applies", "always"))
        self.assertIn("meta", self.rules())

    def test_adapter_on_major_one(self):
        self.edit("1.0", lambda s: s.__setitem__("x-adapter", "career.season@0"))
        self.assertIn("meta", self.rules())

    def test_keyword_outside_subset(self):
        self.edit("1.0", lambda s: s["properties"]["data_group"].__setitem__("format", "uri"))
        self.assertRule("subset", "'format' is outside the schema subset")

    def test_top_only_keyword_nested(self):
        self.edit("1.0", lambda s: team(s)["slot"].__setitem__("x-applies", "boot"))
        self.assertIn("subset", self.rules())

    def test_key_on_non_list(self):
        self.edit("1.0", lambda s: s["properties"]["year"].__setitem__("x-key", "slot"))
        self.assertIn("subset", self.rules())

    def test_key_field_not_required(self):
        self.edit("1.0", lambda s: s["$defs"]["SeasonTeam"]["required"].remove("slot"))
        self.assertIn("must be required", "\n".join(map(str, self.violations())))

    def test_key_field_unknown(self):
        self.edit("1.0", lambda s: s["properties"]["teams"].__setitem__("x-key", "position"))
        self.assertIn("is not a field of the items", "\n".join(map(str, self.violations())))

    def test_ref_bad_target(self):
        self.edit("1.0", lambda s: team(s)["team"].__setitem__("x-ref", "Team Identity"))
        self.assertRule("subset", "is not a registry name")

    def test_ref_on_number(self):
        self.edit("1.0", lambda s: s["properties"]["year"].__setitem__("x-ref", "career.year"))
        self.assertIn("subset", self.rules())

    def test_missing_policy_malformed(self):
        self.edit("1.0", lambda s: team(s)["team"].__setitem__("x-missing", {"default": "skip"}))
        self.assertIn("subset", self.rules())

    def test_remote_ref(self):
        self.edit("1.0", lambda s: s["properties"]["teams"].__setitem__("items", {"$ref": "https://example.com/team.json"}))
        self.assertIn("subset", self.rules())

    def test_dangling_ref(self):
        self.edit("1.0", lambda s: s["properties"]["teams"].__setitem__("items", {"$ref": "#/$defs/Nope"}))
        self.assertIn("does not resolve", "\n".join(map(str, self.violations())))

    def test_duplicate_json_key(self):
        (self.reg / "1.1.json").write_text('{ "x-registry": "career.season", "x-registry": "career.season" }')
        self.assertRule("json", "duplicate key")

    def test_not_json(self):
        (self.reg / "1.1.json").write_text("{ nope")
        self.assertRule("json")

    def test_status(self):
        for status, ok in (
            ({"status": "stable", "since": "1.6.0"}, True),
            ({"status": "experimental", "since": "1.7.0", "feature": "registry_team_identity"}, True),
            ({"status": "superseded", "since": "1.6.0", "superseded": "1.9.0", "by": "career.season@2"}, True),
            ({"status": "retired", "since": "1.6.0"}, False),
            ({"status": "stable", "since": "1.6"}, False),
            ({"status": "experimental", "since": "1.7.0"}, False),
            ({"status": "superseded", "since": "1.6.0"}, False),
        ):
            (self.reg / "status.json").write_text(json.dumps(status))
            self.assertEqual(self.rules(), [] if ok else ["status"], status)

    def test_changes_line_missing(self):
        text = (self.reg / "CHANGES.md").read_text().replace("- 1.1:", "- one-one:")
        (self.reg / "CHANGES.md").write_text(text)
        self.assertRule("changes", "has no line for 1.1")


class Released(TempRegistries):
    def setUp(self):
        super().setUp()
        self.repo = self.tmp / "repo"
        self.repo.mkdir()
        self.git("init", "-q", "-b", "main")
        self.git("config", "user.email", "test@example.com")
        self.git("config", "user.name", "test")
        self.git("config", "commit.gpgsign", "false")
        self.git("config", "tag.gpgsign", "false")
        (self.repo / "README").write_text("x\n")
        self.git("add", ".")
        self.git("commit", "-q", "-m", "init")
        self.git("tag", "v1.5.24")
        self.regs = self.repo / "packages/sdk/std/registries"
        shutil.copytree(FIXTURES / "registries", self.regs)
        self.git("add", ".")
        self.git("commit", "-q", "-m", "std")
        self.git("tag", "v1.6.0")
        self.reg = self.regs / "career.season"

    def git(self, *args):
        subprocess.run(["git", "-C", str(self.repo), *args], check=True, capture_output=True)

    def run_main(self, *args):
        out = io.StringIO()
        with redirect_stdout(out):
            code = schemacheck.main(["--repo", str(self.repo), *args])
        return code, out.getvalue()

    def test_unchanged_passes(self):
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 0, out)
        self.assertIn("3 released, 0 violations", out)

    def test_edited_released_file(self):
        self.edit("1.0", lambda s: s.__setitem__("title", "career.season, first version"))
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 1)
        self.assertIn("1.0.json: was released and has changed", out)
        self.assertIn("[released]", out)

    def test_whitespace_edit_counts(self):
        f = self.reg / "1.0.json"
        f.write_text(f.read_text() + "\n")
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 1, out)

    def test_line_endings_do_not_count(self):
        f = self.reg / "1.0.json"
        f.write_bytes(f.read_bytes().replace(b"\n", b"\r\n"))
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 0, out)

    def test_removed_released_file(self):
        (self.reg / "2.0.json").unlink()
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 1)
        self.assertIn("2.0.json: was released and is gone", out)

    def test_new_minor_after_release(self):
        s = self.schema("1.1")
        s["x-version"] = "1.2"
        s["$defs"]["SeasonTeam"]["properties"]["number"] = {"type": "integer"}
        self.write("1.2", s)
        (self.reg / "CHANGES.md").write_text((self.reg / "CHANGES.md").read_text() + "- 1.2: adds number\n")
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 0, out)

    def test_new_minor_that_breaks(self):
        s = self.schema("1.1")
        s["x-version"] = "1.2"
        s["properties"].pop("changes")
        self.write("1.2", s)
        (self.reg / "CHANGES.md").write_text((self.reg / "CHANGES.md").read_text() + "- 1.2: drops changes\n")
        code, out = self.run_main("--released", "v1.6.0")
        self.assertEqual(code, 1)
        self.assertIn("1.2.json: /changes: is removed", out)

    def test_released_before_finds_the_tag(self):
        self.git("commit", "-q", "--allow-empty", "-m", "later")
        self.edit("1.1", lambda s: s.__setitem__("title", "edited"))
        code, out = self.run_main("--released-before", "HEAD")
        self.assertIn("released schemas from v1.6.0", out)
        self.assertEqual(code, 1)

    def test_nothing_released_yet(self):
        self.edit("1.1", lambda s: s.__setitem__("title", "edited"))
        code, out = self.run_main("--released", "v1.5.24")
        self.assertEqual(code, 0, out)
        self.assertIn("0 released", out)

    def test_bad_ref(self):
        with self.assertRaises(SystemExit):
            self.run_main("--released", "v9.9.9")


if __name__ == "__main__":
    unittest.main()
