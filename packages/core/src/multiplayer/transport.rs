//! Moving sealed datagrams between lobby members.
//!
//! A member is addressed by its lobby number `N` (its virtual IP is `10.77.0.N`, the host is 1).
//! The bridge seals and opens datagrams; a transport only carries opaque bytes to and from members.

use std::collections::HashMap;
use std::io;
use std::net::SocketAddr;
use std::time::Duration;

/// Largest datagram a transport must carry: the game's 1264-byte receive buffer plus the
/// bridge's 34 bytes of header and tag, rounded up.
pub const MAX_DATAGRAM: usize = 1400;

pub trait Transport: Send + Sync {
    /// Where the other members send to reach this one: the TURN relayed address, or the local
    /// socket address for a direct transport.
    fn local_addr(&self) -> SocketAddr;

    /// Replace the member → address map. Members missing from `peers` are dropped.
    fn set_peers(&self, peers: &HashMap<u8, SocketAddr>) -> io::Result<()>;

    /// Whether datagrams to and from `member` can pass now: for TURN, the server holds a
    /// permission for its relayed address.
    fn permitted(&self, member: u8) -> bool {
        let _ = member;
        true
    }

    /// Send one datagram to member `to`.
    fn send(&self, to: u8, payload: &[u8]) -> io::Result<()>;

    /// Wait up to `timeout` for one datagram; `Ok(None)` on timeout. Datagrams from addresses
    /// that are not a known member are dropped.
    fn recv(&self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<(u8, usize)>>;
}
