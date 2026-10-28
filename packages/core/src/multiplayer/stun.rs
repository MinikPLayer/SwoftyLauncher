//! STUN and TURN message encoding (RFC 8489, RFC 8656) and TURN ChannelData framing.
//!
//! Only what a UDP TURN client needs: building requests and indications with long-term credentials,
//! MESSAGE-INTEGRITY (HMAC-SHA1) and FINGERPRINT, and reading responses, Data indications and
//! ChannelData frames.

use std::net::{IpAddr, Ipv4Addr, Ipv6Addr, SocketAddr};

use md5::{Digest as _, Md5};
use sha1::Sha1;

pub const MAGIC_COOKIE: u32 = 0x2112_A442;
pub const HEADER_LEN: usize = 20;
const FINGERPRINT_XOR: u32 = 0x5354_554E;
const INTEGRITY_LEN: usize = 20;

pub mod method {
    pub const BINDING: u16 = 0x001;
    pub const ALLOCATE: u16 = 0x003;
    pub const REFRESH: u16 = 0x004;
    pub const SEND: u16 = 0x006;
    pub const DATA: u16 = 0x007;
    pub const CREATE_PERMISSION: u16 = 0x008;
    pub const CHANNEL_BIND: u16 = 0x009;
}

pub mod attr {
    pub const MAPPED_ADDRESS: u16 = 0x0001;
    pub const USERNAME: u16 = 0x0006;
    pub const MESSAGE_INTEGRITY: u16 = 0x0008;
    pub const ERROR_CODE: u16 = 0x0009;
    pub const CHANNEL_NUMBER: u16 = 0x000C;
    pub const LIFETIME: u16 = 0x000D;
    pub const XOR_PEER_ADDRESS: u16 = 0x0012;
    pub const DATA: u16 = 0x0013;
    pub const REALM: u16 = 0x0014;
    pub const NONCE: u16 = 0x0015;
    pub const XOR_RELAYED_ADDRESS: u16 = 0x0016;
    pub const REQUESTED_TRANSPORT: u16 = 0x0019;
    pub const PASSWORD_ALGORITHM: u16 = 0x001D;
    pub const XOR_MAPPED_ADDRESS: u16 = 0x0020;
    pub const PASSWORD_ALGORITHMS: u16 = 0x8002;
    pub const SOFTWARE: u16 = 0x8022;
    pub const FINGERPRINT: u16 = 0x8028;
}

/// Registered comprehension-required attributes (0x0000-0x7FFF) a response may carry without
/// failing the transaction; any other one does (RFC 8489 §6.3.3).
const KNOWN_REQUIRED: &[u16] = &[
    0x0001, 0x0006, 0x0008, 0x0009, 0x000A, 0x000C, 0x000D, 0x0012, 0x0013, 0x0014, 0x0015, 0x0016, 0x0017,
    0x0018, 0x0019, 0x001A, 0x001B, 0x001C, 0x001D, 0x001E, 0x0020, 0x0022, 0x0024, 0x0025, 0x0026, 0x0027,
    0x002A,
];

/// REQUESTED-TRANSPORT value for UDP (protocol 17, then three reserved bytes).
pub const TRANSPORT_UDP: [u8; 4] = [17, 0, 0, 0];

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Class {
    Request,
    Indication,
    Success,
    Error,
}

fn message_type(method: u16, class: Class) -> u16 {
    let c = match class {
        Class::Request => 0b00,
        Class::Indication => 0b01,
        Class::Success => 0b10,
        Class::Error => 0b11,
    };
    (method & 0x000F) | ((method & 0x0070) << 1) | ((method & 0x0F80) << 2) | ((c & 1) << 4) | ((c & 2) << 7)
}

fn split_type(t: u16) -> (u16, Class) {
    let method = (t & 0x000F) | ((t >> 1) & 0x0070) | ((t >> 2) & 0x0F80);
    let class = match ((t >> 4) & 1) | ((t >> 7) & 2) {
        0 => Class::Request,
        1 => Class::Indication,
        2 => Class::Success,
        _ => Class::Error,
    };
    (method, class)
}

