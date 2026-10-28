//! A stand-in for SwoftyLauncherServer's `/v1/mp` lobby, for session tests: one lobby, REST and
//! the websocket, following `docs/multiplayer-protocol.md` with the room key in the lobby from
//! the first join and `start` as the host's race marker.

use std::collections::HashMap;
use std::io::{Cursor, Read, Write};
use std::net::{TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex, mpsc};
use std::thread;
use std::time::Duration;

use serde_json::{Value, json};
use tungstenite::Message;

pub const KEY: &str = "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8";

#[derive(Clone)]
pub struct FakeUser {
    pub id: String,
    pub name: String,
}

struct Member {
    user: FakeUser,
    ip: u8,
    connected: bool,
    digest: Option<String>,
    blob: Option<Value>,
    relay: Option<String>,
}

struct Room {
    code: String,
    host: String,
    name: String,
    public: bool,
    category: String,
    max: u64,
    started: bool,
    rev: u64,
    next_ip: u8,
    members: Vec<Member>,
}

#[derive(Default)]
struct State {
    stall_leave: Duration,
    users: HashMap<String, FakeUser>,
    room: Option<Room>,
    sockets: HashMap<String, mpsc::Sender<Out>>,
}

enum Out {
    Text(String),
    Close(u16),
}

pub struct FakeServer {
    pub url: String,
    state: Arc<Mutex<State>>,
    stop: Arc<AtomicBool>,
}

impl FakeServer {
    pub fn start(users: &[(&str, &str, &str)]) -> FakeServer {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let url = format!("http://{}", listener.local_addr().unwrap());
        let state = Arc::new(Mutex::new(State {
            users: users
                .iter()
                .map(|(token, id, name)| {
                    (
                        (*token).to_owned(),
                        FakeUser {
                            id: (*id).to_owned(),
                            name: (*name).to_owned(),
                        },
                    )
                })
                .collect(),
            ..State::default()
        }));
        let stop = Arc::new(AtomicBool::new(false));
        listener.set_nonblocking(true).unwrap();
        {
            let (state, stop) = (state.clone(), stop.clone());
            thread::spawn(move || {
                while !stop.load(Ordering::Relaxed) {
                    match listener.accept() {
                        Ok((stream, _)) => {
                            let state = state.clone();
                            thread::spawn(move || serve(stream, &state));
                        }
                        Err(_) => thread::sleep(Duration::from_millis(5)),
                    }
                }
            });
        }
        FakeServer { url, state, stop }
    }

    /// Answer leave requests only after `delay`, as a server on a dead network would.
    pub fn stall_leave(&self, delay: Duration) {
        lock(&self.state).stall_leave = delay;
    }

    pub fn has_room(&self) -> bool {
        lock(&self.state).room.is_some()
    }

    pub fn member_count(&self) -> usize {
        lock(&self.state).room.as_ref().map_or(0, |r| r.members.len())
    }
}

impl Drop for FakeServer {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

fn lock(m: &Mutex<State>) -> std::sync::MutexGuard<'_, State> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

struct Replay {
    head: Cursor<Vec<u8>>,
    tcp: TcpStream,
}

impl Read for Replay {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        if (self.head.position() as usize) < self.head.get_ref().len() {
            return self.head.read(buf);
        }
        self.tcp.read(buf)
    }
}

impl Write for Replay {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.tcp.write(buf)
    }

    fn flush(&mut self) -> std::io::Result<()> {
        self.tcp.flush()
    }
}

