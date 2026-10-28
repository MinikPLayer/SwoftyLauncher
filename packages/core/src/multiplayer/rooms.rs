//! The lobby server's REST side (`/v1/mp` on SwoftyLauncherServer): the room list, creating and
//! joining rooms, the members-only lobby state, and TURN credentials. Every call carries the
//! Discord sign-in's session token (see [`crate::mpauth`]).

use std::fmt;
use std::net::Ipv4Addr;
use std::time::Duration;

use serde::{Deserialize, Serialize};
use serde_json::Value;

use zeroize::{Zeroize, ZeroizeOnDrop};

use super::game::Category;

const TIMEOUT: Duration = Duration::from_secs(20);
/// Leaving runs while the launcher tears a room down, on networks that may be gone.
const LEAVE_TIMEOUT: Duration = Duration::from_secs(3);

/// The server and the session token every call carries.
#[derive(Clone)]
pub struct Api {
    server: String,
    token: String,
}

impl fmt::Debug for Api {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Api")
            .field("server", &self.server)
            .finish_non_exhaustive()
    }
}

/// A failed call. `reason` is the server's stable code (`signed_out`, `not_found`, `in_lobby`,
/// `full`, `started`, `rate_limited`, …), or `network` when the server could not be reached.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ApiError {
    pub status: u16,
    pub reason: String,
    pub message: String,
    /// With `in_lobby`: the lobby the caller is already in.
    pub lobby: Option<String>,
    pub retry_after: Option<u64>,
}

impl fmt::Display for ApiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl std::error::Error for ApiError {}

impl ApiError {
    fn network(message: String) -> Self {
        Self {
            status: 0,
            reason: "network".into(),
            message,
            lobby: None,
            retry_after: None,
        }
    }

    pub(super) fn from_body(status: u16, body: &str) -> Self {
        let doc: Value = serde_json::from_str(body).unwrap_or(Value::Null);
        let text = |k: &str| doc.get(k).and_then(Value::as_str).map(str::to_owned);
        let fallback = match status {
            401 => "your sign-in expired; sign in again",
            403 => "your account can't use multiplayer",
            404 => "that room doesn't exist any more",
            429 => "too many requests; try again shortly",
            _ => "the multiplayer server refused the request",
        };
        Self {
            status,
            reason: text("reason").unwrap_or_else(|| {
                match status {
                    401 => "signed_out",
                    403 => "not_allowed",
                    404 => "not_found",
                    429 => "rate_limited",
                    _ => "server_error",
                }
                .into()
            }),
            message: text("error").unwrap_or_else(|| format!("{fallback} (HTTP {status})")),
            lobby: text("lobby"),
            retry_after: doc.get("retry_after").and_then(Value::as_u64),
        }
    }

    /// Whether trying again later can work: the server was unreachable, busy or rate limiting.
    pub fn transient(&self) -> bool {
        self.status == 0 || self.status == 429 || self.status >= 500
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct User {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub avatar: Option<String>,
}

/// The settings a host creates a room with.
#[derive(Debug, Clone, PartialEq, Eq, Serialize)]
pub struct RoomSettings {
    /// `None` is the server's `<host name>'s room`.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub name: Option<String>,
    pub public: bool,
    pub max_members: u8,
    pub category: Category,
}

impl Default for RoomSettings {
    fn default() -> Self {
        Self {
            name: None,
            public: true,
            max_members: 20,
            category: Category::F12026,
        }
    }
}

/// A public room in the list.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RoomSummary {
    pub code: String,
    pub name: String,
    pub host: User,
    pub members: u32,
    pub max_members: u32,
    pub category: String,
    /// The host's fingerprint digest, `None` until the host sends one.
    #[serde(default)]
    pub digest: Option<String>,
    #[serde(default)]
    pub addons: Option<RoomAddons>,
    #[serde(default)]
    pub created_at: u64,
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct RoomAddons {
    pub count: u32,
    #[serde(default)]
    pub tracks: Vec<String>,
}

/// The lobby state members see.
#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct Lobby {
    pub code: String,
    /// The host's user id.
    pub host: String,
    /// `open`, or `started` while the host has marked a race started.
    #[serde(default)]
    pub state: String,
    #[serde(default)]
    pub rev: u64,
    #[serde(default)]
    pub name: String,
    #[serde(default)]
    pub public: bool,
    #[serde(default)]
    pub category: String,
    #[serde(default)]
    pub created_at: u64,
    #[serde(default)]
    pub max_members: u32,
    #[serde(default)]
    pub members: Vec<LobbyMember>,
    /// The room key, unpadded base64url; never shown or logged.
    #[serde(default, skip_serializing)]
    pub key: Option<RoomKey>,
}

impl Lobby {
    pub fn started(&self) -> bool {
        self.state == "started"
    }