/// The long-term credential key: MD5(username ":" realm ":" password). The password is used as
/// given; SASLprep is not applied (TURN REST credentials are ASCII).
pub fn long_term_key(username: &str, realm: &str, password: &str) -> [u8; 16] {
    let mut key = [0u8; 16];
    key.copy_from_slice(&Md5::digest(format!("{username}:{realm}:{password}").as_bytes()));
    key
}

pub fn hmac_sha1(key: &[u8], data: &[u8]) -> [u8; 20] {
    let mut block = [0u8; 64];
    if key.len() > block.len() {
        block[..20].copy_from_slice(&Sha1::digest(key));
    } else {
        block[..key.len()].copy_from_slice(key);
    }
    let pad = |x: u8| block.map(|b| b ^ x);
    let inner = Sha1::new().chain_update(pad(0x36)).chain_update(data).finalize();
    let outer = Sha1::new().chain_update(pad(0x5C)).chain_update(inner).finalize();
    let mut mac = [0u8; 20];
    mac.copy_from_slice(&outer);
    mac
}

fn crc32(data: &[u8]) -> u32 {
    let mut crc = flate2::Crc::new();
    crc.update(data);
    crc.sum()
}

fn be16(b: &[u8], at: usize) -> u16 {
    u16::from_be_bytes([b[at], b[at + 1]])
}

fn xor_address(addr: SocketAddr, transaction: &[u8; 12]) -> Vec<u8> {
    let port = addr.port() ^ (MAGIC_COOKIE >> 16) as u16;
    let mut mask = MAGIC_COOKIE.to_be_bytes().to_vec();
    mask.extend_from_slice(transaction);
    let (family, ip) = match addr.ip() {
        IpAddr::V4(ip) => (1u8, ip.octets().to_vec()),
        IpAddr::V6(ip) => (2u8, ip.octets().to_vec()),
    };
    let mut v = vec![0, family];
    v.extend_from_slice(&port.to_be_bytes());
    v.extend(ip.iter().zip(&mask).map(|(a, m)| a ^ m));
    v
}

fn read_xor_address(v: &[u8], transaction: &[u8; 12]) -> Option<SocketAddr> {
    if v.len() < 4 {
        return None;
    }
    let port = be16(v, 2) ^ (MAGIC_COOKIE >> 16) as u16;
    let mut mask = [0u8; 16];
    mask[..4].copy_from_slice(&MAGIC_COOKIE.to_be_bytes());
    mask[4..].copy_from_slice(transaction);
    let ip = match (v[1], v.len()) {
        (1, 8) => {
            let mut o = [0u8; 4];
            o.iter_mut()
                .zip(&v[4..])
                .zip(&mask)
                .for_each(|((o, a), m)| *o = a ^ m);
            IpAddr::V4(Ipv4Addr::from(o))
        }
        (2, 20) => {
            let mut o = [0u8; 16];
            o.iter_mut()
                .zip(&v[4..])
                .zip(&mask)
                .for_each(|((o, a), m)| *o = a ^ m);
            IpAddr::V6(Ipv6Addr::from(o))
        }
        _ => return None,
    };
    Some(SocketAddr::new(ip, port))
}

/// Builds one STUN message. MESSAGE-INTEGRITY and FINGERPRINT go last, in that order.
pub struct Builder {
    buf: Vec<u8>,
}

impl Builder {
    pub fn new(method: u16, class: Class, transaction: [u8; 12]) -> Builder {
        let mut buf = Vec::with_capacity(128);
        buf.extend_from_slice(&message_type(method, class).to_be_bytes());
        buf.extend_from_slice(&[0, 0]);
        buf.extend_from_slice(&MAGIC_COOKIE.to_be_bytes());
        buf.extend_from_slice(&transaction);
        Builder { buf }
    }

