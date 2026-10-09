# 03 · User Guide

**[简体中文](03-使用手册.md)** · **[English](03-user-guide.en.md)**

## 1. Quick Start

```bash
# 1. Build
make

# 2. Inspect without changing anything
./bin/jyfree --process        # Mythware processes
./bin/jyfree --modules        # loaded Mythware libraries

# 3. Inject (Mythware must be running, same UID)
./bin/jyfree

# 4. Use the interactive prompt
help                          # command list
status                        # injection status
counters                      # intercept counts ← verify hooks fire
quit                          # exit, hooks automatically restored
```

Graphical interface:

```bash
./bin/jyfree-gui              # then click "Inject"
./bin/jyfree-gui -i           # inject immediately on startup
```

---

## 2. Preconditions

| Condition | Note |
|---|---|
| **Same UID** | Must run as the same user as `Student` (`ptrace` requirement). **Root is not needed.** |
| aarch64 | Cross-compile, or build on the aarch64 machine itself |
| Mythware running | Inject after it starts; otherwise the watchdog waits and injects automatically |

Verify the UID matches:

```bash
# Mythware's UID
pgrep -a Student
ps -o uid= -p $(pgrep Student | head -1)

# this tool's UID
id -u
```

They must be identical.

---

## 3. Command-Line Options

```
Usage: jyfree [options]

  -i, --inject         inject Student and enter interactive mode (default)
  -p, --process        show Mythware processes and exit
  -m, --modules        show loaded Mythware libraries and exit
  -s, --status         show injection status and exit
  -c, --config FILE    load a config file
  -d, --debug N        debug level (0=off .. 5=trace)
  -n, --no-watchdog    do not start the watchdog
  -h, --help           show help
```

Examples:

```bash
./bin/jyfree -d 5               # maximum verbosity
./bin/jyfree -i -n              # inject without the watchdog
./bin/jyfree -c my.json         # use a custom config
```

---

## 4. Interactive Commands

Type these at the interactive prompt.

### 4.1 Status Queries

| Command | Description |
|---|---|
| `status` | Injection status, process state (including the `T` stopped hint), hook count, current feature bits |
| `process` | PID / exe / cmdline / state of `Student`, `StudentAgent`, `StudentService` |
| `modules` | Whether `libcastng` / `libDesk` / `libGetAppsInfo` / `libcast-x11` / `libjyfree` are loaded, plus intercept counts |
| `counters` | Per-feature intercept counts |
| `config show` | Current configuration |
| `log [file]` | Tail of the payload log (default `/tmp/jyfree-payload.log`) |

### 4.2 Feature Bits (the core mechanism)

Feature bits reach the payload through shared memory — **changes take effect
immediately, no re-injection needed**.

```bash
feat list              # list all bits and their on/off state
feat lock on           # enable lock-screen interception
feat monitor off       # disable monitoring interception
feat all on            # enable everything
feat capture off       # disable screen-capture interception
```

| Name | Bit | What it intercepts |
|---|---|---|
| `lock` | `0x01` | `ShowLockScreen` (lock), `ShowBlackScreen` (blank screen) |
| `monitor` | `0x02` | `StartMonitorPassive`, `StartRdpMonitorPassive`, `ProcessDeskMonitorCommand` |
| `command` | `0x04` | `ExecuteRemoteCmd`, `ProcessRemoteCommand` |
| `apps` | `0x08` | `KillProcess`, `CloseTopWindow`, `CloseApps`, `CloseWndByWindowId`, `DoAppPolicyControl` |
| `policy` | `0x10` | `UpdatePoliciesToStudentService`, `UpdateUsbPolicy...`, `UpdateWebPolicy...` |
| `input` | `0x20` | freerdp input callbacks + `XGrabKeyboard/Pointer`, `XTestFake*`, `XWarpPointer`, `XkbLockModifiers`, `XSendEvent` (types 2–6) |
| `capture` | `0x40` | `XGetImage` / `XShmGetImage` pixel overwrite |
| `all` | `0x7F` | everything |

### 4.3 Capture Modes

```bash
capture              # show the current mode
capture pass         # pass through (teacher sees the real screen)
capture freeze       # freeze (keep showing the same frame)
capture black        # all black
capture white        # all white
```

> Any mode other than `pass` automatically enables the `capture` bit.

**Important** — there are **two distinct paths** for screen content:

1. **Monitoring / capture** → `libDesk.so.2` → `XGetImage` ← this tool covers this
2. **Teacher broadcast** → RDP stream → `libcast-x11.so` ← a *different* path

If the `截屏` (capture) counter in `counters` stays at 0, the broadcast path is
the one in use.

### 4.4 Injection Control

| Command | Description |
|---|---|
| `inject` | manually (re-)inject |
| `uninject` | unload the payload (triggers the destructor, restoring all inline hooks) |
| `freeze` | `SIGSTOP` the Student process |
| `thaw` | `SIGCONT` the Student process |

### 4.5 Miscellaneous

| Command | Description |
|---|---|
| `debug <0-5>` | set the debug level |
| `config show\|save\|load` | configuration management |
| `quit` | exit (runs `uninject` automatically) |

---

## 5. Configuration File

Default path: `~/.config/jiyu-trainer/config.json`