    pub fn member(&self, id: &str) -> Option<&LobbyMember> {
        self.members.iter().find(|m| m.id == id)
    }

    pub fn host_member(&self) -> Option<&LobbyMember> {
        self.member(&self.host)
    }
}

#[derive(Debug, Clone, PartialEq, Eq, Serialize, Deserialize)]
pub struct LobbyMember {
    pub id: String,
    pub name: String,
    #[serde(default)]
    pub avatar: Option<String>,
    /// The member's virtual IP, `10.77.0.N`.
    pub ip: String,
    #[serde(default)]
    pub host: bool,
    #[serde(default)]
    pub ready: bool,
    #[serde(default)]
    pub connected: bool,
    #[serde(default)]
    pub digest: Option<String>,
    #[serde(default)]
    pub matches: bool,
    /// The member's TURN relayed address, `ip:port`.
    #[serde(default)]
    pub relay: Option<String>,
}

impl LobbyMember {
    /// The member's number: the last octet of a `10.77.0.N` virtual IP.
    pub fn number(&self) -> Option<u8> {
        member_number(&self.ip)
    }
}

pub fn member_number(ip: &str) -> Option<u8> {
    let ip: Ipv4Addr = ip.parse().ok()?;
    let [a, b, c, n] = ip.octets();
    ((a, b, c) == (10, 77, 0) && (1..=254).contains(&n)).then_some(n)
}

/// The room key as the server sends it; wiped from memory when dropped.
#[derive(Clone, PartialEq, Eq, Serialize, Deserialize)]
#[serde(transparent)]
pub struct RoomKey(String);

impl Drop for RoomKey {
    fn drop(&mut self) {
        self.0.zeroize();
    }
}

impl ZeroizeOnDrop for RoomKey {}

impl fmt::Debug for RoomKey {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str("RoomKey(..)")
    }
}

impl RoomKey {
    /// The 32 key bytes, or `None` when it is not 43 characters of unpadded base64url.
    pub fn bytes(&self) -> Option<[u8; 32]> {
        decode_key(&self.0)
    }
}

impl From<&str> for RoomKey {
    fn from(s: &str) -> Self {
        Self(s.to_owned())
    }
}

fn decode_key(text: &str) -> Option<[u8; 32]> {
    let text = text.trim_end_matches('=');
    if text.len() != 43 {
        return None;
    }
    let mut out = [0u8; 32];
    let mut len = 0;
    let mut acc = 0u32;
    let mut bits = 0;
    for ch in text.bytes() {
        let v = match ch {
            b'A'..=b'Z' => ch - b'A',
            b'a'..=b'z' => ch - b'a' + 26,
            b'0'..=b'9' => ch - b'0' + 52,
            b'-' | b'+' => 62,
            b'_' | b'/' => 63,
            _ => {
                out.zeroize();
                return None;
            }
        };
        acc = (acc << 6) | u32::from(v);
        bits += 6;
        if bits >= 8 {
            bits -= 8;
            out[len] = (acc >> bits) as u8;
            len += 1;
            acc &= (1 << bits) - 1;
        }
    }
    acc.zeroize();
    Some(out)
}

/// One TURN allocation's credentials from `POST /v1/mp/turn`.
#[derive(Clone)]
pub struct TurnCredentials {
    pub urls: Vec<String>,
    pub username: String,
    pub credential: String,
    /// Unix seconds.
    pub expires_at: Option<u64>,
}

impl fmt::Debug for TurnCredentials {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("TurnCredentials")
            .field("urls", &self.urls)
            .field("expires_at", &self.expires_at)
            .finish_non_exhaustive()
    }
}