    fn transaction(&self) -> [u8; 12] {
        let mut t = [0u8; 12];
        t.copy_from_slice(&self.buf[8..20]);
        t
    }

    fn set_len(&mut self, body: usize) {
        self.buf[2..4].copy_from_slice(&(body as u16).to_be_bytes());
    }

    pub fn attr(mut self, kind: u16, value: &[u8]) -> Builder {
        self.buf.extend_from_slice(&kind.to_be_bytes());
        self.buf.extend_from_slice(&(value.len() as u16).to_be_bytes());
        self.buf.extend_from_slice(value);
        self.buf.resize(self.buf.len().next_multiple_of(4), 0);
        self
    }

    pub fn u32(self, kind: u16, value: u32) -> Builder {
        self.attr(kind, &value.to_be_bytes())
    }

    /// An XOR-*-ADDRESS attribute.
    pub fn address(self, kind: u16, addr: SocketAddr) -> Builder {
        let v = xor_address(addr, &self.transaction());
        self.attr(kind, &v)
    }

    /// CHANNEL-NUMBER: the channel, then two reserved bytes.
    pub fn channel(self, channel: u16) -> Builder {
        self.u32(attr::CHANNEL_NUMBER, u32::from(channel) << 16)
    }

    pub fn integrity(mut self, key: &[u8]) -> Builder {
        self.set_len(self.buf.len() - HEADER_LEN + 4 + INTEGRITY_LEN);
        let mac = hmac_sha1(key, &self.buf);
        self.attr(attr::MESSAGE_INTEGRITY, &mac)
    }

    pub fn fingerprint(mut self) -> Builder {
        self.set_len(self.buf.len() - HEADER_LEN + 8);
        let crc = crc32(&self.buf) ^ FINGERPRINT_XOR;
        self.u32(attr::FINGERPRINT, crc)
    }

    pub fn finish(mut self) -> Vec<u8> {
        self.set_len(self.buf.len() - HEADER_LEN);
        self.buf
    }
}

/// A parsed STUN message. Attributes after MESSAGE-INTEGRITY are ignored, except FINGERPRINT.
#[derive(Clone, Debug)]
pub struct Message {
    pub method: u16,
    pub class: Class,
    pub transaction: [u8; 12],
    raw: Vec<u8>,
    attrs: Vec<(u16, usize, usize)>,
}

