<p align="center">
  <h1 align="center">🪢 ttach</h1>
  <p align="center"><i>Resume your shell after SSH or terminal disconnection — nothing else.</i></p>
</p>

<p align="center">
  <a href="https://github.com/tayyebi/ttach/releases/latest">
    <img src="https://img.shields.io/github/v/release/tayyebi/ttach?style=flat&label=release&color=blue" alt="Release">
  </a>
  <a href="LICENSE">
    <img src="https://img.shields.io/github/license/tayyebi/ttach?style=flat&color=green" alt="License">
  </a>
  <a href="https://github.com/tayyebi/ttach/actions/workflows/release.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/tayyebi/ttach/release.yml?branch=main&style=flat&label=release+builds&color=orange" alt="Release builds">
  </a>
  <a href="https://github.com/tayyebi/ttach/actions/workflows/ci.yml">
    <img src="https://img.shields.io/github/actions/workflow/status/tayyebi/ttach/ci.yml?branch=main&style=flat&label=tests&color=brightgreen" alt="CI">
  </a>
  <img src="https://img.shields.io/badge/language-C99-00599C?style=flat" alt="C99">
  <img src="https://img.shields.io/badge/dependencies-zero-brightgreen?style=flat" alt="Zero deps">
  <img src="https://img.shields.io/badge/size-%3E30KB-important?style=flat" alt="Size">
</p>

---

## ✨ What is `ttach`?

**ttach** is the smallest utility that does exactly **one thing**:

> 💬 *"Reconnect me to the shell I was using before the network disappeared."*

It is **not** tmux. It is **not** screen. It is **not** a terminal multiplexer.

No panes. No windows. No tabs. No scrollback. No config files. No plugins.

```mermaid
flowchart LR
    A[You]
    B[ttach]
    C[PTY + Shell]
    D[Unix socket]
    E[Network drops]
    F[You reconnect]

    A -- SSH / terminal --> B
    B -- creates --> C
    B -- listens on --> D
    C <--> D
    D <--> B
    B -.-> E
    E -.-> B
    F -- run ttach again --> B
    B -- attaches to --> C

    style A fill:#d4f0ff,stroke:#333
    style B fill:#fff3d4,stroke:#333
    style C fill:#d4ffd4,stroke:#333
    style D fill:#ffd4d4,stroke:#333
    style E fill:#ffcccc,stroke:#f00
    style F fill:#d4f0ff,stroke:#333
```

---

## 🚀 Quick Start

### ⬇️ Download