fn serve(mut tcp: TcpStream, state: &Mutex<State>) {
    tcp.set_nonblocking(false).unwrap();
    tcp.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
    let mut raw = Vec::new();
    let mut buf = [0u8; 4096];
    let head_end = loop {
        if let Some(i) = raw.windows(4).position(|w| w == b"\r\n\r\n") {
            break i + 4;
        }
        match tcp.read(&mut buf) {
            Ok(0) | Err(_) => return,
            Ok(n) => raw.extend_from_slice(&buf[..n]),
        }
    };
    let head = String::from_utf8_lossy(&raw[..head_end]).into_owned();
    let mut lines = head.lines();
    let mut first = lines.next().unwrap_or("").split_whitespace();
    let (method, path) = (
        first.next().unwrap_or("").to_owned(),
        first.next().unwrap_or("").to_owned(),
    );
    let headers: HashMap<String, String> = lines
        .filter_map(|l| l.split_once(':'))
        .map(|(k, v)| (k.trim().to_ascii_lowercase(), v.trim().to_owned()))
        .collect();
    let token = headers
        .get("authorization")
        .and_then(|a| a.strip_prefix("Bearer "))
        .unwrap_or("")
        .to_owned();
    let user = lock(state).users.get(&token).cloned();
    if path.ends_with("/ws") {
        let replay = Replay {
            head: Cursor::new(raw),
            tcp: tcp.try_clone().unwrap(),
        };
        let Some(user) = user else {
            return respond(
                &mut tcp,
                401,
                &json!({"error": "sign in", "reason": "signed_out"}),
            );
        };
        let member = lock(state)
            .room
            .as_ref()
            .is_some_and(|r| r.members.iter().any(|m| m.user.id == user.id));
        if !member {
            return respond(
                &mut tcp,
                404,
                &json!({"error": "no open lobby has that code", "reason": "not_found"}),
            );
        }
        return websocket(replay, tcp, state, &user);
    }
    let length: usize = headers
        .get("content-length")
        .and_then(|l| l.parse().ok())
        .unwrap_or(0);
    let mut body = raw[head_end..].to_vec();
    while body.len() < length {
        match tcp.read(&mut buf) {
            Ok(0) | Err(_) => break,
            Ok(n) => body.extend_from_slice(&buf[..n]),
        }
    }
    let Some(user) = user else {
        return respond(
            &mut tcp,
            401,
            &json!({"error": "sign in", "reason": "signed_out"}),
        );
    };
    if path.ends_with("/leave") {
        let stall = lock(state).stall_leave;
        thread::sleep(stall);
    }
    let (status, answer) = route(state, &method, &path, &body, &user);
    respond(&mut tcp, status, &answer);
}

fn respond(tcp: &mut TcpStream, status: u16, body: &Value) {
    let text = if status == 204 {
        String::new()
    } else {
        body.to_string()
    };
    let answer = format!(
        "HTTP/1.1 {status} X\r\nContent-Type: application/json\r\nContent-Length: {}\r\nConnection: close\r\n\r\n{text}",
        text.len()
    );
    let _ = tcp.write_all(answer.as_bytes());
    let _ = tcp.flush();
}

fn view(room: &Room) -> Value {
    json!({
        "code": room.code, "host": room.host, "state": if room.started { "started" } else { "open" },
        "rev": room.rev, "name": room.name, "public": room.public, "category": room.category,
        "created_at": 1_793_000_000, "max_members": room.max, "can_start": !room.started, "blocker": null,
        "key": KEY,
        "members": room.members.iter().map(|m| {
            let host_digest = room.members.first().and_then(|h| h.digest.clone());
            json!({
                "id": m.user.id, "name": m.user.name, "avatar": null, "ip": format!("10.77.0.{}", m.ip),
                "host": m.user.id == room.host, "ready": false, "connected": m.connected,
                "digest": m.digest, "matches": m.digest.is_some() && m.digest == host_digest, "relay": m.relay,
            })
        }).collect::<Vec<_>>(),
    })
}

fn broadcast(s: &mut State) {
    let Some(room) = s.room.as_mut() else { return };
    room.rev += 1;
    let msg = json!({"type": "lobby", "lobby": view(room)}).to_string();
    for tx in s.sockets.values() {
        let _ = tx.send(Out::Text(msg.clone()));
    }
}

fn error(status: u16, reason: &str, text: &str) -> (u16, Value) {
    (status, json!({"error": text, "reason": reason}))
}