impl Message {
    pub fn parse(buf: &[u8]) -> Option<Message> {
        if buf.len() < HEADER_LEN || buf[0] & 0xC0 != 0 {
            return None;
        }
        let len = usize::from(be16(buf, 2));
        let cookie = u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);
        if cookie != MAGIC_COOKIE || len % 4 != 0 || buf.len() < HEADER_LEN + len {
            return None;
        }
        let raw = buf[..HEADER_LEN + len].to_vec();
        let mut attrs = Vec::new();
        let mut at = HEADER_LEN;
        let mut sealed = false;
        while at < raw.len() {
            if at + 4 > raw.len() {
                return None;
            }
            let (kind, n) = (be16(&raw, at), usize::from(be16(&raw, at + 2)));
            let start = at + 4;
            if start + n > raw.len() {
                return None;
            }
            if !sealed || kind == attr::FINGERPRINT {
                attrs.push((kind, start, n));
            }
            sealed |= kind == attr::MESSAGE_INTEGRITY;
            at = start + n.next_multiple_of(4);
        }
        let (method, class) = split_type(be16(&raw, 0));
        let mut transaction = [0u8; 12];
        transaction.copy_from_slice(&raw[8..20]);
        Some(Message {
            method,
            class,
            transaction,
            raw,
            attrs,
        })
    }

    fn find(&self, kind: u16) -> Option<(usize, usize)> {
        self.attrs
            .iter()
            .find(|(k, ..)| *k == kind)
            .map(|&(_, start, n)| (start, n))
    }

    pub fn get(&self, kind: u16) -> Option<&[u8]> {
        self.find(kind).map(|(start, n)| &self.raw[start..start + n])
    }

    pub fn has(&self, kind: u16) -> bool {
        self.find(kind).is_some()
    }

    pub fn u32(&self, kind: u16) -> Option<u32> {
        let v = self.get(kind)?;
        Some(u32::from_be_bytes(v.get(..4)?.try_into().ok()?))
    }

    pub fn text(&self, kind: u16) -> Option<String> {
        self.get(kind).map(|v| String::from_utf8_lossy(v).into_owned())
    }

    /// An XOR-*-ADDRESS attribute.
    pub fn address(&self, kind: u16) -> Option<SocketAddr> {
        read_xor_address(self.get(kind)?, &self.transaction)
    }

    /// ERROR-CODE as (code, reason).
    pub fn error(&self) -> Option<(u16, String)> {
        let v = self.get(attr::ERROR_CODE)?;
        if v.len() < 4 {
            return None;
        }
        let code = u16::from(v[2] & 0x07) * 100 + u16::from(v[3]);
        Some((code, String::from_utf8_lossy(&v[4..]).into_owned()))
    }

    /// The first comprehension-required attribute this client doesn't know, if any.
    pub fn unknown_required(&self) -> Option<u16> {
        self.attrs
            .iter()
            .map(|(k, ..)| *k)
            .find(|k| *k < 0x8000 && !KNOWN_REQUIRED.contains(k))
    }

    /// Whether MESSAGE-INTEGRITY is present and matches `key`.
    pub fn check_integrity(&self, key: &[u8]) -> bool {
        let Some((start, n)) = self.find(attr::MESSAGE_INTEGRITY) else {
            return false;
        };
        if n != INTEGRITY_LEN {
            return false;
        }
        let mut signed = self.raw[..start - 4].to_vec();
        signed[2..4].copy_from_slice(&((start + n - HEADER_LEN) as u16).to_be_bytes());
        let mac = hmac_sha1(key, &signed);
        mac.iter()
            .zip(&self.raw[start..start + n])
            .fold(0u8, |acc, (a, b)| acc | (a ^ b))
            == 0
    }

    /// False only when a FINGERPRINT is present and wrong.
    pub fn fingerprint_ok(&self) -> bool {
        let Some((start, n)) = self.find(attr::FINGERPRINT) else {
            return true;
        };
        n == 4
            && start + n == self.raw.len()
            && crc32(&self.raw[..start - 4]) ^ FINGERPRINT_XOR
                == u32::from_be_bytes(self.raw[start..start + 4].try_into().unwrap_or_default())
    }
}

/// What a datagram from the TURN server is, by its first byte (RFC 7983).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Stun,
    ChannelData,
    Other,
}

pub fn kind(buf: &[u8]) -> Kind {
    match buf.first() {
        Some(0..=3) => Kind::Stun,
        Some(0x40..=0x7F) => Kind::ChannelData,
        _ => Kind::Other,
    }
}

/// Frame `data` for `channel`. Over UDP the trailing padding to a multiple of 4 is optional
/// (`pad = false`); over TCP it is required.
pub fn channel_data(channel: u16, data: &[u8], pad: bool) -> Vec<u8> {
    let mut out = Vec::with_capacity(4 + data.len() + 3);
    out.extend_from_slice(&channel.to_be_bytes());
    out.extend_from_slice(&(data.len() as u16).to_be_bytes());
    out.extend_from_slice(data);
    if pad {
        out.resize(out.len().next_multiple_of(4), 0);
    }
    out
}

/// Read a ChannelData frame. Bytes past the declared length are padding and are ignored; a frame
/// shorter than its declared length is dropped.
pub fn parse_channel_data(buf: &[u8]) -> Option<(u16, &[u8])> {
    if buf.len() < 4 {
        return None;
    }
    let channel = be16(buf, 0);
    let len = usize::from(be16(buf, 2));
    if !(0x4000..=0x7FFF).contains(&channel) || buf.len() < 4 + len {
        return None;
    }
    Some((channel, &buf[4..4 + len]))
}

#[cfg(test)]
mod tests {
    use super::*;

