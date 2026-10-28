//! A blocking websocket client for the lobby server.
//!
//! One thread owns a [`Socket`]: it reads with a short timeout and writes between reads, so no
//! async runtime is needed. `wss://` goes over rustls with the webpki roots, the same stack ureq
//! uses; `ws://` is plain TCP, for a server on this machine.

use std::io::{self, Read, Write};
use std::net::{SocketAddr, TcpStream, ToSocketAddrs};
use std::sync::Arc;
use std::time::Duration;

use rustls::pki_types::ServerName;
use tungstenite::client::IntoClientRequest;
use tungstenite::http::HeaderValue;
use tungstenite::protocol::WebSocket;
use tungstenite::{HandshakeError, Message};

/// How long a read waits before [`Socket::read`] returns `Ok(None)`.
pub const READ_TICK: Duration = Duration::from_millis(100);
const WRITE_TIMEOUT: Duration = Duration::from_secs(5);

pub enum Stream {
    Plain(TcpStream),
    Tls(Box<rustls::StreamOwned<rustls::ClientConnection, TcpStream>>),
}

impl Stream {
    fn tcp(&self) -> &TcpStream {
        match self {
            Stream::Plain(s) => s,
            Stream::Tls(s) => s.get_ref(),
        }
    }
}

impl Read for Stream {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match self {
            Stream::Plain(s) => s.read(buf),
            Stream::Tls(s) => s.read(buf),
        }
    }
}

impl Write for Stream {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        match self {
            Stream::Plain(s) => s.write(buf),
            Stream::Tls(s) => s.write(buf),
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        match self {
            Stream::Plain(s) => s.flush(),
            Stream::Tls(s) => s.flush(),
        }
    }
}

#[derive(Debug, thiserror::Error)]
pub enum ConnectError {
    /// The server answered the upgrade with an HTTP error; `body` is its JSON error, if any.
    #[error("the server refused the websocket with HTTP {status}")]
    Refused { status: u16, body: String },
    #[error("{0}")]
    Failed(String),
}

/// What [`Socket::read`] got.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Event {
    Text(String),
    /// The server closed the socket, with its close code when it sent one.
    Closed {
        code: Option<u16>,
        reason: String,
    },
}

pub struct Socket {
    ws: WebSocket<Stream>,
}

impl Socket {
    /// Open `url` (`ws://` or `wss://`) with `Authorization: Bearer <token>`.
    pub fn connect(url: &str, token: &str, timeout: Duration) -> Result<Socket, ConnectError> {
        let failed = |what: &str, e: &dyn std::fmt::Display| ConnectError::Failed(format!("{what}: {e}"));
        let mut request = url
            .into_client_request()
            .map_err(|e| failed("bad lobby URL", &e))?;
        let bearer =
            HeaderValue::from_str(&format!("Bearer {token}")).map_err(|e| failed("bad session token", &e))?;
        request.headers_mut().insert("Authorization", bearer);
        request
            .headers_mut()
            .insert("User-Agent", HeaderValue::from_static("swofty-launcher"));
        let uri = request.uri().clone();
        let tls = match uri.scheme_str() {
            Some("wss") => true,
            Some("ws") => false,
            _ => {
                return Err(ConnectError::Failed(format!(
                    "{url} is not a ws:// or wss:// URL"
                )));
            }
        };
        let host = uri
            .host()
            .ok_or_else(|| ConnectError::Failed(format!("{url} has no host")))?
            .trim_start_matches('[')
            .trim_end_matches(']')
            .to_owned();
        if !tls && !loopback(&host) {
            return Err(ConnectError::Failed(format!(
                "refusing to send the session token to {host} over plain ws://; use wss://"
            )));
        }
        let port = uri.port_u16().unwrap_or(if tls { 443 } else { 80 });
        let tcp =
            dial(&host, port, timeout).map_err(|e| failed(&format!("connecting to {host}:{port}"), &e))?;
        tcp.set_read_timeout(Some(timeout))
            .and_then(|()| tcp.set_write_timeout(Some(WRITE_TIMEOUT)))
            .and_then(|()| tcp.set_nodelay(true))
            .map_err(|e| failed("setting up the socket", &e))?;
        let stream = if tls {
            Stream::Tls(Box::new(tls_stream(&host, tcp)?))
        } else {
            Stream::Plain(tcp)
        };
        let (ws, _) = tungstenite::client::client(request, stream).map_err(|e| match e {
            HandshakeError::Failure(tungstenite::Error::Http(response)) => ConnectError::Refused {
                status: response.status().as_u16(),
                body: response
                    .body()
                    .as_deref()
                    .map(|b| String::from_utf8_lossy(b).into_owned())
                    .unwrap_or_default(),
            },
            HandshakeError::Failure(e) => failed("websocket handshake", &e),
            HandshakeError::Interrupted(_) => {
                ConnectError::Failed("the websocket handshake timed out".into())
            }
        })?;
        ws.get_ref()
            .tcp()
            .set_read_timeout(Some(READ_TICK))
            .map_err(|e| failed("setting up the socket", &e))?;
        Ok(Socket { ws })
    }

