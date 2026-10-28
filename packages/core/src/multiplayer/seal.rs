//! The bridge's wire format: each game datagram sealed end to end between launchers.
//!
//! `ver:u8 (2), from:u8, session:u64, counter:u64` big-endian, then the ChaCha20-Poly1305
//! ciphertext and its 16-byte tag. A sender picks a session id when its bridge starts (see
//! [`new_session_id`]) and seals under the subkey HKDF-SHA256(lobby key, empty salt, info = `swofty-netplay-v1` ‖
//! session ‖ from), counting from 0 for each destination. The AAD is the 18-byte header followed
//! by the receiver's lobby number, and the nonce is `from`, `to`, two zero bytes, then the
//! counter, so a datagram opens only at the member it was sealed for.
//!
//! A receiver keeps a [`WINDOW`]-counter sliding window for each sender's current session. A
//! datagram of a larger session id takes over once it verifies; a smaller one only once the
//! current session has been silent for [`TAKEOVER_SILENCE`], for a sender whose clock stepped back.
//! A receiver derives a subkey for at most one new session id per sender every [`ATTEMPT_GAP`].

use std::collections::HashMap;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use ring::aead::{Aad, CHACHA20_POLY1305, LessSafeKey, NONCE_LEN, Nonce, UnboundKey};
use ring::hkdf;
use zeroize::Zeroize;

pub const VERSION: u8 = 2;
pub const HEADER_LEN: usize = 18;
pub const TAG_LEN: usize = 16;
/// What sealing adds to a datagram.
pub const OVERHEAD: usize = HEADER_LEN + TAG_LEN;
/// How many counters behind the newest one a datagram may arrive and still be accepted.
pub const WINDOW: u64 = 1024;
const WORDS: usize = (WINDOW / 64) as usize;
/// How long a sender's current session must be silent before a smaller session id may replace it.
pub const TAKEOVER_SILENCE: Duration = Duration::from_secs(10);
/// How soon after trying one new session id a receiver tries another from the same sender.
pub const ATTEMPT_GAP: Duration = Duration::from_millis(50);
const INFO: &[u8] = b"swofty-netplay-v1";
const RANDOM_BITS: u32 = 22;

/// Why a sealed datagram was dropped.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Reject {
    /// Shorter than a header and tag, or another wire version.
    Malformed,
    /// The header names a sender that isn't a remote member.
    UnknownMember,
    /// The header names a different member from the one the transport received it from.
    WrongSender,
    /// The tag doesn't verify: another key or recipient, or a changed header or ciphertext.
    BadTag,
    /// A counter already accepted in this session.
    Replay,
    /// A counter older than the session's window.
    TooOld,
    /// A smaller session id than the sender's current one, which isn't silent yet.
    OldSession,
    /// Another new session id from this sender within [`ATTEMPT_GAP`] of the last one tried.
    Throttled,
}

/// A session id that orders by start time: the milliseconds since the Unix epoch in the high 42
/// bits, and 22 bits of `random` below them.
pub fn new_session_id(now: SystemTime, random: u64) -> u64 {
    let ms = now.duration_since(UNIX_EPOCH).map_or(0, |d| d.as_millis() as u64);
    (ms << RANDOM_BITS) | (random & ((1 << RANDOM_BITS) - 1))
}

pub fn header(from: u8, session: u64, counter: u64) -> [u8; HEADER_LEN] {
    let mut h = [0u8; HEADER_LEN];
    h[0] = VERSION;
    h[1] = from;
    h[2..10].copy_from_slice(&session.to_be_bytes());
    h[10..].copy_from_slice(&counter.to_be_bytes());
    h
}

pub fn nonce(from: u8, to: u8, counter: u64) -> [u8; NONCE_LEN] {
    let mut n = [0u8; NONCE_LEN];
    n[0] = from;
    n[1] = to;
    n[4..].copy_from_slice(&counter.to_be_bytes());
    n
}

/// The header followed by the receiver's lobby number.
pub fn aad(header: &[u8], to: u8) -> [u8; HEADER_LEN + 1] {
    let mut a = [0u8; HEADER_LEN + 1];
    a[..HEADER_LEN].copy_from_slice(&header[..HEADER_LEN]);
    a[HEADER_LEN] = to;
    a
}

