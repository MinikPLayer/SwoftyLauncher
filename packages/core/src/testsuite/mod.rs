//! `swofty test`: test files, plans, the game lock, the test agent's mailbox,
//! checks and reports. The CLI drives the game; everything here works on
//! files and text, so it is tested without one.

pub mod agent;
pub mod bench;
pub mod career;
pub mod checks;
pub mod fixture;
pub mod lock;
pub mod plan;
pub mod report;
pub mod sandbox;
pub mod suite;

#[cfg(test)]
mod tests;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error(pub String);

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(&self.0)
    }
}

impl std::error::Error for Error {}
