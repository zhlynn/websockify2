# websockify2 — 高性能 WebSocket 到 TCP 代理

> 快速、轻量级 **WebSocket-TCP 代理/桥接器**。可作为 Python **websockify** 的直接替代品，**内存占用降低 10 倍**、**并发能力提升 10 倍**。

**关键词**：C 语言 websockify · websockify 替代 · WebSocket 代理 · WebSocket TCP 桥接 · noVNC 服务器 · WebSocket VNC 代理 · RFC 6455 服务器 · 高性能 WebSocket · epoll WebSocket · kqueue WebSocket · 嵌入式 WebSocket 代理 · C WebSocket 服务器 · wss 转 TCP · WebSocket 网关 · 浏览器 SSH 代理

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)   [English](README.md)

## 什么是 websockify2？

websockify2 在 **WebSocket** 和原始 **TCP** 之间搭建桥梁，让浏览器可以连接任何基于 TCP 的后端服务：

- **noVNC / 浏览器 VNC** —— 经典 websockify 场景，可直接替换
- **浏览器 SSH / xterm.js** —— 代理到 SSH 服务器
- **浏览器数据库客户端** —— Redis、MySQL、PostgreSQL
- **游戏服务器、IoT、MQTT、遗留系统** —— 任意 TCP 后端
- **微服务网关** —— 内部 TCP 服务的 WebSocket 前端

单进程事件驱动架构（epoll / kqueue / WSAPoll），**高性能、低内存占用**。

## 特色

- **单进程事件循环** —— `epoll`（Linux）/ `kqueue`（macOS、FreeBSD）/ `WSAPoll`（Windows），支持 **1 万+ 并发 WebSocket 连接**
- **每连接 FIFO 缓冲区**，边缘触发 I/O，8 字节对齐 WebSocket 掩码 XOR 优化
- **~60KB 编译二进制** —— 启动快，易于嵌入或容器化
- 完整 **RFC 6455 WebSocket** 协议、**SSL/TLS 自动检测**、**令牌路由多目标代理**、静态文件服务、守护进程、会话录制

## 安装

### Homebrew（macOS / Linux）

```bash
# 从本仓库 Formula 安装（源码构建，最新 main 分支）
brew install --HEAD --build-from-source \
    https://raw.githubusercontent.com/zhlynn/websockify2/main/Formula/websockify2.rb
```

### 源码编译

```bash
# 依赖
apt-get install build-essential libssl-dev   # Debian/Ubuntu
brew install openssl                         # macOS
apk add build-base openssl-dev               # Alpine

# 编译
sh configure.sh && make
sudo make install   # 可选，安装到 /usr/local/bin
```

支持 Linux (`epoll`)、macOS / FreeBSD (`kqueue`)、Windows (`WSAPoll`，通过 MinGW-w64)。

> Windows 使用 `WSAPoll` 而非 IOCP，适合中等并发。高并发生产部署建议使用 Linux。

## 使用

```
websockify2 [options] [listen_host:]listen_port [target_host:target_port]
```

### 常见场景

```bash
# 基本代理（noVNC）
websockify2 8080 localhost:5900

# 一体化 noVNC（静态文件 + 代理）
websockify2 --web /path/to/noVNC 8080 localhost:5900

# SSL/TLS
websockify2 -c server.pem -s 443 localhost:5900

# 守护进程
websockify2 -D -p /run/ws.pid -l /var/log/ws.log 8080 localhost:5900

# 多核扩展（通过 SO_REUSEPORT 启动多实例共享端口）
for i in 1 2 3 4; do websockify2 -D --pid-file /run/ws-$i.pid 8080 localhost:5900; done
```

### 选项