/// The key member `from` seals with in `session`.
pub fn subkey(lobby_key: &[u8; 32], session: u64, from: u8) -> LessSafeKey {
    let prk = hkdf::Salt::new(hkdf::HKDF_SHA256, &[]).extract(lobby_key);
    let session = session.to_be_bytes();
    let info: [&[u8]; 3] = [INFO, &session, &[from]];
    let okm = prk
        .expand(&info, &CHACHA20_POLY1305)
        .expect("a 32-byte key is within HKDF's output limit");
    LessSafeKey::new(UnboundKey::from(okm))
}

/// Seals this member's outgoing datagrams in one session, with a counter per destination.
pub struct Sealer {
    key: LessSafeKey,
    from: u8,
    session: u64,
    counters: [AtomicU64; 256],
}

impl Sealer {
    pub fn new(lobby_key: &[u8; 32], from: u8, session: u64) -> Self {
        Self {
            key: subkey(lobby_key, session, from),
            from,
            session,
            counters: std::array::from_fn(|_| AtomicU64::new(0)),
        }
    }

    pub fn session(&self) -> u64 {
        self.session
    }

    /// Replace `out` with `plaintext` sealed for member `to` under its next counter.
    pub fn seal(&self, to: u8, plaintext: &[u8], out: &mut Vec<u8>) {
        let counter = self.counters[usize::from(to)].fetch_add(1, Ordering::Relaxed);
        self.seal_at(to, counter, plaintext, out);
    }

    fn seal_at(&self, to: u8, counter: u64, plaintext: &[u8], out: &mut Vec<u8>) {
        out.clear();
        out.reserve(plaintext.len() + OVERHEAD);
        out.extend_from_slice(&header(self.from, self.session, counter));
        out.extend_from_slice(plaintext);
        let aad = aad(out, to);
        let nonce = Nonce::assume_unique_for_key(nonce(self.from, to, counter));
        let tag = self
            .key
            .seal_in_place_separate_tag(nonce, Aad::from(aad), &mut out[HEADER_LEN..])
            .expect("a datagram is far below the AEAD's length limit");
        out.extend_from_slice(tag.as_ref());
    }
}

#[cfg(test)]
impl Sealer {
    /// Seal under a chosen counter, which the bridge never does.
    pub(crate) fn seal_with_counter(&self, to: u8, counter: u64, plaintext: &[u8], out: &mut Vec<u8>) {
        self.seal_at(to, counter, plaintext, out);
    }
}

/// Opens the datagrams sealed for member `me`, keeping each remote member's session. Wipes the
/// lobby key when dropped.
pub struct Opener {
    lobby_key: [u8; 32],
    me: u8,
    peers: HashMap<u8, Peer>,
    derived: u64,
}

#[derive(Default)]
struct Peer {
    current: Option<Session>,
    attempt: Option<Attempt>,
}

struct Session {
    id: u64,
    key: LessSafeKey,
    window: ReplayWindow,
    last: Instant,
}

/// The last new session id tried for a sender, with its subkey.
struct Attempt {
    id: u64,
    key: LessSafeKey,
    at: Instant,
}

impl Drop for Opener {
    fn drop(&mut self) {
        self.lobby_key.zeroize();
    }
}

impl zeroize::ZeroizeOnDrop for Opener {}

impl Opener {
    pub fn new(lobby_key: &[u8; 32], me: u8, members: impl IntoIterator<Item = u8>) -> Self {
        Self {
            lobby_key: *lobby_key,
            me,
            peers: members.into_iter().map(|m| (m, Peer::default())).collect(),
            derived: 0,
        }
    }

    /// Accept datagrams from `member`, who joined after this opener was made.
    pub fn add_member(&mut self, member: u8) {
        if member != self.me {
            self.peers.entry(member).or_default();
        }
    }

    /// How many session subkeys this opener has derived.
    pub fn subkeys_derived(&self) -> u64 {
        self.derived
    }