impl TurnCredentials {
    /// The first `iceServers` entry with `turn:` URLs and credentials.
    pub fn from_ice(doc: &Value) -> Option<Self> {
        let expires_at = doc.get("expires_at").and_then(Value::as_u64);
        doc.get("iceServers")?.as_array()?.iter().find_map(|server| {
            let urls: Vec<String> = match server.get("urls")? {
                Value::String(u) => vec![u.clone()],
                Value::Array(list) => list
                    .iter()
                    .filter_map(|u| u.as_str().map(str::to_owned))
                    .collect(),
                _ => return None,
            };
            let username = server.get("username")?.as_str()?.to_owned();
            let credential = server.get("credential")?.as_str()?.to_owned();
            urls.iter().any(|u| u.starts_with("turn:")).then_some(Self {
                urls,
                username,
                credential,
                expires_at,
            })
        })
    }
}

impl Api {
    /// The launcher's server ([`crate::catalog::base_url`]).
    pub fn new(token: &str) -> Self {
        Self::with_server(&crate::catalog::base_url(), token)
    }

    pub fn with_server(server: &str, token: &str) -> Self {
        Self {
            server: server.trim().trim_end_matches('/').to_owned(),
            token: token.to_owned(),
        }
    }

    pub fn server(&self) -> &str {
        &self.server
    }

    pub fn token(&self) -> &str {
        &self.token
    }

    /// The signed-in user.
    pub fn me(&self) -> Result<User, ApiError> {
        #[derive(Deserialize)]
        struct Me {
            user: User,
        }
        Ok(self.call::<Me>("GET", "/v1/auth/launcher/me", None)?.user)
    }

    pub fn rooms(&self) -> Result<Vec<RoomSummary>, ApiError> {
        #[derive(Deserialize)]
        struct Rooms {
            rooms: Vec<RoomSummary>,
        }
        Ok(self.call::<Rooms>("GET", "/v1/mp/rooms", None)?.rooms)
    }

    pub fn create(&self, settings: &RoomSettings) -> Result<Lobby, ApiError> {
        let body = serde_json::to_string(settings).expect("serialisable");
        Ok(self.call::<Wrapped>("POST", "/v1/mp/lobbies", Some(body))?.lobby)
    }

    pub fn join(&self, code: &str) -> Result<Lobby, ApiError> {
        let path = format!("/v1/mp/lobbies/{}/join", path_code(code)?);
        Ok(self.call::<Wrapped>("POST", &path, Some(String::new()))?.lobby)
    }

    /// Gives up after a few seconds.
    pub fn leave(&self, code: &str) -> Result<(), ApiError> {
        let path = format!("/v1/mp/lobbies/{}/leave", path_code(code)?);
        self.call_within::<Value>("POST", &path, Some(String::new()), LEAVE_TIMEOUT)
            .map(|_| ())
    }

    pub fn lobby(&self, code: &str) -> Result<Lobby, ApiError> {
        let path = format!("/v1/mp/lobbies/{}", path_code(code)?);
        Ok(self.call::<Wrapped>("GET", &path, None)?.lobby)
    }

    pub fn turn(&self) -> Result<TurnCredentials, ApiError> {
        let doc = self.call::<Value>("POST", "/v1/mp/turn", Some(String::new()))?;
        TurnCredentials::from_ice(&doc).ok_or_else(|| ApiError {
            status: 200,
            reason: "bad_answer".into(),
            message: "the server's TURN answer has no turn: server with credentials".into(),
            lobby: None,
            retry_after: None,
        })
    }

    /// The lobby websocket's URL: `ws://` for an `http://` server, `wss://` for `https://`.
    pub fn ws_url(&self, code: &str) -> Result<String, ApiError> {
        let base = if let Some(rest) = self.server.strip_prefix("https://") {
            format!("wss://{rest}")
        } else if let Some(rest) = self.server.strip_prefix("http://") {
            format!("ws://{rest}")
        } else {
            self.server.clone()
        };
        Ok(format!("{base}/v1/mp/lobbies/{}/ws", path_code(code)?))
    }

    fn call<T: serde::de::DeserializeOwned>(
        &self,
        method: &str,
        path: &str,
        body: Option<String>,
    ) -> Result<T, ApiError> {
        self.call_within(method, path, body, TIMEOUT)
    }