| 类别 | 选项 | 说明 |
|------|------|------|
| **监听 / 目标** | `listen_port` | 监听端口（必需，除非 `-U`） |
| | `-U, --unix-listen PATH` | 监听 Unix socket |
| | `-u, --unix-target PATH` | 连接 Unix socket 目标 |
| **SSL/TLS** | `-c, --cert FILE` | 证书（PEM） |
| | `-k, --key FILE` | 私钥（默认同证书） |
| | `-C, --ca-cert FILE` | 客户端验证 CA 证书 |
| | `-s, --ssl-only` | 拒绝非 SSL 连接 |
| | `--ssl-ciphers LIST` | 密码套件 |
| | `--verify-client` | 要求客户端证书 |
| **Web** | `-w, --web DIR` | 静态文件目录 |
| **路由** | `-t, --token-plugin PATH` | 令牌文件或目录 |
| | `-H, --host-token` | 从 `Host` 头提取令牌 |
| **守护进程** | `-D, --daemon` | 后台运行 |
| | `-p, --pid-file FILE` | PID 文件 |
| | `-l, --log-file FILE` | 日志文件（或 `syslog`） |
| | `-L, --log-level LEVEL` | `debug` / `info` / `warn` / `error` |
| **调优** | `-i, --idle-timeout N` | 空闲超时秒数 |
| | `-T, --timeout N` | N 秒后关闭 |
| | `--keepalive-idle/intvl/cnt` | TCP keepalive (60/10/5) |
| **杂项** | `-r, --record FILE` | 会话录制 |
| | `-v, --verbose` | 详细日志 |
| | `-h, --help` · `-V, --version` | |

### 令牌路由

文件格式（每行一条，`#` 开头为注释）：
```
vm1: 10.0.0.5:5900
vm2: 10.0.0.6:5901
```

客户端通过 `ws://host:8080/?token=vm1` 连接，或启用 `--host-token` 后通过 `ws://vm1.host:8080/`。

## 测试

```bash
make test              # 66 单元测试 + 9 集成测试
sh tests/docker_test.sh # Linux glibc + Alpine musl 跨平台

# 压测
./build/bin/echo_server 5900 &
./build/bin/websockify2 8080 localhost:5900 &
./build/bin/bench_connections 127.0.0.1 8080 1000 1024 10
```

## 架构

单进程、单线程、事件驱动。一个事件循环同时多路复用监听套接字与所有客户端、上游 fd。每条连接持有一小块自身状态(两对 FIFO 缓冲区、WebSocket 帧解析器、可选的 SSL 会话),无每连接线程,无共享可变状态。

### 数据流

```
    浏览器 / noVNC                                         TCP 后端
  ┌──────────────────┐                                   ┌─────────────────┐
  │ WebSocket 客户端 │                                    │  VNC / SSH /    │
  │  (wss:// 或 ws)  │                                   │  Redis 等        │
  └────────┬─────────┘                                   └────────▲────────┘
           │                                                      │
           │  TLS(可选)                                          │  原始 TCP
           │  + HTTP/1.1 Upgrade                                  │
           │  + RFC 6455 帧                                       │
           │                                                      │
           ▼                                                      │
  ┌───────────────────────────────────────────────────────────────┴────────┐
  │                           websockify2                                  │
  │                                                                        │
  │   监听 (SO_REUSEPORT)                                                  │
  │       │ accept()                                                       │
  │       ▼                                                                │
  │   ┌───────────────────────── 每连接 ──────────────────────────────┐    │
  │   │                                                               │    │
  │   │     client_recv ──► [ SSL 解密 ] ──► [ HTTP/WS 解析 ]          │    │
  │   │                                                │              │    │
  │   │                                        去掩码 + 组装帧         │    │
  │   │                                                ▼              │    │
  │   │                                          target_send  ────────┼───►│
  │   │                                                               │    │
  │   │     client_send ◄── [ SSL 加密 ] ◄── [ WS 帧封装 ]             │    │
  │   │                                                ▲              │    │
  │   │                                         target_recv  ◄────────┼────│
  │   │                                                               │    │
  │   │    状态: ACCEPTED → HANDSHAKE → CONNECTING → PROXYING          │    │
  │   └───────────────────────────────────────────────────────────────┘    │
  │                                                                        │
  │   事件循环: epoll (Linux) / kqueue (macOS、BSD) / WSAPoll (Windows)     │
  │             优先边缘触发,一次事件内抽干可读字节                         │
  └────────────────────────────────────────────────────────────────────────┘
```

### 连接生命周期