    /// Open `wire`, which the transport received from member `sender`, in place and return its
    /// plaintext.
    pub fn open<'a>(&mut self, sender: u8, wire: &'a mut [u8]) -> Result<&'a [u8], Reject> {
        self.open_at(sender, wire, Instant::now())
    }

    pub fn open_at<'a>(&mut self, sender: u8, wire: &'a mut [u8], now: Instant) -> Result<&'a [u8], Reject> {
        if wire.len() < OVERHEAD || wire[0] != VERSION {
            return Err(Reject::Malformed);
        }
        let from = wire[1];
        let peer = self.peers.get_mut(&from).ok_or(Reject::UnknownMember)?;
        if from != sender {
            return Err(Reject::WrongSender);
        }
        let session = u64::from_be_bytes(wire[2..10].try_into().expect("8 bytes"));
        let counter = u64::from_be_bytes(wire[10..HEADER_LEN].try_into().expect("8 bytes"));
        let aad = Aad::from(aad(wire, self.me));
        let nonce = Nonce::assume_unique_for_key(nonce(from, self.me, counter));
        let body = &mut wire[HEADER_LEN..];
        if let Some(current) = peer.current.as_mut().filter(|c| c.id == session) {
            current.window.check(counter)?;
            let plain = current
                .key
                .open_in_place(nonce, aad, body)
                .map_err(|_| Reject::BadTag)?;
            current.window.accept(counter);
            current.last = now;
            return Ok(plain);
        }
        if let Some(current) = &peer.current
            && session < current.id
            && now.saturating_duration_since(current.last) < TAKEOVER_SILENCE
        {
            return Err(Reject::OldSession);
        }
        let attempt = match peer.attempt.take() {
            Some(a) if a.id == session => a,
            Some(a) if now.saturating_duration_since(a.at) < ATTEMPT_GAP => {
                peer.attempt = Some(a);
                return Err(Reject::Throttled);
            }
            _ => {
                self.derived += 1;
                Attempt {
                    id: session,
                    key: subkey(&self.lobby_key, session, from),
                    at: now,
                }
            }
        };
        match attempt.key.open_in_place(nonce, aad, body) {
            Ok(plain) => {
                let mut window = ReplayWindow::default();
                window.accept(counter);
                peer.current = Some(Session {
                    id: session,
                    key: attempt.key,
                    window,
                    last: now,
                });
                Ok(plain)
            }
            Err(_) => {
                peer.attempt = Some(attempt);
                Err(Reject::BadTag)
            }
        }
    }
}

/// The counters seen in one session: the newest, and a bit for each of the [`WINDOW`] up to and
/// including it.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ReplayWindow {
    top: Option<u64>,
    seen: [u64; WORDS],
}

impl ReplayWindow {
    pub fn check(&self, counter: u64) -> Result<(), Reject> {
        let Some(top) = self.top else { return Ok(()) };
        if counter > top {
            Ok(())
        } else if top - counter >= WINDOW {
            Err(Reject::TooOld)
        } else if self.is_set(counter) {
            Err(Reject::Replay)
        } else {
            Ok(())
        }
    }

    /// Record `counter`, which [`ReplayWindow::check`] passed and whose tag verified.
    pub fn accept(&mut self, counter: u64) {
        match self.top {
            Some(top) if counter <= top => {}
            Some(top) if counter - top < WINDOW => {
                for n in top + 1..counter {
                    self.clear(n);
                }
                self.top = Some(counter);
            }
            _ => {
                self.seen = [0; WORDS];
                self.top = Some(counter);
            }
        }
        let (word, bit) = Self::slot(counter);
        self.seen[word] |= bit;
    }

    fn slot(counter: u64) -> (usize, u64) {
        let index = counter % WINDOW;
        ((index / 64) as usize, 1 << (index % 64))
    }

    fn is_set(&self, counter: u64) -> bool {
        let (word, bit) = Self::slot(counter);
        self.seen[word] & bit != 0
    }

    fn clear(&mut self, counter: u64) {
        let (word, bit) = Self::slot(counter);
        self.seen[word] &= !bit;
    }
}

#[cfg(test)]
mod tests {
    use ring::hmac;

    use super::*;

    const KEY: [u8; 32] = [7; 32];
    const SESSION: u64 = 0x0102_0304_0506_0708;

    fn sealed(from: u8, to: u8, plaintext: &[u8]) -> Vec<u8> {
        let mut wire = Vec::new();
        Sealer::new(&KEY, from, SESSION).seal(to, plaintext, &mut wire);
        wire
    }

