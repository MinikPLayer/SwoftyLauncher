//! The launcher's side of the 1.6 standard library (docs/std/launcher.md):
//! it reads each addon's `registry.json` and `registry.lock.json`, checks
//! dependencies before Play and at install, writes the registry index for the
//! core, reads the core's reports, and keeps the database room's safety net.
//! It applies nothing an addon registers: the core does that in game.

pub mod careers;
pub mod data;
pub mod deps;
pub mod index;
pub mod lock;
pub mod manifest;
pub mod paths;
pub mod range;
pub mod reports;
pub mod room;
pub mod state;