    fn hex(s: &str) -> Vec<u8> {
        s.split_whitespace()
            .map(|b| u8::from_str_radix(b, 16).unwrap())
            .collect()
    }

    const PASSWORD: &str = "VOkJxbRl1RmTxUk/WvJxBt";

    fn request() -> Vec<u8> {
        hex("00 01 00 58 21 12 a4 42 b7 e7 a7 01 bc 34 d6 86 fa 87 df ae
             80 22 00 10 53 54 55 4e 20 74 65 73 74 20 63 6c 69 65 6e 74
             00 24 00 04 6e 00 01 ff
             80 29 00 08 93 2f f9 b1 51 26 3b 36
             00 06 00 09 65 76 74 6a 3a 68 36 76 59 20 20 20
             00 08 00 14 9a ea a7 0c bf d8 cb 56 78 1e f2 b5 b2 d3 f2 49 c1 b5 71 a2
             80 28 00 04 e5 7a 3b cf")
    }

    fn response_v4() -> Vec<u8> {
        hex("01 01 00 3c 21 12 a4 42 b7 e7 a7 01 bc 34 d6 86 fa 87 df ae
             80 22 00 0b 74 65 73 74 20 76 65 63 74 6f 72 20
             00 20 00 08 00 01 a1 47 e1 12 a6 43
             00 08 00 14 2b 91 f5 99 fd 9e 90 c3 8c 74 89 f9 2a f9 ba 53 f0 6b e7 d7
             80 28 00 04 c0 7d 4c 96")
    }

    fn response_v6() -> Vec<u8> {
        hex("01 01 00 48 21 12 a4 42 b7 e7 a7 01 bc 34 d6 86 fa 87 df ae
             80 22 00 0b 74 65 73 74 20 76 65 63 74 6f 72 20
             00 20 00 14 00 02 a1 47 01 13 a9 fa a5 d3 f1 79 bc 25 f4 b5 be d2 b9 d9
             00 08 00 14 a3 82 95 4e 4b e6 7b f1 17 84 c9 7c 82 92 c2 75 bf e3 ed 41
             80 28 00 04 c8 fb 0b 4c")
    }

    fn long_term_request() -> Vec<u8> {
        hex("00 01 00 60 21 12 a4 42 78 ad 34 33 c6 ad 72 c0 29 da 41 2e
             00 06 00 12 e3 83 9e e3 83 88 e3 83 aa e3 83 83 e3 82 af e3 82 b9 00 00
             00 15 00 1c 66 2f 2f 34 39 39 6b 39 35 34 64 36 4f 4c 33 34 6f 4c 39 46
                         53 54 76 79 36 34 73 41
             00 14 00 0b 65 78 61 6d 70 6c 65 2e 6f 72 67 00
             00 08 00 14 f6 70 24 65 6d d6 4a 3e 02 b8 e0 71 2e 85 c9 a2 8c a8 96 66")
    }

    fn rebuild(vector: &[u8], key: &[u8], fingerprint: bool) -> Vec<u8> {
        let mi = vector.len() - if fingerprint { 32 } else { 24 };
        let mut b = Builder {
            buf: vector[..mi].to_vec(),
        }
        .integrity(key);
        if fingerprint {
            b = b.fingerprint();
        }
        b.finish()
    }

    #[test]
    fn rfc5769_request_integrity_and_fingerprint() {
        let v = request();
        assert_eq!(rebuild(&v, PASSWORD.as_bytes(), true), v);
        let m = Message::parse(&v).unwrap();
        assert_eq!((m.method, m.class), (method::BINDING, Class::Request));
        assert_eq!(m.text(attr::USERNAME).unwrap(), "evtj:h6vY");
        assert_eq!(m.text(attr::SOFTWARE).unwrap(), "STUN test client");
        assert!(m.check_integrity(PASSWORD.as_bytes()));
        assert!(!m.check_integrity(b"wrong"));
        assert!(m.fingerprint_ok());
    }

    #[test]
    fn rfc5769_ipv4_response() {
        let v = response_v4();
        assert_eq!(rebuild(&v, PASSWORD.as_bytes(), true), v);
        let m = Message::parse(&v).unwrap();
        assert_eq!((m.method, m.class), (method::BINDING, Class::Success));
        assert_eq!(
            m.address(attr::XOR_MAPPED_ADDRESS).unwrap(),
            "192.0.2.1:32853".parse().unwrap()
        );
        assert!(m.check_integrity(PASSWORD.as_bytes()));
        assert!(m.fingerprint_ok());
    }

    #[test]
    fn rfc5769_ipv6_response() {
        let v = response_v6();
        assert_eq!(rebuild(&v, PASSWORD.as_bytes(), true), v);
        let m = Message::parse(&v).unwrap();
        assert_eq!(
            m.address(attr::XOR_MAPPED_ADDRESS).unwrap(),
            "[2001:db8:1234:5678:11:2233:4455:6677]:32853".parse().unwrap()
        );
        assert!(m.check_integrity(PASSWORD.as_bytes()));
        assert!(m.fingerprint_ok());
    }

    #[test]
    fn rfc5769_long_term_request() {
        let v = long_term_request();
        let username = "\u{30DE}\u{30C8}\u{30EA}\u{30C3}\u{30AF}\u{30B9}";
        let key = long_term_key(username, "example.org", "TheMatrIX");
        assert_eq!(rebuild(&v, &key, false), v);
        let m = Message::parse(&v).unwrap();
        assert_eq!(m.text(attr::USERNAME).unwrap(), username);
        assert_eq!(m.text(attr::REALM).unwrap(), "example.org");
        assert_eq!(m.text(attr::NONCE).unwrap(), "f//499k954d6OL34oL9FSTvy64sA");
        assert!(m.check_integrity(&key));

        let built = Builder::new(method::BINDING, Class::Request, m.transaction)
            .attr(attr::USERNAME, username.as_bytes())
            .attr(attr::NONCE, b"f//499k954d6OL34oL9FSTvy64sA")
            .attr(attr::REALM, b"example.org")
            .integrity(&key)
            .finish();
        assert_eq!(built, v);
    }

    #[test]
    fn long_term_key_is_md5_of_user_realm_password() {
        assert_eq!(
            long_term_key("user", "realm", "pass"),
            *Md5::digest(b"user:realm:pass").as_slice()
        );
    }

    #[test]
    fn hmac_sha1_matches_rfc2202() {
        let mac = hmac_sha1(&[0x0b; 20], b"Hi There");
        assert_eq!(
            mac.to_vec(),
            hex("b6 17 31 86 55 05 72 64 e2 8b c0 b6 fb 37 8c 8e f1 46 be 00")
        );
        let mac = hmac_sha1(
            &[0xaa; 80],
            b"Test Using Larger Than Block-Size Key - Hash Key First",
        );
        assert_eq!(
            mac.to_vec(),
            hex("aa 4a e5 e1 52 72 d0 0e 95 70 56 37 ce 8a 3b 55 ed 40 21 12")
        );
    }

    #[test]
    fn tampered_messages_fail_checks() {
        let mut v = response_v4();
        v[30] ^= 1;
        let m = Message::parse(&v).unwrap();
        assert!(!m.check_integrity(PASSWORD.as_bytes()));
        assert!(!m.fingerprint_ok());
        assert!(Message::parse(&v[..v.len() - 2]).is_none());
        let mut bad_cookie = response_v4();
        bad_cookie[4] = 0;
        assert!(Message::parse(&bad_cookie).is_none());
    }

    #[test]
    fn message_types_round_trip() {
        for (m, c, t) in [
            (method::ALLOCATE, Class::Request, 0x0003),
            (method::ALLOCATE, Class::Success, 0x0103),
            (method::ALLOCATE, Class::Error, 0x0113),
            (method::SEND, Class::Indication, 0x0016),
            (method::DATA, Class::Indication, 0x0017),
            (method::CHANNEL_BIND, Class::Success, 0x0109),
        ] {
            assert_eq!(message_type(m, c), t);
            assert_eq!(split_type(t), (m, c));
        }
    }

    #[test]
    fn turn_attributes_round_trip() {
        let peer4: SocketAddr = "203.0.113.9:49152".parse().unwrap();
        let peer6: SocketAddr = "[2001:db8::7]:3478".parse().unwrap();
        let tid = [7u8; 12];
        let raw = Builder::new(method::CHANNEL_BIND, Class::Request, tid)
            .channel(0x4003)
            .address(attr::XOR_PEER_ADDRESS, peer4)
            .address(attr::XOR_RELAYED_ADDRESS, peer6)
            .u32(attr::LIFETIME, 600)
            .attr(attr::DATA, b"hello")
            .attr(attr::ERROR_CODE, &[0, 0, 4, 38, b'S', b't', b'a', b'l', b'e'])
            .fingerprint()
            .finish();
        assert_eq!(raw.len() % 4, 0);
        let m = Message::parse(&raw).unwrap();
        assert_eq!((m.method, m.class), (method::CHANNEL_BIND, Class::Request));
        assert_eq!(m.u32(attr::CHANNEL_NUMBER), Some(0x4003_0000));
        assert_eq!(m.address(attr::XOR_PEER_ADDRESS), Some(peer4));
        assert_eq!(m.address(attr::XOR_RELAYED_ADDRESS), Some(peer6));
        assert_eq!(m.u32(attr::LIFETIME), Some(600));
        assert_eq!(m.get(attr::DATA), Some(&b"hello"[..]));
        assert_eq!(m.error(), Some((438, "Stale".to_owned())));
        assert!(m.fingerprint_ok());
        assert_eq!(kind(&raw), Kind::Stun);
    }

    #[test]
    fn unknown_comprehension_required_attributes_are_flagged() {
        let tid = [1u8; 12];
        let known = Builder::new(method::ALLOCATE, Class::Success, tid)
            .u32(attr::LIFETIME, 600)
            .attr(0x8030, b"optional")
            .finish();
        assert_eq!(Message::parse(&known).unwrap().unknown_required(), None);
        let unknown = Builder::new(method::ALLOCATE, Class::Success, tid)
            .u32(attr::LIFETIME, 600)
            .attr(0x0030, b"required")
            .finish();
        assert_eq!(Message::parse(&unknown).unwrap().unknown_required(), Some(0x0030));
        let after_integrity = Builder::new(method::ALLOCATE, Class::Success, tid)
            .integrity(b"key")
            .finish();
        let mut tail = after_integrity.clone();
        tail.extend_from_slice(&[0x00, 0x30, 0x00, 0x00]);
        let len = (tail.len() - HEADER_LEN) as u16;
        tail[2..4].copy_from_slice(&len.to_be_bytes());
        assert_eq!(Message::parse(&tail).unwrap().unknown_required(), None);
    }

    #[test]
    fn channel_data_framing_and_padding() {
        for len in [0usize, 1, 2, 3, 4, 5, 1400] {
            let data: Vec<u8> = (0..len).map(|i| i as u8).collect();
            let udp = channel_data(0x4001, &data, false);
            assert_eq!(udp.len(), 4 + len);
            let tcp = channel_data(0x4001, &data, true);
            assert_eq!(tcp.len(), (4 + len).next_multiple_of(4));
            assert_eq!(&udp[..4], &[0x40, 0x01, (len >> 8) as u8, len as u8]);
            for frame in [&udp, &tcp] {
                assert_eq!(kind(frame), Kind::ChannelData);
                assert_eq!(parse_channel_data(frame), Some((0x4001, &data[..])));
            }
            if len > 0 {
                assert_eq!(parse_channel_data(&udp[..udp.len() - 1]), None);
            }
        }
        assert_eq!(parse_channel_data(&[0x30, 0x00, 0x00, 0x00]), None);
    }
}