    #[test]
    fn seals_and_opens_with_the_contract_layout() {
        let sealer = Sealer::new(&KEY, 3, SESSION);
        let mut wire = Vec::new();
        sealer.seal_with_counter(1, 0x1112_1314_1516_1718, b"hello game", &mut wire);
        assert_eq!(wire.len(), 10 + OVERHEAD);
        assert_eq!(
            wire[..HEADER_LEN],
            [
                2, 3, 1, 2, 3, 4, 5, 6, 7, 8, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18
            ]
        );
        assert_ne!(&wire[HEADER_LEN..HEADER_LEN + 10], b"hello game");
        let mut opener = Opener::new(&KEY, 1, [3]);
        assert_eq!(opener.open(3, &mut wire).unwrap(), b"hello game");
    }

    #[test]
    fn counts_from_zero_for_each_destination() {
        let sealer = Sealer::new(&KEY, 2, SESSION);
        let mut wire = Vec::new();
        let counter = |wire: &[u8]| u64::from_be_bytes(wire[10..HEADER_LEN].try_into().unwrap());
        sealer.seal(1, b"a", &mut wire);
        assert_eq!(counter(&wire), 0);
        sealer.seal(3, b"b", &mut wire);
        assert_eq!(counter(&wire), 0);
        sealer.seal(1, b"c", &mut wire);
        assert_eq!(counter(&wire), 1);
        assert_eq!(wire[2..10], SESSION.to_be_bytes());
    }

    #[test]
    fn the_subkey_is_hkdf_sha256_over_the_session_and_sender() {
        let prk = hmac::sign(&hmac::Key::new(hmac::HMAC_SHA256, &[0; 32]), &KEY);
        let mut info = b"swofty-netplay-v1".to_vec();
        info.extend_from_slice(&SESSION.to_be_bytes());
        info.extend_from_slice(&[9, 1]);
        let okm = hmac::sign(&hmac::Key::new(hmac::HMAC_SHA256, prk.as_ref()), &info);
        let expected = LessSafeKey::new(UnboundKey::new(&CHACHA20_POLY1305, okm.as_ref()).unwrap());

        let mut wire = Vec::new();
        Sealer::new(&KEY, 9, SESSION).seal_with_counter(4, 77, b"payload", &mut wire);
        assert_eq!(nonce(9, 4, 77), [9, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 77]);
        let mut body = wire[HEADER_LEN..].to_vec();
        let nonce = Nonce::assume_unique_for_key(nonce(9, 4, 77));
        let mut ad = wire[..HEADER_LEN].to_vec();
        ad.push(4);
        let plain = expected.open_in_place(nonce, Aad::from(&ad), &mut body).unwrap();
        assert_eq!(plain, b"payload");
    }

    #[test]
    fn a_datagram_opens_only_at_its_recipient() {
        let mut wire = sealed(1, 3, b"for three");
        let mut at_two = Opener::new(&KEY, 2, [1, 3]);
        assert_eq!(at_two.open(1, &mut wire.clone()), Err(Reject::BadTag));
        let mut at_three = Opener::new(&KEY, 3, [1, 2]);
        assert_eq!(at_three.open(1, &mut wire).unwrap(), b"for three");
    }

    #[test]
    fn the_header_is_bound_to_the_tag() {
        let sealed = sealed(2, 1, b"bound");
        for byte in 2..HEADER_LEN {
            let mut wire = sealed.clone();
            wire[byte] ^= 1;
            let mut opener = Opener::new(&KEY, 1, [2]);
            assert_eq!(opener.open(2, &mut wire), Err(Reject::BadTag), "byte {byte}");
        }
        let mut wire = sealed.clone();
        wire[1] = 4;
        let mut opener = Opener::new(&KEY, 1, [2, 4]);
        assert_eq!(opener.open(4, &mut wire), Err(Reject::BadTag));
        let mut wire = sealed.clone();
        *wire.last_mut().unwrap() ^= 0x80;
        assert_eq!(opener.open(2, &mut wire), Err(Reject::BadTag));
        let mut wire = sealed.clone();
        wire[HEADER_LEN] ^= 0x80;
        assert_eq!(opener.open(2, &mut wire), Err(Reject::BadTag));
    }