    fn call_within<T: serde::de::DeserializeOwned>(
        &self,
        method: &str,
        path: &str,
        body: Option<String>,
        timeout: Duration,
    ) -> Result<T, ApiError> {
        if let Some(rest) = self.server.strip_prefix("http://") {
            let authority = rest.split('/').next().unwrap_or("");
            if !super::ws::loopback(host_of(authority)) {
                return Err(ApiError {
                    status: 0,
                    reason: "insecure".into(),
                    message: format!(
                        "refusing to send the session token to {} over plain http; use https",
                        self.server
                    ),
                    lobby: None,
                    retry_after: None,
                });
            }
        }
        let url = format!("{}{path}", self.server);
        let auth = format!("Bearer {}", self.token);
        let sent = match body {
            Some(body) => ureq::post(&url)
                .header("Authorization", &auth)
                .header("User-Agent", "swofty-launcher")
                .header("Content-Type", "application/json")
                .config()
                .timeout_global(Some(timeout))
                .max_redirects(0)
                .http_status_as_error(false)
                .build()
                .send(body),
            None => {
                debug_assert_eq!(method, "GET");
                ureq::get(&url)
                    .header("Authorization", &auth)
                    .header("User-Agent", "swofty-launcher")
                    .config()
                    .timeout_global(Some(timeout))
                    .max_redirects(0)
                    .http_status_as_error(false)
                    .build()
                    .call()
            }
        };
        let mut response =
            sent.map_err(|e| ApiError::network(format!("couldn't reach {}: {e}", self.server)))?;
        let status = response.status().as_u16();
        let text = response
            .body_mut()
            .read_to_string()
            .map_err(|e| ApiError::network(format!("reading the answer to {method} {path}: {e}")))?;
        if !(200..300).contains(&status) {
            return Err(ApiError::from_body(status, &text));
        }
        let text = if text.trim().is_empty() { "null" } else { &text };
        serde_json::from_str(text).map_err(|e| ApiError {
            status,
            reason: "bad_answer".into(),
            message: format!("the server's answer to {method} {path} is not valid: {e}"),
            lobby: None,
            retry_after: None,
        })
    }
}

/// The host of `host[:port]` or `[v6]:port`, without brackets.
fn host_of(authority: &str) -> &str {
    let authority = authority.rsplit('@').next().unwrap_or(authority);
    if let Some(v6) = authority.strip_prefix('[') {
        return v6.split(']').next().unwrap_or("");
    }
    authority.split(':').next().unwrap_or("")
}

#[derive(Deserialize)]
struct Wrapped {
    lobby: Lobby,
}

/// An invite code as a path segment: letters, digits and dashes only.
fn path_code(code: &str) -> Result<String, ApiError> {
    let code = code.trim();
    if code.is_empty() || code.len() > 16 || !code.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'-') {
        return Err(ApiError {
            status: 0,
            reason: "bad_code".into(),
            message: format!("'{code}' is not an invite code (like F1-7KQ2MX)"),
            lobby: None,
            retry_after: None,
        });
    }
    Ok(code.to_ascii_uppercase())
}

/// The room list from the launcher's server.
pub fn list(token: &str) -> Result<Vec<RoomSummary>, ApiError> {
    Api::new(token).rooms()
}

pub fn create(token: &str, settings: &RoomSettings) -> Result<Lobby, ApiError> {
    Api::new(token).create(settings)
}

pub fn join(token: &str, code: &str) -> Result<Lobby, ApiError> {
    Api::new(token).join(code)
}

#[cfg(test)]
mod tests {
    use serde_json::json;

    use super::*;

    #[test]
    fn decodes_the_server_key() {
        let raw: Vec<u8> = (0..32).collect();
        let key = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";
        assert_eq!(RoomKey::from(key).bytes().unwrap().to_vec(), raw);
        assert_eq!(RoomKey::from("short").bytes(), None);
        assert_eq!(RoomKey::from(&*format!("{}!", &key[..42])).bytes(), None);
        assert_eq!(format!("{:?}", RoomKey::from(key)), "RoomKey(..)");
    }

    #[test]
    fn the_key_is_wiped_on_drop() {
        fn wiped<T: ZeroizeOnDrop>() {}
        wiped::<RoomKey>();
        wiped::<crate::multiplayer::bridge::BridgeConfig>();
        wiped::<crate::multiplayer::seal::Opener>();
        let cfg = crate::multiplayer::bridge::BridgeConfig {
            self_member: 1,
            members: vec![1],
            key: [0xab; 32],
            game_port: 21414,
        };
        assert!(!format!("{cfg:?}").to_ascii_lowercase().contains("ab, "));
    }