    pub fn send_text(&mut self, text: &str) -> io::Result<()> {
        self.ws.send(Message::text(text)).map_err(io_error)
    }

    /// The next text message or close, or `Ok(None)` when nothing came within [`READ_TICK`].
    /// Pings are answered here.
    pub fn read(&mut self) -> io::Result<Option<Event>> {
        loop {
            match self.ws.read() {
                Ok(Message::Text(text)) => return Ok(Some(Event::Text(text.as_str().to_owned()))),
                Ok(Message::Close(frame)) => {
                    let _ = self.ws.flush();
                    return Ok(Some(Event::Closed {
                        code: frame.as_ref().map(|f| u16::from(f.code)),
                        reason: frame.map(|f| f.reason.as_str().to_owned()).unwrap_or_default(),
                    }));
                }
                Ok(_) => continue,
                Err(tungstenite::Error::Io(e)) if timed_out(&e) => return Ok(None),
                Err(tungstenite::Error::ConnectionClosed | tungstenite::Error::AlreadyClosed) => {
                    return Ok(Some(Event::Closed {
                        code: None,
                        reason: String::new(),
                    }));
                }
                Err(e) => return Err(io_error(e)),
            }
        }
    }

    /// Send a close frame and drop the connection.
    pub fn close(mut self) {
        let _ = self.ws.close(None);
        let _ = self.ws.flush();
    }
}

/// Whether `host` is this machine: `localhost`, 127.0.0.0/8 or ::1. Only these get the session
/// token without TLS.
pub fn loopback(host: &str) -> bool {
    host.eq_ignore_ascii_case("localhost")
        || host
            .parse::<std::net::IpAddr>()
            .is_ok_and(|ip| ip.to_canonical().is_loopback())
}

fn dial(host: &str, port: u16, timeout: Duration) -> io::Result<TcpStream> {
    let mut addrs: Vec<SocketAddr> = (host, port).to_socket_addrs()?.collect();
    addrs.sort_by_key(SocketAddr::is_ipv6);
    let mut last = io::Error::new(io::ErrorKind::NotFound, format!("{host} has no address"));
    for addr in addrs {
        match TcpStream::connect_timeout(&addr, timeout) {
            Ok(s) => return Ok(s),
            Err(e) => last = e,
        }
    }
    Err(last)
}

fn tls_stream(
    host: &str,
    tcp: TcpStream,
) -> Result<rustls::StreamOwned<rustls::ClientConnection, TcpStream>, ConnectError> {
    let failed = |e: &dyn std::fmt::Display| ConnectError::Failed(format!("TLS to {host}: {e}"));
    let roots = rustls::RootCertStore {
        roots: webpki_roots::TLS_SERVER_ROOTS.to_vec(),
    };
    let config =
        rustls::ClientConfig::builder_with_provider(Arc::new(rustls::crypto::ring::default_provider()))
            .with_safe_default_protocol_versions()
            .map_err(|e| failed(&e))?
            .with_root_certificates(roots)
            .with_no_client_auth();
    let name = ServerName::try_from(host.to_owned()).map_err(|e| failed(&e))?;
    let conn = rustls::ClientConnection::new(Arc::new(config), name).map_err(|e| failed(&e))?;
    Ok(rustls::StreamOwned::new(conn, tcp))
}