```
  accept()
     │
     ▼
  ACCEPTED   ──► 窥探首字节,嗅探 TLS ClientHello
     │                │
     │                └──► SSL 握手(若配置了证书)
     ▼
  HANDSHAKE  ──► 读取 HTTP 请求,校验 Upgrade / Sec-WebSocket-*
     │                │
     │                ├──► 静态文件(web 目录) ──► 响应并关闭
     │                └──► 101 Switching Protocols,解析目标地址
     ▼
  CONNECTING ──► 对 TCP 后端发起非阻塞 connect()
     │           (期间客户端数据缓存在 client_recv,等
     │            on_target_connect 触发后重放)
     ▼
  PROXYING   ──► 双向桥接,直到任一侧关闭
     │
     ▼
  CLOSED     ──► 刷写未发送字节,释放缓冲区
```

### 模块布局

| 层级         | 文件                                              | 职责                                          |
|--------------|---------------------------------------------------|-----------------------------------------------|
| 入口/循环    | `main.c`、`server.c`                              | CLI、信号、监听、事件分发                      |
| 状态机       | `proxy.c`                                         | 每连接状态、双向桥接、背压                     |
| 协议         | `ws.c`、`http.c`、`ssl_conn.c`                    | RFC 6455 帧、HTTP/1.1、OpenSSL 封装            |
| 功能         | `token.c`、`web.c`、`record.c`、`daemon.c`        | 令牌路由、静态文件、会话录制、守护进程         |
| 基础设施     | `event.c`、`buf.c`、`net.c`、`crypto.c`、`log.c`  | I/O 多路复用、FIFO 缓冲、套接字、SHA-1、日志   |
| 跨平台       | `platform.c`、`platform.h`                        | POSIX / Windows 抽象(套接字、时间、errno)    |

### 设计要点

- **缓冲区按需增长,不做池化**。每连接持有两读两写共四个 FIFO(`ws_buf_t`):初始为空,满时 1.5× `realloc`,排空时压缩,关闭时释放。无固定上限、无预分配池 —— 内存占用随实际负载变化。
- **边缘触发下抽干读**。`epoll` / `kqueue` 下,每次可读事件必须把套接字读空,循环不依赖下一次事件重新触发已入队的字节。
- **隐式背压**。慢速对端会让对应的发送 FIFO 增长,OS 的 TCP 窗口自然会抑制快的一侧。没有显式高水位 —— 这是刻意与 Python websockify 语义对齐的选择。
- **唯一全局上下文**。`server.c` 中的 `g_proxy_ctx` 是全进程唯一的共享状态;这是单进程设计的刻意取舍,不是多 worker 的过渡脚手架。

## 常见问题

**可以替代 Python 版 websockify 吗？**
可以。websockify2 实现了相同的命令行接口和 WebSocket-TCP 桥接行为，内存占用显著降低、并发能力更强。适用于 noVNC 部署、浏览器 SSH，或任何需要 WebSocket 网关的场景。

**能和 noVNC 一起用吗？**
可以。`websockify2 --web /path/to/noVNC 6080 localhost:5900` 即可搭建完整的浏览器 VNC 方案。

**和 nginx / haproxy 的 WebSocket 代理有何区别？**
nginx/haproxy 能转发 WebSocket 连接，但无法将 WebSocket 帧翻译为原始 TCP。websockify2 专门用于将浏览器 WebSocket 客户端桥接到不支持 WebSocket 的后端。

**Windows 下可以用吗？**
可以，通过 MinGW-w64 编译（使用 `WSAPoll`）。Windows 下高并发场景仍建议使用 Linux（`epoll`）。

## License

MIT — 见 [LICENSE](LICENSE)

---

<sub>**相关搜索**：websocket tcp 代理 · websocket 桥接 · websockify C 实现 · websockify 重写 · websockify 替代品 · 高性能 WebSocket 服务器 · 低延迟 WebSocket · WebSocket VNC 网关 · WebSocket SSH 代理 · 浏览器到 TCP 网关 · RFC 6455 C · wss 代理 · noVNC websocket · WebSocket 反向代理 · C WebSocket 库 · 嵌入式 WebSocket 服务器 · 低内存 WebSocket · 单进程 WebSocket · 事件驱动 WebSocket.</sub>
