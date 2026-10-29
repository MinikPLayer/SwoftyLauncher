import unittest

from compat import semver


class Requirements(unittest.TestCase):
    def check(self, req, yes=(), no=()):
        r = semver.Requirement(req)
        for v in yes:
            self.assertTrue(r.matches(v), f"{req} should match {v}")
        for v in no:
            self.assertFalse(r.matches(v), f"{req} should not match {v}")

    def test_lower_bounds(self):
        self.check(">=1.5.1", yes=["1.5.1", "1.5.23", "1.6.0", "2.0.0"], no=["1.5.0", "1.4.9"])
        self.check(">1.5.1", yes=["1.5.2"], no=["1.5.1"])
        self.check(">1.5", yes=["1.6.0"], no=["1.5.9"])

    def test_upper_bounds(self):
        self.check("<1.6.0", yes=["1.5.23"], no=["1.6.0"])
        self.check("<=1.5", yes=["1.5.99"], no=["1.6.0"])
        self.check(">=1.5.0, <1.6.0", yes=["1.5.0", "1.5.23"], no=["1.6.0", "1.4.0"])

    def test_caret_and_tilde(self):
        self.check("^1.5.3", yes=["1.5.3", "1.9.0"], no=["2.0.0", "1.5.2"])
        self.check("1.5.3", yes=["1.5.3", "1.9.0"], no=["2.0.0"])
        self.check("^0.2.11", yes=["0.2.13"], no=["0.3.0"])
        self.check("^0.0.3", yes=["0.0.3"], no=["0.0.4"])
        self.check("~1.5.3", yes=["1.5.9"], no=["1.6.0"])
        self.check("~1", yes=["1.9.9"], no=["2.0.0"])

    def test_exact_and_wildcards(self):
        self.check("=1.5.23", yes=["1.5.23"], no=["1.5.24"])
        self.check("=1.5", yes=["1.5.0", "1.5.7"], no=["1.6.0"])
        self.check("1.5.*", yes=["1.5.7"], no=["1.6.0"])
        self.check("*", yes=["0.0.1", "9.9.9"])
        self.check("", yes=["1.0.0"])

    def test_pre_releases_only_match_their_own_release(self):
        self.check(">=1.6.0-rc.1", yes=["1.6.0-rc.2", "1.6.0"], no=["1.6.1-rc.1"])
        self.check(">=1.5.1", no=["1.6.0-rc.1"])

    def test_ordering(self):
        self.assertLess(semver.key("1.6.0-rc.1"), semver.key("1.6.0"))
        self.assertLess(semver.key("1.6.0-rc.2"), semver.key("1.6.0-rc.10"))
        self.assertLess(semver.key("1.5.23"), semver.key("1.6.0-alpha"))

    def test_invalid(self):
        with self.assertRaises(semver.Invalid):
            semver.parse("1.5")
        with self.assertRaises(semver.Invalid):
            semver.Requirement(">=banana")


if __name__ == "__main__":
    unittest.main()