fn timed_out(e: &io::Error) -> bool {
    matches!(e.kind(), io::ErrorKind::WouldBlock | io::ErrorKind::TimedOut)
}

fn io_error(e: tungstenite::Error) -> io::Error {
    match e {
        tungstenite::Error::Io(e) => e,
        e => io::Error::other(e.to_string()),
    }
}

#[cfg(test)]
mod tests {
    use std::net::TcpListener;

    use super::*;

    #[test]
    #[allow(clippy::result_large_err)]
    fn talks_to_a_local_server_with_the_bearer_token() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = std::thread::spawn(move || {
            let (stream, _) = listener.accept().unwrap();
            stream.set_read_timeout(Some(Duration::from_secs(5))).unwrap();
            let mut auth = String::new();
            let mut ws =
                tungstenite::accept_hdr(stream, |req: &tungstenite::handshake::server::Request, resp| {
                    auth = req.headers()["authorization"].to_str().unwrap().to_owned();
                    Ok(resp)
                })
                .unwrap();
            let got = ws.read().unwrap();
            ws.send(Message::text(format!("{auth} said {got}"))).unwrap();
            ws.close(Some(tungstenite::protocol::CloseFrame {
                code: 4002.into(),
                reason: "closed".into(),
            }))
            .unwrap();
            while ws.read().is_ok() {}
        });
        let url = format!("ws://127.0.0.1:{port}/v1/mp/lobbies/F1-TEST22/ws");
        let mut socket = Socket::connect(&url, "tok", Duration::from_secs(5)).unwrap();
        socket.send_text("hi").unwrap();
        let mut events = Vec::new();
        while events.len() < 2 {
            if let Some(e) = socket.read().unwrap() {
                events.push(e);
            }
        }
        assert_eq!(events[0], Event::Text("Bearer tok said hi".into()));
        assert!(matches!(events[1], Event::Closed { code: Some(4002), .. }));
        server.join().unwrap();
    }

    #[test]
    fn refuses_plain_ws_to_another_machine() {
        for url in [
            "ws://example.com/ws",
            "ws://10.0.0.5:8471/ws",
            "ws://[2001:db8::1]:80/ws",
        ] {
            match Socket::connect(url, "tok", Duration::from_secs(1)) {
                Err(ConnectError::Failed(why)) => assert!(why.contains("plain ws://"), "{url}: {why}"),
                Err(e) => panic!("{url}: unexpected {e}"),
                Ok(_) => panic!("{url} should be refused"),
            }
        }
        assert!(loopback("127.3.4.5") && loopback("::1") && loopback("LOCALHOST"));
        assert!(!loopback("127.0.0.1.example.com") && !loopback("192.168.1.2"));
    }

    #[test]
    fn reports_a_refused_upgrade_with_its_status_and_body() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let server = std::thread::spawn(move || {
            let (mut stream, _) = listener.accept().unwrap();
            let mut buf = [0u8; 2048];
            let _ = stream.read(&mut buf).unwrap();
            let body = r#"{"error":"no open lobby has that code","reason":"not_found"}"#;
            let answer = format!(
                "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: {}\r\n\r\n{body}",
                body.len()
            );
            stream.write_all(answer.as_bytes()).unwrap();
        });
        let url = format!("ws://127.0.0.1:{port}/v1/mp/lobbies/F1-GONE22/ws");
        match Socket::connect(&url, "tok", Duration::from_secs(5)) {
            Err(ConnectError::Refused { status, body }) => {
                assert_eq!(status, 404);
                assert!(body.contains("not_found"));
            }
            Err(e) => panic!("unexpected {e}"),
            Ok(_) => panic!("the upgrade should be refused"),
        }
        server.join().unwrap();
    }
}
