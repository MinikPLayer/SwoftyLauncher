//! A [`Transport`] over plain UDP straight to each member's address, for a LAN or one machine.

use std::collections::HashMap;
use std::io;
use std::net::{SocketAddr, ToSocketAddrs, UdpSocket};
use std::sync::RwLock;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};

use super::transport::Transport;

pub struct DirectUdp {
    socket: UdpSocket,
    local: SocketAddr,
    peers: RwLock<Peers>,
    unknown: AtomicU64,
}

#[derive(Default)]
struct Peers {
    by_member: HashMap<u8, SocketAddr>,
    by_addr: HashMap<SocketAddr, u8>,
}

impl DirectUdp {
    pub fn bind(addr: impl ToSocketAddrs) -> io::Result<Self> {
        let socket = UdpSocket::bind(addr)?;
        Ok(Self {
            local: socket.local_addr()?,
            socket,
            peers: RwLock::default(),
            unknown: AtomicU64::new(0),
        })
    }

    /// Datagrams dropped because they came from an address that isn't a member's.
    pub fn unknown_dropped(&self) -> u64 {
        self.unknown.load(Ordering::Relaxed)
    }
}

impl Transport for DirectUdp {
    fn local_addr(&self) -> SocketAddr {
        self.local
    }

    fn set_peers(&self, peers: &HashMap<u8, SocketAddr>) -> io::Result<()> {
        let by_member: HashMap<u8, SocketAddr> = peers.iter().map(|(&m, &a)| (m, canonical(a))).collect();
        let by_addr = by_member.iter().map(|(&m, &a)| (a, m)).collect();
        *self.peers.write().unwrap_or_else(|e| e.into_inner()) = Peers { by_member, by_addr };
        Ok(())
    }

    fn permitted(&self, member: u8) -> bool {
        self.peers
            .read()
            .unwrap_or_else(|e| e.into_inner())
            .by_member
            .contains_key(&member)
    }

    fn send(&self, to: u8, payload: &[u8]) -> io::Result<()> {
        let addr = self
            .peers
            .read()
            .unwrap_or_else(|e| e.into_inner())
            .by_member
            .get(&to)
            .copied();
        let addr = addr
            .ok_or_else(|| io::Error::new(io::ErrorKind::NotFound, format!("no address for member {to}")))?;
        self.socket.send_to(payload, addr).map(|_| ())
    }

    fn recv(&self, buf: &mut [u8], timeout: Duration) -> io::Result<Option<(u8, usize)>> {
        let deadline = Instant::now() + timeout;
        loop {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Ok(None);
            }
            self.socket
                .set_read_timeout(Some(left.max(Duration::from_millis(1))))?;
            match self.socket.recv_from(buf) {
                Ok((n, from)) => {
                    let member = self
                        .peers
                        .read()
                        .unwrap_or_else(|e| e.into_inner())
                        .by_addr
                        .get(&canonical(from))
                        .copied();
                    match member {
                        Some(m) => return Ok(Some((m, n))),
                        None => {
                            self.unknown.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                }
                Err(e) if timed_out(&e) => return Ok(None),
                Err(e) if e.kind() == io::ErrorKind::ConnectionReset => {}
                Err(e) => return Err(e),
            }
        }
    }
}

fn canonical(addr: SocketAddr) -> SocketAddr {
    SocketAddr::new(addr.ip().to_canonical(), addr.port())
}

/// Whether a socket read gave up at its timeout, which Unix reports as `WouldBlock` and
/// Windows as `TimedOut`.
pub(super) fn timed_out(e: &io::Error) -> bool {
    matches!(e.kind(), io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn pair() -> (DirectUdp, DirectUdp) {
        let a = DirectUdp::bind("127.0.0.1:0").unwrap();
        let b = DirectUdp::bind("127.0.0.1:0").unwrap();
        a.set_peers(&HashMap::from([(2, b.local_addr())])).unwrap();
        b.set_peers(&HashMap::from([(1, a.local_addr())])).unwrap();
        (a, b)
    }

    #[test]
    fn carries_datagrams_between_members() {
        let (a, b) = pair();
        a.send(2, b"to two").unwrap();
        let mut buf = [0u8; 64];
        assert_eq!(b.recv(&mut buf, Duration::from_secs(2)).unwrap(), Some((1, 6)));
        assert_eq!(&buf[..6], b"to two");
        assert!(a.send(3, b"nobody").is_err());
    }

    #[test]
    fn drops_strangers_and_times_out() {
        let (_a, b) = pair();
        let stranger = UdpSocket::bind("127.0.0.1:0").unwrap();
        stranger.send_to(b"hi", b.local_addr()).unwrap();
        let mut buf = [0u8; 64];
        assert_eq!(b.recv(&mut buf, Duration::from_millis(200)).unwrap(), None);
        assert_eq!(b.unknown_dropped(), 1);
    }
}