    #[test]
    fn reads_lobbies_and_members() {
        let lobby: Lobby = serde_json::from_value(json!({
            "code": "F1-7KQ2MX", "host": "1", "state": "open", "rev": 3, "name": "Room",
            "public": true, "category": "f1-2026", "created_at": 1, "max_members": 20,
            "can_start": true, "blocker": null, "key": "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8",
            "members": [
                {"id": "1", "name": "Host", "avatar": null, "ip": "10.77.0.1", "host": true,
                 "ready": false, "connected": true, "digest": "sha256:aa", "matches": true, "relay": "104.30.2.14:50123"},
                {"id": "2", "name": "Ana", "ip": "10.77.0.2", "connected": false}
            ]
        }))
        .unwrap();
        assert!(!lobby.started());
        assert_eq!(lobby.host_member().unwrap().number(), Some(1));
        assert_eq!(lobby.member("2").unwrap().number(), Some(2));
        assert!(lobby.key.as_ref().unwrap().bytes().is_some());
        assert!(!serde_json::to_string(&lobby).unwrap().contains("AAECAw"));
        assert_eq!(member_number("10.77.1.2"), None);
        assert_eq!(member_number("10.77.0.0"), None);
    }

    #[test]
    fn errors_carry_the_server_reason() {
        let e = ApiError::from_body(
            409,
            r#"{"error": "you are already in lobby F1-AAAAAA; leave it first", "reason": "in_lobby", "lobby": "F1-AAAAAA"}"#,
        );
        assert_eq!(
            (e.reason.as_str(), e.lobby.as_deref()),
            ("in_lobby", Some("F1-AAAAAA"))
        );
        assert!(!e.transient());
        let e = ApiError::from_body(
            429,
            r#"{"error": "slow down", "reason": "rate_limited", "retry_after": 12}"#,
        );
        assert_eq!(e.retry_after, Some(12));
        assert!(e.transient());
        assert_eq!(ApiError::from_body(502, "<html>").reason, "server_error");
    }

    #[test]
    fn finds_turn_credentials_in_ice_servers() {
        let doc = json!({
            "iceServers": [
                {"urls": ["stun:stun.cloudflare.com:3478"]},
                {"urls": ["turn:turn.cloudflare.com:3478?transport=udp", "turns:turn.cloudflare.com:5349?transport=tcp"],
                 "username": "u", "credential": "c"}
            ],
            "ttl": 14400, "expires_at": 1793014400, "lobby": "F1-7KQ2MX"
        });
        let creds = TurnCredentials::from_ice(&doc).unwrap();
        assert_eq!(creds.urls.len(), 2);
        assert_eq!((creds.username.as_str(), creds.credential.as_str()), ("u", "c"));
        assert_eq!(creds.expires_at, Some(1793014400));
        assert!(TurnCredentials::from_ice(&json!({"iceServers": [{"urls": "stun:x"}]})).is_none());
    }

    #[test]
    fn builds_urls_and_checks_codes() {
        let api = Api::with_server("https://api.f1.swofty.net/", "t");
        assert_eq!(
            api.ws_url("f1-7kq2mx").unwrap(),
            "wss://api.f1.swofty.net/v1/mp/lobbies/F1-7KQ2MX/ws"
        );
        let local = Api::with_server("http://127.0.0.1:8471", "t");
        assert_eq!(
            local.ws_url("7KQ2MX").unwrap(),
            "ws://127.0.0.1:8471/v1/mp/lobbies/7KQ2MX/ws"
        );
        assert_eq!(api.ws_url("../admin").unwrap_err().reason, "bad_code");
        assert!(!format!("{api:?}").contains("\"t\""));
    }

    #[test]
    fn sends_the_token_over_plain_http_only_to_this_machine() {
        for server in [
            "http://example.com",
            "http://10.0.0.5:8471",
            "http://127.0.0.1.example.com",
        ] {
            let e = Api::with_server(server, "t").rooms().unwrap_err();
            assert_eq!(e.reason, "insecure", "{server}");
        }
        for server in ["http://127.0.0.1:1", "http://localhost:1", "http://[::1]:1"] {
            let e = Api::with_server(server, "t").rooms().unwrap_err();
            assert_eq!(e.reason, "network", "{server}");
        }
        assert_eq!(host_of("[::1]:8471"), "::1");
        assert_eq!(host_of("user@127.0.0.1:80"), "127.0.0.1");
    }

    #[test]
    fn room_settings_leave_the_default_name_to_the_server() {
        let body = serde_json::to_value(RoomSettings::default()).unwrap();
        assert_eq!(
            body,
            json!({"public": true, "max_members": 20, "category": "f1-2026"})
        );
    }
}
