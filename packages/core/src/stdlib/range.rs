//! Version ranges in `registry.json` and `registry.lock.json`: the ranges of
//! `swofty.json`'s `requires` (`^1.3.0`, `>=0.11.0`, `<0.5.0`, `*`), with the
//! short forms the standard library's docs write: a missing or empty range is
//! `*`, a version may leave out its minor and patch (`^1`, `>=1`), and
//! comparators may be separated by spaces as well as commas
//! (`>=1.0 <2`). Registry versions (`1.0`) read the same way.

use std::fmt;

use semver::{Version, VersionReq};

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Range {
    text: String,
    req: VersionReq,
}

impl Range {
    pub fn any() -> Self {
        Self {
            text: "*".into(),
            req: VersionReq::STAR,
        }
    }

    pub fn parse(text: &str) -> Result<Self, String> {
        let trimmed = text.trim();
        if trimmed.is_empty() || trimmed == "*" || trimmed.eq_ignore_ascii_case("x") {
            return Ok(Self::any());
        }
        let joined = join_comparators(trimmed);
        VersionReq::parse(&joined)
            .map(|req| Self {
                text: trimmed.to_owned(),
                req,
            })
            .map_err(|e| format!("{trimmed:?} is not a version range ({e})"))
    }

    pub fn of(text: Option<&str>) -> Result<Self, String> {
        text.map_or_else(|| Ok(Self::any()), Self::parse)
    }

    pub fn matches(&self, v: &Version) -> bool {
        self.req.matches(v)
    }

    pub fn is_any(&self) -> bool {
        self.req == VersionReq::STAR
    }

    /// Whether a newer version can leave the range: it has an upper bound and no lower one, as
    /// `<0.5.0` or `<=1.2`, so updating the addon is a fix.
    pub fn update_escapes(&self) -> bool {
        use semver::Op;
        !self.req.comparators.is_empty()
            && self
                .req
                .comparators
                .iter()
                .all(|c| matches!(c.op, Op::Less | Op::LessEq | Op::Exact | Op::Caret | Op::Tilde))
    }

    pub fn as_str(&self) -> &str {
        &self.text
    }
}

impl fmt::Display for Range {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.text)
    }
}

fn join_comparators(text: &str) -> String {
    let mut out = String::new();
    let mut pending_op = false;
    for word in text.split_whitespace() {
        let only_op = word.chars().all(|c| matches!(c, '<' | '>' | '=' | '^' | '~'));
        if out.is_empty() || pending_op || word.starts_with(',') || out.ends_with(',') {
            out.push_str(word);
        } else {
            out.push_str(", ");
            out.push_str(word);
        }
        pending_op = only_op;
    }
    out
}

/// A version as `registry.json` and the reports write it: `1`, `1.0` or a full `1.0.0`.
pub fn version(text: &str) -> Option<Version> {
    let t = text.trim().trim_start_matches(['v', 'V']);
    if let Ok(v) = Version::parse(t) {
        return Some(v);
    }
    let (core, rest) = match t.find(['-', '+']) {
        Some(at) => t.split_at(at),
        None => (t, ""),
    };
    let parts: Vec<&str> = core.split('.').collect();
    if parts.is_empty()
        || parts.len() > 3
        || parts
            .iter()
            .any(|p| p.is_empty() || !p.bytes().all(|b| b.is_ascii_digit()))
    {
        return None;
    }
    let mut full: Vec<&str> = parts;
    while full.len() < 3 {
        full.push("0");
    }
    Version::parse(&format!("{}{rest}", full.join("."))).ok()
}

#[cfg(test)]
mod tests {
    use super::*;

    fn v(t: &str) -> Version {
        version(t).unwrap()
    }

    #[test]
    fn reads_the_ranges_the_docs_write() {
        assert!(Range::parse("^1").unwrap().matches(&v("1.0")));
        assert!(Range::parse("^1").unwrap().matches(&v("1.4.2")));
        assert!(!Range::parse("^1").unwrap().matches(&v("2.0")));
        assert!(Range::parse(">=1").unwrap().matches(&v("3.1.0")));
        assert!(!Range::parse(">=1").unwrap().matches(&v("0.9.9")));
        assert!(Range::parse("<0.5.0").unwrap().matches(&v("0.4.2")));
        assert!(!Range::parse("<0.5.0").unwrap().matches(&v("0.5.0")));
        assert!(Range::parse(">=0.11.0").unwrap().matches(&v("0.11.0")));
        assert!(Range::parse("^1.3.0").unwrap().matches(&v("1.9.0")));
        assert!(!Range::parse("^1.3.0").unwrap().matches(&v("1.2.9")));
        assert!(Range::parse("*").unwrap().matches(&v("0.0.1")));
    }

    #[test]
    fn a_missing_or_empty_range_is_any_version() {
        assert!(Range::of(None).unwrap().is_any());
        assert!(Range::parse("").unwrap().is_any());
        assert!(Range::parse("  ").unwrap().is_any());
        assert!(Range::parse("x").unwrap().is_any());
    }

    #[test]
    fn comparators_may_be_separated_by_spaces_or_commas() {
        let r = Range::parse(">=1.0 <2").unwrap();
        assert!(r.matches(&v("1.5")));
        assert!(!r.matches(&v("2.0")));
        let r = Range::parse(">=1.0, <2").unwrap();
        assert!(r.matches(&v("1.5")));
        let r = Range::parse(">= 1.2 < 2").unwrap();
        assert!(r.matches(&v("1.2")));
        assert!(!r.matches(&v("1.1")));
        assert_eq!(r.to_string(), ">= 1.2 < 2");
    }

    #[test]
    fn a_broken_range_is_an_error_that_names_it() {
        let e = Range::parse(">=banana").unwrap_err();
        assert!(e.contains("\">=banana\" is not a version range"), "{e}");
    }

    #[test]
    fn short_versions_are_padded() {
        assert_eq!(version("1"), Some(Version::new(1, 0, 0)));
        assert_eq!(version("1.0"), Some(Version::new(1, 0, 0)));
        assert_eq!(version("2.3.4"), Some(Version::new(2, 3, 4)));
        assert_eq!(version("v1.2"), Some(Version::new(1, 2, 0)));
        assert_eq!(
            version("1.0-beta.1").map(|v| v.pre.to_string()),
            Some("beta.1".into())
        );
        assert_eq!(version("1..0"), None);
        assert_eq!(version("one"), None);
        assert_eq!(version("1.2.3.4"), None);
    }

    #[test]
    fn only_an_upper_bound_can_be_updated_out_of() {
        assert!(Range::parse("<0.5.0").unwrap().update_escapes());
        assert!(Range::parse("=0.4.2").unwrap().update_escapes());
        assert!(!Range::parse("*").unwrap().update_escapes());
        assert!(!Range::parse(">=1").unwrap().update_escapes());
    }
}
