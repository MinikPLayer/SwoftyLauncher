import unittest

from compat import catalog


def version(v, launcher=">=1.5.1", requires=None, yanked=False, size=10):
    out = {"version": v, "launcher": launcher, "size": size, "sha256": "ab" * 32, "url": f"u/{v}", "yanked": yanked}
    if requires is not None:
        out["requires"] = requires
    return out


CATALOG = {
    "addons": [
        {"id": "base", "name": "Base", "versions": [version("0.6.2"), version("0.6.1"), version("0.5.0")]},
        {
            "id": "year",
            "name": "Year",
            "versions": [
                version("0.6.5", requires=[{"id": "swofty.core", "version": ">=0.2.13"}, {"id": "base", "version": ">=0.6.1"}]),
                version("0.6.4", launcher=">=1.6.0"),
                version("0.6.3", yanked=True),
            ],
        },
        {"id": "new", "name": "New", "versions": [version("1.0.0", launcher=">=1.6.0")]},
        {"id": "loop", "name": "Loop", "versions": [version("1.0.0", requires=[{"id": "loop2", "version": "*"}])]},
        {"id": "loop2", "name": "Loop2", "versions": [version("1.0.0", requires=[{"id": "loop", "version": "*"}])]},
        {"id": "needs", "name": "Needs", "versions": [version("1.0.0", requires=[{"id": "base", "version": ">=9"}])]},
    ]
}


class Selecting(unittest.TestCase):
    def setUp(self):
        self.entries = catalog.entries(CATALOG)

    def labels(self, items):
        return [e.label for e in items]

    def test_every_version_a_launcher_accepts_newest_first(self):
        picked = catalog.select(self.entries, "1.5.23")
        self.assertEqual(
            self.labels(picked),
            ["base@0.6.2", "base@0.6.1", "base@0.5.0", "loop@1.0.0", "loop2@1.0.0", "needs@1.0.0", "year@0.6.5"],
        )

    def test_yanked_and_newer_launcher_versions_are_left_out(self):
        picked = self.labels(catalog.select(self.entries, "1.5.23"))
        self.assertNotIn("year@0.6.3", picked)
        self.assertNotIn("year@0.6.4", picked)
        self.assertNotIn("new@1.0.0", picked)
        self.assertIn("new@1.0.0", self.labels(catalog.select(self.entries, "1.6.0")))

    def test_latest_and_only(self):
        self.assertEqual(
            self.labels(catalog.select(self.entries, "1.5.23", latest=True, only=catalog.parse_only(["base,year"]))),
            ["base@0.6.2", "year@0.6.5"],
        )
        self.assertEqual(
            self.labels(catalog.select(self.entries, None, only=catalog.parse_only(["base@0.6.1"]))), ["base@0.6.1"]
        )

    def test_requirements_take_the_newest_fitting_version(self):
        year = next(e for e in self.entries if e.label == "year@0.6.5")
        deps, unresolved, outside = catalog.requirements(year, self.entries, "1.5.23")
        self.assertEqual(self.labels(deps), ["base@0.6.2"])
        self.assertEqual(unresolved, [])
        self.assertEqual(outside, [("swofty.core", ">=0.2.13")])

    def test_requirement_cycles_end(self):
        loop = next(e for e in self.entries if e.label == "loop@1.0.0")
        deps, _, _ = catalog.requirements(loop, self.entries, "1.5.23")
        self.assertEqual(self.labels(deps), ["loop2@1.0.0"])

    def test_unresolvable_requirements_are_reported(self):
        needs = next(e for e in self.entries if e.id == "needs")
        deps, unresolved, _ = catalog.requirements(needs, self.entries, "1.5.23")
        self.assertEqual(deps, [])
        self.assertEqual(unresolved, [("base", ">=9")])

    def test_no_launcher_range_accepts_everything(self):
        e = catalog.Entry("x", "x", "1.0.0", "", 1, "", "")
        self.assertTrue(e.accepts("1.0.0"))
        self.assertFalse(catalog.Entry("x", "x", "1.0.0", ">=banana", 1, "", "").accepts("1.5.0"))


if __name__ == "__main__":
    unittest.main()