| Platform | Download |
|----------|----------|
| 🐧 **Linux** (x86_64) | [`ttach-linux`](https://github.com/tayyebi/ttach/releases/download/v1.0.0-alpha/ttach-linux) |
| 🪟 **Windows** (x86_64) | [`ttach-windows.exe`](https://github.com/tayyebi/ttach/releases/download/v1.0.0-alpha/ttach-windows.exe) |

> ⚡ Binaries are built automatically on every [release](https://github.com/tayyebi/ttach/releases). For the latest, grab from the newest release.

### 🐧 Linux

```bash
# Download
curl -LO https://github.com/tayyebi/ttach/releases/download/v1.0.0-alpha/ttach-linux

# Make executable
chmod +x ttach-linux

# (optional) Install system-wide
sudo mv ttach-linux /usr/local/bin/ttach
```

### 🪟 Windows

Download `ttach-windows.exe` and place it somewhere in your `PATH`, or run it directly from a terminal.

### ▶️ Usage

```bash
# First time — start a session
ttach

# Disconnect (SSH drops / terminal closes / network fails)
# ... any time later ...

# Reconnect — same command, same shell
ttach
```

That's it. No arguments. No flags. No session names.

---

### 🔌 Shell Integration

Add one of these snippets to `~/.bashrc` or `~/.zshrc` for automatic reconnect when you SSH in:

**Option A — Auto-attach if a session exists, start one if not:**

```bash
# ~/.bashrc or ~/.zshrc
if [ -z "$TTACH_SESSION" ] && command -v ttach >/dev/null 2>&1; then
    export TTACH_SESSION=1
    # Check if socket exists
    sock="$XDG_RUNTIME_DIR/ttach.sock"
    [ -z "$XDG_RUNTIME_DIR" ] && sock="/tmp/ttach-$(id -u).sock"
    if [ -S "$sock" ] 2>/dev/null; then
        exec ttach
    else
        # First time — start server in background, then attach
        nohup ttach >/dev/null 2>&1 &
        sleep 0.3
        exec ttach
    fi
fi
```

**Option B — Only attach if a session already exists (fall through to normal shell otherwise):**

```bash
# ~/.bashrc or ~/.zshrc
sock="$XDG_RUNTIME_DIR/ttach.sock"
[ -z "$XDG_RUNTIME_DIR" ] && sock="/tmp/ttach-$(id -u).sock"
if [ -z "$TTACH_SESSION" ] && [ -S "$sock" ] 2>/dev/null && command -v ttach >/dev/null 2>&1; then
    export TTACH_SESSION=1
    exec ttach
fi
```

**What happens:**

| You do | Result |
|--------|--------|
| `ssh server` | 🪢 Automatically attaches to your session |
| Network drops | 🐚 Shell keeps running inside ttach |
| `ssh server` again | 🪢 Reattaches exactly where you left off |
| `exit` inside the session | 🧹 Shell exits, ttach server cleans up, SSH disconnects |

```mermaid
sequenceDiagram
    participant U as You
    participant T as ttach
    participant S as Shell

    Note over U,S: First session
    U->>T: ttach
    T->>S: fork + exec login shell
    T-->>U: attached
    U-->>T: keystrokes
    T-->>S: forward keystrokes

    Note over U,S: Disconnection
    U--xT: SSH drops
    Note over T: Shell stays alive

    Note over U,S: Reconnection
    U->>T: ttach
    T->>T: connect to socket
    T-->>U: reattached
    U-->>T: keystrokes
    T-->>S: forward keystrokes
```

---

## 🧠 How It Works

```mermaid
flowchart TB
    subgraph server[ttach server]
        PTY[PTY master]
        SHELL[Shell]
        SOCK[Unix socket]
        RELAY[Relay loop]
        PTY <--> RELAY
        SOCK <--> RELAY
        PTY --> SHELL
    end

    subgraph client[ttach client]
        TERM[Your terminal]
        CRELAY[Relay loop]
        TERM <--> CRELAY
    end

    client --> server

    style server fill:#fff3d4,stroke:#333
    style client fill:#d4f0ff,stroke:#333
```

**Core architecture — step by step:**

1. **Server starts** — creates a PTY (pseudo-terminal), forks, and executes your login shell inside it
2. **Socket** — opens a Unix domain socket at `$XDG_RUNTIME_DIR/ttach.sock` (fallback: `/tmp/ttach-<uid>.sock`)
3. **Wait** — blocks on `accept()` for a client connection
4. **Client connects** — sends the terminal window size (4-byte handshake), then enters raw mode
5. **Relay** — `poll()` forwards bytes bidirectionally between your terminal and the PTY:
   - Your keystrokes → socket → PTY → shell
   - Shell output → PTY → socket → your terminal
6. **Disconnect** — client disappears; server keeps the shell alive and returns to step 3
7. **Reconnect** — another `ttach` invocation connects, handshakes, relay resumes

---

## 📋 When to use `ttach` vs `tmux` / `screen` / `dtach`

| Need | `ttach` | `tmux` | `screen` | `dtach` |
|------|---------|--------|----------|---------|
| Reconnect after SSH drop | ✅ | ✅ | ✅ | ✅ |
| Multiple windows/panes | ❌ | ✅ | ✅ | ❌ |
| Scrollback buffer | ❌ | ✅ | ✅ | ❌ |
| Session sharing | ❌ | ✅ | ✅ | ❌ |
| Configuration files | ❌ | ✅ | ✅ | ❌ |
| Scripting/plugins | ❌ | ✅ | ✅ | ❌ |
| Binary size | **~30KB** | ~2MB | ~1MB | ~50KB |
| Zero-config attach | ✅ **yes** | ❌ `tmux attach` | ❌ `screen -r` | ❌ `dtach -a` |
| Lines of code | **~600** | ~300K | ~100K | ~8K |

> 🎯 **Use `ttach` when:** you want exactly one thing — reconnect to your lost shell — and don't need a full terminal multiplexer.

---

## 🛠️ Building from Source

### Requirements

- C99 compiler (GCC or Clang)
- `make`
- POSIX system (Linux, BSD, macOS)

### Build

```bash
git clone https://github.com/tayyebi/ttach.git
cd ttach
make
sudo make install
```

The build is warning-free under `-Wall -Wextra -Wpedantic -std=c99`.

---

## ⚙️ Technical Details

| Aspect | Choice |
|--------|--------|
| Language | **C99** — no extensions, no compiler-specific code |
| Dependencies | **Zero** — only libc and POSIX APIs |
| PTY | `forkpty()` — creates a new pseudo-terminal |
| Event loop | `poll()` — no busy-waiting, no threads |
| Signals | `SIGCHLD`, `SIGTERM`, `SIGWINCH` handled; `SIGHUP`, `SIGINT` ignored |
| Socket | `AF_UNIX` (`SOCK_STREAM`) — `$XDG_RUNTIME_DIR/ttach.sock` or `/tmp/ttach-<uid>.sock` |
| Window size | 4-byte handshake on client connect |

**Signal handling:**

| Signal | Behavior |
|--------|----------|
| `SIGCHLD` | Shell exited → clean up and exit |
| `SIGTERM` | Forward to shell, clean up, exit |
| `SIGWINCH` | Query terminal size, update PTY |
| `SIGHUP` | **Ignored** — survives terminal close |
| `SIGINT` | **Ignored** — passes through to shell |

---

## 📜 Philosophy

Every feature must justify its existence. If removing it makes the code smaller without breaking the core promise — *reconnect me to my shell* — then the feature should be removed.

> 🧘 **ttach is intentionally less capable than dtach.**

One user. One session. One shell. No configuration.

---

## 📄 License

[GPL-3.0](LICENSE)