    #[test]
    fn rejects_malformed_unknown_and_misattributed_datagrams() {
        let sealed = sealed(2, 1, b"x");
        let mut opener = Opener::new(&KEY, 1, [2, 3]);
        let mut short = sealed[..OVERHEAD - 1].to_vec();
        assert_eq!(opener.open(2, &mut short), Err(Reject::Malformed));
        let mut wire = sealed.clone();
        wire[0] = 1;
        assert_eq!(opener.open(2, &mut wire), Err(Reject::Malformed));
        assert_eq!(opener.open(3, &mut sealed.clone()), Err(Reject::WrongSender));
        let mut opener = Opener::new(&KEY, 1, [3]);
        assert_eq!(opener.open(2, &mut sealed.clone()), Err(Reject::UnknownMember));
        let mut opener = Opener::new(&[8; 32], 1, [2]);
        assert_eq!(opener.open(2, &mut sealed.clone()), Err(Reject::BadTag));
    }

    #[test]
    fn an_empty_datagram_round_trips() {
        let mut wire = sealed(5, 1, b"");
        assert_eq!(wire.len(), OVERHEAD);
        assert_eq!(Opener::new(&KEY, 1, [5]).open(5, &mut wire).unwrap(), b"");
    }

    #[test]
    fn a_restarted_sender_never_reuses_a_keystream() {
        let (mut a, mut b) = (Vec::new(), Vec::new());
        Sealer::new(&KEY, 2, 1).seal(1, b"same plaintext", &mut a);
        Sealer::new(&KEY, 2, 2).seal(1, b"same plaintext", &mut b);
        assert_eq!(a[10..HEADER_LEN], b[10..HEADER_LEN]);
        assert_ne!(a[HEADER_LEN..], b[HEADER_LEN..]);
    }

    #[test]
    fn a_delayed_datagram_from_the_previous_session_is_dropped() {
        let (old, new) = (Sealer::new(&KEY, 2, 10), Sealer::new(&KEY, 2, 11));
        let mut opener = Opener::new(&KEY, 1, [2]);
        let (mut old_wires, mut wire) = (Vec::new(), Vec::new());
        for _ in 0..3 {
            old.seal(1, b"old", &mut wire);
            old_wires.push(wire.clone());
        }
        assert_eq!(opener.open(2, &mut old_wires[0].clone()).unwrap(), b"old");
        assert_eq!(opener.open(2, &mut old_wires[1].clone()).unwrap(), b"old");
        new.seal(1, b"new", &mut wire);
        assert_eq!(opener.open(2, &mut wire.clone()).unwrap(), b"new");
        assert_eq!(opener.open(2, &mut wire), Err(Reject::Replay));
        assert_eq!(opener.open(2, &mut old_wires[2].clone()), Err(Reject::OldSession));
        assert_eq!(opener.open(2, &mut old_wires[0].clone()), Err(Reject::OldSession));
        new.seal(1, b"next", &mut wire);
        assert_eq!(opener.open(2, &mut wire).unwrap(), b"next");
    }

    #[test]
    fn a_forged_new_session_leaves_the_current_one_alone() {
        let real = Sealer::new(&KEY, 2, 10);
        let mut opener = Opener::new(&KEY, 1, [2]);
        let mut wire = Vec::new();
        real.seal(1, b"first", &mut wire);
        opener.open(2, &mut wire).unwrap();
        let mut forged = Vec::new();
        Sealer::new(&[9; 32], 2, 99).seal(1, b"forged", &mut forged);
        assert_eq!(opener.open(2, &mut forged), Err(Reject::BadTag));
        real.seal(1, b"second", &mut wire);
        assert_eq!(opener.open(2, &mut wire).unwrap(), b"second");
    }

    #[test]
    fn session_ids_order_by_start_time() {
        let at = |ms| UNIX_EPOCH + Duration::from_millis(ms);
        assert_eq!(new_session_id(at(1000), u64::MAX), (1000 << 22) | 0x3f_ffff);
        assert_eq!(new_session_id(at(1000), 5), (1000 << 22) | 5);
        assert!(new_session_id(at(1001), 0) > new_session_id(at(1000), u64::MAX));
        let now = SystemTime::now();
        assert!(new_session_id(now + Duration::from_millis(1), 0) > new_session_id(now, u64::MAX));
    }

