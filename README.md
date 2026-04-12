# websockify2 — High-Performance WebSocket to TCP Proxy

> A fast, lightweight **WebSocket-to-TCP proxy and bridge**. A drop-in alternative to Python **websockify** with **10x lower memory** and **10x higher concurrency**.

**Keywords**: websockify in C · websockify alternative · WebSocket proxy · WebSocket to TCP bridge · noVNC server · WebSocket VNC proxy · RFC 6455 server · high-performance WebSocket · epoll WebSocket · kqueue WebSocket · embedded WebSocket proxy · C WebSocket server · wss to TCP · WebSocket gateway

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)   [中文文档](README.zh-CN.md)

## What is websockify2?

Websockify2 bridges **WebSocket** traffic to raw **TCP**, letting browsers connect to any backend that speaks a TCP protocol:

- **noVNC / VNC over WebSocket** — classic websockify use case, ready to drop in
- **Browser SSH / xterm.js** — proxy to SSH servers
- **Database clients in the browser** — Redis, MySQL, PostgreSQL
- **Game servers, IoT, MQTT, legacy systems** — any TCP backend
- **Microservice gateway** — WebSocket frontend for internal TCP services

Uses a single-process event-driven design (epoll / kqueue / WSAPoll) for **high performance and low memory footprint**.

## Highlights

- **Single-process event loop** — `epoll` (Linux) / `kqueue` (macOS, FreeBSD) / `WSAPoll` (Windows), **10,000+ concurrent WebSocket connections**
- **Pre-allocated connection pool** — zero `malloc` on the hot path, **~70KB memory per connection**
- **Ring buffers** with O(1) operations, edge-triggered I/O, 8-byte aligned WebSocket mask XOR
- **~60KB compiled binary** — fast startup, easy to embed or containerize
- **Full RFC 6455 WebSocket** protocol, **SSL/TLS with auto-detection**, **token routing for multi-target proxying**, static file serving, daemon mode, session recording

## Install

### Homebrew (macOS / Linux)

```bash
# From this repo's Formula (build from source, latest main)
brew install --HEAD --build-from-source \
    https://raw.githubusercontent.com/zhlynn/websockify2/main/Formula/websockify2.rb
```

### Build from source

```bash
# Dependencies
apt-get install build-essential libssl-dev   # Debian/Ubuntu
brew install openssl                         # macOS
apk add build-base openssl-dev               # Alpine

# Build
sh configure.sh && make
sudo make install   # optional, installs to /usr/local/bin
```

Supports Linux (`epoll`), macOS / FreeBSD (`kqueue`), Windows (`WSAPoll` via MinGW-w64).

> Windows uses `WSAPoll` rather than IOCP — fine for moderate concurrency. For production-scale high-concurrency deployments, prefer Linux.

## Usage

```
websockify [options] [listen_host:]listen_port [target_host:target_port]
```

### Common scenarios

```bash
# Basic proxy (noVNC)
websockify 8080 localhost:5900

# All-in-one noVNC (static files + proxy)
websockify --web /path/to/noVNC 8080 localhost:5900

# SSL/TLS
websockify -c server.pem -s 443 localhost:5900

# Daemon
websockify -D -p /run/ws.pid -l /var/log/ws.log 8080 localhost:5900

# Multi-core (multiple instances share port via SO_REUSEPORT)
for i in 1 2 3 4; do websockify -D --pid-file /run/ws-$i.pid 8080 localhost:5900; done
```

### Options