fn route(state: &Mutex<State>, method: &str, path: &str, body: &[u8], user: &FakeUser) -> (u16, Value) {
    let mut s = lock(state);
    let in_room = s
        .room
        .as_ref()
        .is_some_and(|r| r.members.iter().any(|m| m.user.id == user.id));
    match (method, path) {
        ("GET", "/v1/auth/launcher/me") => (
            200,
            json!({"user": {"id": user.id, "name": user.name, "avatar": null}, "multiplayer_allowed": true}),
        ),
        ("GET", "/v1/mp/rooms") => {
            let rooms: Vec<Value> = s
                .room
                .iter()
                .filter(|r| r.public && !r.started)
                .map(|r| {
                    json!({"code": r.code, "name": r.name, "host": {"id": r.host, "name": "Host", "avatar": null},
                           "members": r.members.len(), "max_members": r.max, "category": r.category,
                           "digest": r.members[0].digest, "addons": null, "created_at": 1})
                })
                .collect();
            (200, json!({ "rooms": rooms }))
        }
        ("POST", "/v1/mp/lobbies") => {
            if in_room {
                let code = s.room.as_ref().unwrap().code.clone();
                return (
                    409,
                    json!({"error": "already in a lobby", "reason": "in_lobby", "lobby": code}),
                );
            }
            let settings: Value = serde_json::from_slice(body).unwrap_or(json!({}));
            let room = Room {
                code: "F1-TEST22".into(),
                host: user.id.clone(),
                name: settings["name"]
                    .as_str()
                    .map_or_else(|| format!("{}'s room", user.name), str::to_owned),
                public: settings["public"].as_bool().unwrap_or(true),
                category: settings["category"].as_str().unwrap_or("f1-2026").to_owned(),
                max: settings["max_members"].as_u64().unwrap_or(20),
                started: false,
                rev: 0,
                next_ip: 2,
                members: vec![Member {
                    user: user.clone(),
                    ip: 1,
                    connected: false,
                    digest: None,
                    blob: None,
                    relay: None,
                }],
            };
            let lobby = view(&room);
            s.room = Some(room);
            (201, json!({ "lobby": lobby }))
        }
        _ => {
            let parts: Vec<&str> = path.trim_start_matches('/').split('/').collect();
            let (code, action) = match parts.as_slice() {
                ["v1", "mp", "lobbies", code] => (*code, ""),
                ["v1", "mp", "lobbies", code, action] => (*code, *action),
                ["v1", "mp", "turn"] => return error(503, "turn", "TURN is not configured"),
                _ => return error(404, "not_found", "no route"),
            };
            let Some(room) = s.room.as_mut().filter(|r| r.code.eq_ignore_ascii_case(code)) else {
                return error(404, "not_found", "no open lobby has that code");
            };
            match (method, action) {
                ("GET", "") if in_room => (200, json!({ "lobby": view(room) })),
                ("POST", "join") => {
                    if !in_room {
                        if room.started {
                            return error(409, "started", "this room is in a race; join after it ends");
                        }
                        let ip = room.next_ip;
                        room.next_ip += 1;
                        room.members.push(Member {
                            user: user.clone(),
                            ip,
                            connected: false,
                            digest: None,
                            blob: None,
                            relay: None,
                        });
                    }
                    let lobby = view(room);
                    broadcast(&mut s);
                    (200, json!({ "lobby": lobby }))
                }
                ("POST", "leave") if in_room => {
                    if room.host == user.id {
                        let txs: Vec<_> = s.sockets.drain().map(|(_, tx)| tx).collect();
                        for tx in txs {
                            let _ = tx.send(Out::Text(
                                json!({"type": "closed", "reason": "host_left"}).to_string(),
                            ));
                            let _ = tx.send(Out::Close(4002));
                        }
                        s.room = None;
                    } else {
                        room.members.retain(|m| m.user.id != user.id);
                        if let Some(tx) = s.sockets.remove(&user.id) {
                            let _ = tx.send(Out::Text(json!({"type": "left"}).to_string()));
                            let _ = tx.send(Out::Close(4003));
                        }
                        broadcast(&mut s);
                    }
                    (204, Value::Null)
                }
                _ => error(404, "not_found", "no open lobby has that code"),
            }
        }
    }
}