    #[test]
    fn a_late_datagram_from_a_skipped_session_never_takes_over() {
        let (a, b, c) = (
            Sealer::new(&KEY, 2, 100),
            Sealer::new(&KEY, 2, 200),
            Sealer::new(&KEY, 2, 300),
        );
        let mut opener = Opener::new(&KEY, 1, [2]);
        let t = Instant::now();
        let secs = |n| t + Duration::from_secs(n);
        let (mut wire, mut late_b) = (Vec::new(), Vec::new());
        a.seal(1, b"a", &mut wire);
        assert_eq!(opener.open_at(2, &mut wire, t).unwrap(), b"a");
        b.seal(1, b"late b", &mut late_b);
        c.seal(1, b"c", &mut wire);
        assert_eq!(opener.open_at(2, &mut wire, secs(1)).unwrap(), b"c");
        assert_eq!(opener.open_at(2, &mut late_b, secs(2)), Err(Reject::OldSession));
        c.seal(1, b"c again", &mut wire);
        assert_eq!(opener.open_at(2, &mut wire, secs(3)).unwrap(), b"c again");
    }

    #[test]
    fn a_smaller_session_takes_over_after_ten_seconds_of_silence() {
        let (current, stepped_back) = (Sealer::new(&KEY, 2, 500), Sealer::new(&KEY, 2, 400));
        let mut opener = Opener::new(&KEY, 1, [2]);
        let t = Instant::now();
        let mut wire = Vec::new();
        current.seal(1, b"before", &mut wire);
        opener.open_at(2, &mut wire, t).unwrap();
        stepped_back.seal(1, b"too soon", &mut wire);
        let soon = t + TAKEOVER_SILENCE - Duration::from_millis(1);
        assert_eq!(opener.open_at(2, &mut wire, soon), Err(Reject::OldSession));
        stepped_back.seal(1, b"after", &mut wire);
        assert_eq!(
            opener.open_at(2, &mut wire, t + TAKEOVER_SILENCE).unwrap(),
            b"after"
        );
        stepped_back.seal(1, b"next", &mut wire);
        assert_eq!(
            opener.open_at(2, &mut wire, t + TAKEOVER_SILENCE).unwrap(),
            b"next"
        );
    }

    #[test]
    fn a_flood_of_fresh_session_ids_derives_a_bounded_number_of_keys() {
        let live = Sealer::new(&KEY, 2, 1000);
        let mut opener = Opener::new(&KEY, 1, [2]);
        let t = Instant::now();
        let mut wire = Vec::new();
        live.seal(1, b"live", &mut wire);
        opener.open_at(2, &mut wire, t).unwrap();
        let mut bad_tags = 0;
        for i in 0..10_000u64 {
            let mut fake = wire.clone();
            fake[2..10].copy_from_slice(&(2000 + i).to_be_bytes());
            match opener.open_at(2, &mut fake, t) {
                Err(Reject::BadTag) => bad_tags += 1,
                other => assert_eq!(other, Err(Reject::Throttled)),
            }
        }
        assert_eq!((bad_tags, opener.subkeys_derived()), (1, 2));
        live.seal(1, b"still live", &mut wire);
        assert_eq!(opener.open_at(2, &mut wire, t).unwrap(), b"still live");
        let mut fake = wire.clone();
        fake[2..10].copy_from_slice(&99_999u64.to_be_bytes());
        assert_eq!(opener.open_at(2, &mut fake, t + ATTEMPT_GAP), Err(Reject::BadTag));
        assert_eq!(opener.subkeys_derived(), 3);
    }

    #[test]
    fn a_new_session_retried_while_throttled_opens_from_the_cached_key() {
        let mut opener = Opener::new(&KEY, 1, [2]);
        let t = Instant::now();
        let mut wire = Vec::new();
        Sealer::new(&KEY, 2, 10).seal(1, b"x", &mut wire);
        opener.open_at(2, &mut wire, t).unwrap();
        let next = Sealer::new(&KEY, 2, 20);
        let mut first = Vec::new();
        next.seal(1, b"first", &mut first);
        let mut corrupt = first.clone();
        *corrupt.last_mut().unwrap() ^= 1;
        assert_eq!(opener.open_at(2, &mut corrupt, t), Err(Reject::BadTag));
        assert_eq!(opener.open_at(2, &mut first, t).unwrap(), b"first");
        assert_eq!(opener.subkeys_derived(), 2);
    }