| Category | Option | Description |
|----------|--------|-------------|
| **Listen / target** | `listen_port` | Listen port (required unless `-U`) |
| | `-U, --unix-listen PATH` | Unix domain socket listen |
| | `-u, --unix-target PATH` | Unix domain socket target |
| **SSL/TLS** | `-c, --cert FILE` | Certificate (PEM) |
| | `-k, --key FILE` | Private key (defaults to cert file) |
| | `-C, --ca-cert FILE` | CA cert for client verification |
| | `-s, --ssl-only` | Reject non-SSL connections |
| | `--ssl-ciphers LIST` | Cipher suite list |
| | `--verify-client` | Require client certificate |
| **Web** | `-w, --web DIR` | Serve static files from `DIR` |
| **Routing** | `-t, --token-plugin PATH` | Token file or directory |
| | `-H, --host-token` | Extract token from `Host` header |
| **Daemon** | `-D, --daemon` | Fork to background |
| | `-p, --pid-file FILE` | PID file path |
| | `-l, --log-file FILE` | Log file (or `syslog`) |
| | `-L, --log-level LEVEL` | `debug` / `info` / `warn` / `error` |
| **Tuning** | `-n, --max-connections N` | Default 10000 |
| | `-b, --buffer-size N` | Per-connection, default 65536 |
| | `-i, --idle-timeout N` | Close idle connections (seconds) |
| | `-T, --timeout N` | Shutdown after N seconds |
| | `--keepalive-idle/intvl/cnt` | TCP keepalive (60/10/5) |
| **Misc** | `-r, --record FILE` | Session recording |
| | `-v, --verbose` | Debug output |
| | `-h, --help` · `-V, --version` | |

### Token routing

File format (one per line, `#` for comments):
```
vm1: 10.0.0.5:5900
vm2: 10.0.0.6:5901
```

Client connects via `ws://host:8080/?token=vm1`, or with `--host-token` via `ws://vm1.host:8080/`.

## Testing

```bash
make test              # 66 unit + 9 integration tests
sh tests/docker_test.sh # Linux glibc + Alpine musl cross-platform

# Benchmark
./build/bin/echo_server 5900 &
./build/bin/websockify 8080 localhost:5900 &
./build/bin/bench_connections 127.0.0.1 8080 1000 1024 10
```

## Architecture

```
┌─────────────────────────────────────┐
│   Event Loop (epoll/kqueue/poll)    │
│            │                        │
│    Listener (SO_REUSEPORT)          │
│            │                        │
│   Connection pool (pre-allocated)   │
│   ├─ Ring buffer (per-connection)   │
│   ├─ State machine (proxy.c)        │
│   └─ SSL/TLS (OpenSSL, optional)    │
└─────────────────────────────────────┘
```

Modules: `server` (entry) → `proxy` (state machine) → `ws` / `http` / `ssl_conn` (protocols) → `event` / `buf` / `net` / `crypto` (infrastructure) → `platform` (cross-platform layer).

## FAQ

**Is this a replacement for Python websockify?**
Yes — websockify2 implements the same command-line interface and WebSocket-to-TCP bridging behavior, with substantially lower memory and higher concurrency. Drop it in for noVNC deployments, browser-based SSH, or any scenario where a WebSocket gateway is needed.

**Does it work with noVNC?**
Yes. `websockify --web /path/to/noVNC 6080 localhost:5900` gives you a complete browser-based VNC setup.

**How does it compare to nginx / haproxy WebSocket proxying?**
nginx/haproxy can forward WebSocket connections, but they cannot translate WebSocket frames into raw TCP. websockify2 is specifically for bridging browser WebSocket clients to backends that don't speak WebSocket.

**Can I use it on Windows?**
Yes, via MinGW-w64 (uses `WSAPoll`). For very high concurrency on Windows, Linux with `epoll` is recommended.

## License

MIT — see [LICENSE](LICENSE).

---

<sub>**Related terms**: websocket tcp proxy, websocket bridge, websockify c implementation, websockify rewrite, websockify replacement, high performance websocket server, low latency websocket, websocket vnc gateway, websocket ssh proxy, browser to tcp gateway, rfc 6455 c, wss proxy, noVNC websocket, websocket reverse proxy, C websocket library, embedded websocket server, low memory websocket, single-process websocket server, event-driven websocket.</sub>