fn websocket(replay: Replay, tcp: TcpStream, state: &Mutex<State>, user: &FakeUser) {
    let Ok(mut ws) = tungstenite::accept(replay) else {
        return;
    };
    tcp.set_read_timeout(Some(Duration::from_millis(20))).unwrap();
    let (tx, rx) = mpsc::channel();
    {
        let mut s = lock(state);
        s.sockets.insert(user.id.clone(), tx.clone());
        if let Some(room) = s.room.as_mut() {
            for m in &mut room.members {
                if m.user.id == user.id {
                    m.connected = true;
                }
            }
        }
        broadcast(&mut s);
        if let Some(room) = s.room.as_ref() {
            for m in room.members.iter().filter(|m| m.user.id != user.id) {
                if let (Some(digest), Some(blob)) = (&m.digest, &m.blob) {
                    let msg =
                        json!({"type": "fingerprint", "member": m.user.id, "digest": digest, "blob": blob});
                    let _ = tx.send(Out::Text(msg.to_string()));
                }
            }
        }
    }
    'conn: loop {
        while let Ok(out) = rx.try_recv() {
            match out {
                Out::Text(text) => {
                    if ws.send(Message::text(text)).is_err() {
                        break 'conn;
                    }
                }
                Out::Close(code) => {
                    let _ = ws.close(Some(tungstenite::protocol::CloseFrame {
                        code: code.into(),
                        reason: "".into(),
                    }));
                    let _ = ws.flush();
                    return;
                }
            }
        }
        match ws.read() {
            Ok(Message::Text(text)) => {
                let msg: Value = serde_json::from_str(text.as_str()).unwrap_or(Value::Null);
                handle(state, user, &msg);
            }
            Ok(Message::Close(_)) => break,
            Ok(_) => {}
            Err(tungstenite::Error::Io(e))
                if matches!(
                    e.kind(),
                    std::io::ErrorKind::WouldBlock | std::io::ErrorKind::TimedOut
                ) => {}
            Err(_) => break,
        }
    }
    let mut s = lock(state);
    s.sockets.remove(&user.id);
    if let Some(room) = s.room.as_mut() {
        for m in &mut room.members {
            if m.user.id == user.id {
                m.connected = false;
            }
        }
    }
    broadcast(&mut s);
}

fn handle(state: &Mutex<State>, user: &FakeUser, msg: &Value) {
    let mut s = lock(state);
    let Some(room) = s.room.as_mut() else { return };
    let is_host = room.host == user.id;
    match msg["type"].as_str().unwrap_or("") {
        "fingerprint" => {
            let digest = msg["digest"].as_str().map(str::to_owned);
            let blob = msg.get("blob").cloned();
            if let Some(m) = room.members.iter_mut().find(|m| m.user.id == user.id) {
                m.digest = digest.clone();
                m.blob = blob.clone();
            }
            let out =
                json!({"type": "fingerprint", "member": user.id, "digest": digest, "blob": blob}).to_string();
            for (id, tx) in &s.sockets {
                if *id != user.id {
                    let _ = tx.send(Out::Text(out.clone()));
                }
            }
            broadcast(&mut s);
        }
        "relay" => {
            if let Some(m) = room.members.iter_mut().find(|m| m.user.id == user.id) {
                m.relay = msg["relay"].as_str().map(str::to_owned);
            }
            broadcast(&mut s);
        }
        "start" if is_host && !room.started => {
            room.started = true;
            let ips: serde_json::Map<String, Value> = room
                .members
                .iter()
                .map(|m| (m.user.id.clone(), Value::String(format!("10.77.0.{}", m.ip))))
                .collect();
            broadcast(&mut s);
            let out = json!({"type": "start", "host_ip": "10.77.0.1", "ips": ips, "key": KEY}).to_string();
            for tx in s.sockets.values() {
                let _ = tx.send(Out::Text(out.clone()));
            }
        }
        "reopen" if is_host && room.started => {
            room.started = false;
            broadcast(&mut s);
        }
        "ping" => {
            if let Some(tx) = s.sockets.get(&user.id) {
                let _ = tx.send(Out::Text(json!({"type": "pong"}).to_string()));
            }
        }
        _ => {}
    }
}