    fn feed(w: &mut ReplayWindow, counter: u64) -> Result<(), Reject> {
        w.check(counter)?;
        w.accept(counter);
        Ok(())
    }

    #[test]
    fn the_window_accepts_reordering_and_rejects_duplicates() {
        let mut w = ReplayWindow::default();
        assert_eq!(feed(&mut w, 10), Ok(()));
        assert_eq!(feed(&mut w, 12), Ok(()));
        assert_eq!(feed(&mut w, 11), Ok(()));
        assert_eq!(feed(&mut w, 11), Err(Reject::Replay));
        assert_eq!(feed(&mut w, 12), Err(Reject::Replay));
        assert_eq!(feed(&mut w, 10), Err(Reject::Replay));
        assert_eq!(feed(&mut w, 5), Ok(()));
        assert_eq!(feed(&mut w, 13), Ok(()));
    }

    #[test]
    fn the_window_edge_is_1024_counters() {
        let mut w = ReplayWindow::default();
        feed(&mut w, 5000).unwrap();
        assert_eq!(feed(&mut w, 5000 - (WINDOW - 1)), Ok(()));
        assert_eq!(feed(&mut w, 5000 - WINDOW), Err(Reject::TooOld));
        assert_eq!(feed(&mut w, 0), Err(Reject::TooOld));
        feed(&mut w, 5000 + WINDOW).unwrap();
        assert_eq!(w.check(5001), Ok(()));
        assert_eq!(w.check(5000), Err(Reject::TooOld));
        assert_eq!(w.check(5000 + WINDOW), Err(Reject::Replay));
    }

    #[test]
    fn the_window_slides_and_remembers_what_it_kept() {
        let mut w = ReplayWindow::default();
        feed(&mut w, 0).unwrap();
        assert_eq!(feed(&mut w, 0), Err(Reject::Replay));
        feed(&mut w, 3).unwrap();
        feed(&mut w, 700).unwrap();
        assert_eq!(w.check(3), Err(Reject::Replay));
        assert_eq!(w.check(2), Ok(()));
        feed(&mut w, 1026).unwrap();
        assert_eq!(w.check(3), Err(Reject::Replay));
        assert_eq!(w.check(2), Err(Reject::TooOld));
        assert_eq!(w.check(700), Err(Reject::Replay));
        assert_eq!(w.check(701), Ok(()));
        assert_eq!(w.check(1026), Err(Reject::Replay));
        assert_eq!(w.check(1025), Ok(()));
        feed(&mut w, u64::MAX).unwrap();
        assert_eq!(w.check(1026), Err(Reject::TooOld));
        assert_eq!(w.check(u64::MAX), Err(Reject::Replay));
        assert_eq!(w.check(u64::MAX - 1), Ok(()));
    }

    #[test]
    fn a_datagram_overtaken_by_a_big_lobby_still_opens() {
        let sealer = Sealer::new(&KEY, 2, SESSION);
        let mut opener = Opener::new(&KEY, 1, [2]);
        let (mut late, mut wire) = (Vec::new(), Vec::new());
        sealer.seal(1, b"late", &mut late);
        for _ in 0..1000 {
            sealer.seal(1, b"on time", &mut wire);
            opener.open(2, &mut wire).unwrap();
        }
        assert_eq!(opener.open(2, &mut late).unwrap(), b"late");
    }

    #[test]
    fn a_failed_tag_leaves_the_window_alone() {
        let sealer = Sealer::new(&KEY, 2, SESSION);
        let mut good = Vec::new();
        sealer.seal_with_counter(1, 50, b"real", &mut good);
        let mut opener = Opener::new(&KEY, 1, [2]);
        assert_eq!(opener.open(2, &mut good.clone()).unwrap(), b"real");
        let mut forged = good.clone();
        forged[10..HEADER_LEN].copy_from_slice(&5000u64.to_be_bytes());
        assert_eq!(opener.open(2, &mut forged), Err(Reject::BadTag));
        let mut next = Vec::new();
        sealer.seal_with_counter(1, 49, b"older", &mut next);
        assert_eq!(opener.open(2, &mut next).unwrap(), b"older");
        assert_eq!(opener.open(2, &mut good), Err(Reject::Replay));
    }
}