```json
{
  "debug_level": 3,
  "run_mode": 0,
  "enable_gui": false,
  "enable_log": true,
  "discovery_port": 4788,
  "auto_reinject": true,
  "feature_mask": 127,
  "capture_mode": 1,
  "log_file": ""
}
```

| Field | Meaning |
|---|---|
| `debug_level` | 0=off 1=error 2=warn 3=info 4=verbose 5=trace |
| `feature_mask` | feature bit mask (decimal); `127` = all on |
| `capture_mode` | 0=pass 1=freeze 2=black 3=white |
| `auto_reinject` | watchdog auto re-injection |

```bash
./bin/jyfree -c /path/to/my.json
```

---

## 6. Graphical Interface

```bash
./bin/jyfree-gui
./bin/jyfree-gui -i        # inject on startup
./bin/jyfree-gui -d 4      # raise the log level
```

Layout:

```
┌────────────────────────────────────────┐
│ jyfree - 极域绕过 (UOS)              v2 │
├────────────────────────────────────────┤
│ [注入] [刷新] [日志] [截屏:冻结]        │
│ [锁屏拦截] [监视拦截] [命令拦截]         │
│ [输入拦截] [策略拦截] [应用拦截]         │
│                                        │
│ Student ●    Payload ●                 │
│ 已装钩子    拦截次数                    │
├────────────────────────────────────────┤
│ 就绪 - 点击注入开始                     │
└────────────────────────────────────────┘
```

- **注入** — perform ptrace injection
- **刷新** — refresh process / hook / counter state
- **日志** — open the live log window (scroll wheel to scroll, ESC to close)
- **截屏** — cycle 放行 (pass) → 冻结 (freeze) → 全黑 (black)
- The six feature buttons toggle their respective bits on click

The log window shows **controller** logs. Payload-internal logs go to
`/tmp/jyfree-payload.log`.

---

## 7. The Watchdog

Enabled by default (`-n` disables it). Polls every second:

1. `Student` pid changes → new instance → re-inject
2. `Student` `start_time` changes (pid reuse) → re-inject
3. Payload heartbeat stalls for 5 seconds → re-inject

**Why it exists**: `Student` was measured crashing via
`QThread destroyed while running` → abort → restarted by `StudentAgent`,
which invalidates the injection. The watchdog restores it.

Confirm it's working:

```bash
./bin/jyfree -d 4     # prints "检测到 Student 新实例 (pid=NNN), 重新注入"
```

---

## 8. Running as a Service

```bash
systemctl --user daemon-reload
systemctl --user enable jyfree.service
systemctl --user start jyfree.service

# logs
journalctl --user -u jyfree.service -f
```

See `deploy/jyfree.service`. It is a **user** unit (not a system unit) because
it must share the UID of `Student`.

> Mythware's own unit `com.mythware.mcm-student.service` sets
> `RefuseManualStop=yes`, so `systemctl --user stop` will not work on it;
> you must send signals directly.

---

## 9. Uninstalling / Restoring

```bash
./bin/jyfree          # interactive mode
> uninject            # triggers the payload destructor, restores all inline hooks
> quit
```

Or just `quit` (it runs `uninject` automatically).

**GOT hooks are not restored** — the process is about to exit or will be
re-injected anyway, and GOT slots still hold valid original pointers (the
payload keeps the originals, so calls still reach the real functions).

---

## 10. Typical Workflows

### Full Protection During Class

```bash
./bin/jyfree
> status
  Student 进程:   PID=12345
  payload 就绪:   是
  已装钩子数:     16
  当前开启:       锁屏 监视 命令 应用 策略 输入 截屏

> feat capture off      # let capture through so you can use the machine
... class in progress ...
> counters
  锁屏 3 <- 已拦截
  监视 1 <- 已拦截
  输入 12 <- 已拦截
> feat capture on       # re-enable capture interception
> quit
```

### Lock-Screen Protection Only

```bash
./bin/jyfree
> feat lock on
> feat monitor off
> feat command off
> feat input off
> feat apps off
> feat policy off
> feat capture off
> quit
```

### Debugging Why a Hook Isn't Firing

```bash
./bin/jyfree -d 5
> modules      # was the target library even loaded?
> counters     # are the counts climbing?
> log          # what did the payload observe?
```

---

## 11. Relevant Files and Paths

| Path | Contents |
|---|---|
| `/dev/shm/jyfree.state` | shared memory (feature bits, counters, heartbeat) |
| `/tmp/jyfree-payload.log` | payload-internal log |
| `~/.config/jiyu-trainer/config.json` | configuration |
| `~/.config/jiyu-trainer/jiyu-trainer.log` | controller log (rotates at 10MB) |

Inspect the shared memory:

```bash
ls -l /dev/shm/jyfree.state
```

---

For authorized local research and educational use only.
---

<div align="center">

**[⬆ Main documentation](../README.en.md)** · **[中文主文档 →](../README.md)**
**[Documentation index](README.en.md)** · **[中文索引 →](README.md)**

For authorized local research and educational use only.
Mythware is a registered trademark of Guangzhou Shirui Software Technology
Co., Ltd. This project is not affiliated with or endorsed by them.

</div>