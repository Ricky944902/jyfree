<div align="center">

# jyfree

**Mythware Classroom Bypass Tool** — for UOS (统信) on aarch64

Injects into the `Student` process via `ptrace` and installs inline + GOT hooks
to intercept **lock screen, screen monitoring, remote control, application
policies, policy delivery, keyboard/mouse injection, and screen capture** —
all in real time.

> **No root required** — injection only needs the same UID as `Student`.

[![语言](https://img.shields.io/badge/lang-简体中文-informational?style=flat-square&logo=github)](README.md)
[![English](https://img.shields.io/badge/lang-English-informational?style=flat-square&logo=github)](README.en.md)
[![License](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](LICENSE)
[![Arch](https://img.shields.io/badge/arch-aarch64-orange?style=flat-square)](#target-environment)
[![C](https://img.shields.io/badge/c-C11-lightgrey?style=flat-square&logo=c)](src)
[![Deps](https://img.shields.io/badge/deps-pthread%20%7C%20dl%20%7C%20rt%20%7C%20X11-brightgreen?style=flat-square)](#dependencies)

</div>

---

<div align="center">

**[简体中文](README.md)** · **[English](README.en.md)**

</div>

---

> ⚠️ **Disclaimer** — For authorized local research and educational use only.
> Mythware is a registered trademark of Guangzhou Shirui Software Technology
> Co., Ltd. This project is not affiliated with or endorsed by them.
> Intended for authorized study of the underlying principles only.

---

> # ⚠️ NOT VERIFIED ON REAL HARDWARE — do not use this directly
> # We will do this in 1-2 weeks
> **This project has never been run on an actual aarch64 machine with Mythware
> installed.**
>
> The code compiles cleanly and is internally consistent, but the following are
> **completely unverified**:
>
> - ❌ Whether `ptrace` injection actually succeeds against a real `Student`
> - ❌ Whether the symbol addresses match Student **v2.7.3715** (other versions
>   will not work)
> - ❌ Whether `Student` crashes once hooks are installed
> - ❌ Whether any feature bit actually intercepts anything
> - ❌ Whether X11 hooks behave as expected under Wayland/XWayland
>
> **Do not treat this as a working tool.** This repository is a
> **reverse-engineering artifact plus ptrace/hook study material.**
>
> Lowest-risk way to try it on real hardware:
>
> ```bash
> ./bin/jyfree -d 5          # just check whether injection succeeds
> cat /tmp/jyfree-payload.log
> ```
>
> Once injection is confirmed working, enable **one feature bit at a time** and
> watch for crashes:
>
> ```bash
> > feat list
> > feat lock on            # lock screen only
> > counters                # see whether the count climbs
> ```
>
> If anything goes wrong:
>
> ```bash
> > uninject                # restores every inline hook
> > quit
> ```

---

## 30-Second Start

```bash
git clone <this-repo> && cd JiYuTrainer
make

# Inject (requires Mythware running, same UID as this tool)
./bin/jyfree

# Interactive prompt
> status       # injection status
> counters     # intercept counts ← verify hooks actually fire
> quit         # exit and restore all hooks
```

GUI: `./bin/jyfree-gui`

---

## What It Does

The Linux edition of Mythware implements most control features as plain
function calls inside a user-space process. jyfree hooks those entry points
so they return immediately:

| Bit | Name | Hooked functions | Effect |
|---|---|---|---|
| `0x01` | `lock` | `CStudentMainWork::ShowLockScreen`<br>`::ShowBlackScreen` | Lock / black screen never appears |
| `0x02` | `monitor` | `::StartMonitorPassive`<br>`::StartRdpMonitorPassive`<br>`::ProcessDeskMonitorCommand` | Teacher cannot see your screen |
| `0x04` | `command` | `::ExecuteRemoteCmd`<br>`::ProcessRemoteCommand` | Remote commands do nothing |
| `0x08` | `apps` | `libGetAppsInfo::KillProcess`<br>`::CloseTopWindow` / `CloseApps` | No process kills, no window closing |
| `0x10` | `policy` | `::UpdatePoliciesToStudentService`<br>`::UpdateUsbPolicy...` / `UpdateWebPolicy...` | Policies never delivered |
| `0x20` | `input` | `tf_peer_*_event` (freerdp)<br>`XGrab*` / `XTestFake*` / `XWarpPointer` | Teacher's mouse & keyboard are inert |
| `0x40` | `capture` | `XGetImage` / `XShmGetImage` (GOT) | Screen capture frozen or blacked out |

Feature bits travel through shared memory, so **changes take effect instantly
without re-injection**.

---

## How It Works

```
jyfree (controller, user space, same UID as Student)
 │
 ├─ ptrace injection engine
 │    ATTACH → save regs → remote mmap → remote dlopen → restore regs → DETACH
 │
 ├─ shared memory  /dev/shm/jyfree.state   ←→   feature bits / counters / heartbeat
 │
 └─ watchdog (1s poll)
      re-injects automatically after Student is restarted by systemd

libjyfree.so (payload, dlopen'd INSIDE the Student process)
 │
 ├─ inline hooks ×16
 │    aarch64 patch (16 bytes, no trampoline needed):
 │      ldr x16, #8
 │      br  x16
 │      .quad detour
 │
 ├─ GOT hooks ×11
 │    dl_iterate_phdr over every loaded ELF
 │    parse PT_DYNAMIC → DT_JMPREL/DT_RELA → match by name → rewrite
 │
 └─ coordinator thread
      1s heartbeat + detect libcast-x11.so loaded at runtime
```

The key enabler: `Student` is an **ET_EXEC non-PIE** binary, so addresses from
`nm` *are* the runtime addresses — the payload writes them straight into the
patch.

```
$ nm /opt/mythware/classroom-management/Student | grep ShowLockScreen
0000000000441ed4 T _ZN16CStudentMainWork14ShowLockScreenEi
                 ^^^^^^^^ the runtime address
```

---

## Project Layout

```
JiYuTrainer/
├── README.md                     ← this file (Chinese, primary)
├── README.en.md                  ← English
├── LICENSE                       ← MIT
│
├── bin/                          ← build output
│   ├── jyfree                    ★ main binary (CLI)
│   ├── jyfree-gui                   GUI
│   └── libjyfree.so                 payload (must sit next to jyfree)
│
├── include/                      ← headers
│   ├── jiyu.h                       controller API
│   ├── payload_state.h            ★ shared by both sides (bits/symbols/structs)
│   └── gui.h                       Xlib GUI
│
├── src/                          ← sources
│   ├── main.c                       CLI entry + command table
│   ├── inject.c                     ptrace engine + shared memory + watchdog
│   ├── hook_payload.c            ★  payload (built into libjyfree.so)
│   ├── process.c                    /proc scanning for Student
│   ├── gui.c / gui_main.c           Xlib GUI
│   └── utils.c                      config / logging / debug
│
├── scripts/                      ← helper scripts
│   ├── build.sh                  ★  one-shot build (deps + arch check)
│   ├── install.sh                   one-shot install
│   └── extract-mythware.sh          file extractor (TUI)
│
├── deploy/                       ← deployment
│   ├── jyfree.service                systemd user unit
│   └── jyfree.desktop                desktop entry
│
├── docs/                         ← documentation (bilingual)
│   ├── README.md                     index
│   ├── 01-reverse-engineering.en.md / 01-逆向分析.md
│   ├── 02-build-guide.en.md     / 02-编译指南.md
│   ├── 03-user-guide.en.md      / 03-使用手册.md
│   └── 04-troubleshooting.en.md / 04-排错手册.md
│
└── obj/                          ← intermediate objects (safe to delete)
```

---

## Dependencies

### Required (all provided by the base system)

| Dependency | Purpose |
|---|---|
| `gcc` ≥ 6, `make` | build |
| `libc.so.6` | **the only runtime library dependency** — present on every Linux |

**No development libraries are needed at build time.** Since glibc ≥ 2.34,
`pthread` / `dlopen` / `shm_open` / `ptrace` all live in `libc`; the separate
`libpthread` / `libdl` / `librt` files are now empty stubs.

```bash
# UOS / Debian based
sudo apt install build-essential

# Fedora / RHEL
sudo dnf install gcc make
```

### Optional (graphical build only)

```bash
sudo apt install libx11-dev        # needed only by jyfree-gui
```

**The build still succeeds without it** — `make` auto-detects X11 and skips
the GUI target; the CLI is fully functional.

```bash
ldd bin/jyfree
#   linux-vdso.so.1
#   libc.so.6 => /lib/x86_64-linux-gnu/libc.so.6
#   /lib64/ld-linux-x86-64.so.2      ← zero external dependencies
```

Legacy systems (glibc < 2.34): use `make OLD_GLIBC=1`.

Details: [`docs/02-build-guide.en.md`](docs/02-build-guide.en.md) · [中文](docs/02-编译指南.md)

---

## Building

```bash
# Option 1: script (recommended — checks deps and architecture)
./scripts/build.sh

# Option 2: Makefile
make              # everything
make cli          # CLI only
make gui          # GUI only
make payload      # payload only
make debug        # debug build (-O0 -g3 -DDEBUG)
make install      # install to /usr/local
make arch-check   # verify architecture matches
make help         # all targets
```

**Cross-compiling** (building aarch64 on an x86 host):

```bash
./scripts/build.sh --cross
# or
make ARCH=aarch64 CROSS=aarch64-linux-gnu-
```

> ⚠️ **Architecture matters** — the inline hook addresses in the payload
> (`0x441ed4`, …) are **aarch64-only**. Building on x86 verifies that the code
> compiles; it will **not** run correctly. Use `make arch-check`.

Details: [`docs/02-build-guide.en.md`](docs/02-build-guide.en.md) · [中文](docs/02-编译指南.md)

---

## Usage

### Main binary `bin/jyfree`

```bash
./bin/jyfree              # inject, then interactive mode
./bin/jyfree --status     # show status and exit
./bin/jyfree --process    # list Mythware processes
./bin/jyfree --modules    # list loaded Mythware libraries
./bin/jyfree -d 5         # maximum verbosity
```

### Interactive Commands

| Command | Description |
|---|---|
| `status` | Injection status, process state, hook count |
| `feat list` | List feature bits |
| `feat <name> on\|off` | Toggle a feature |
| `capture <mode>` | `pass` / `freeze` / `black` / `white` |
| `counters` | **Per-feature intercept counts** (verifies hooks fire) |
| `modules` | Loaded Mythware libraries + counts |
| `inject` / `uninject` | Manual inject / unload & restore |
| `process` | Details of all three Mythware processes |
| `freeze` / `thaw` | `SIGSTOP` / `SIGCONT` |
| `log [file]` | Tail of the payload log |
| `quit` | Exit (auto-restores hooks) |

### Graphical Interface

```bash
./bin/jyfree-gui          # click "Inject"
./bin/jyfree-gui -i       # inject immediately on startup
```

```
┌────────────────────────────────────────┐
│ jyfree - 极域绕过 (UOS)              v2 │
├────────────────────────────────────────┤
│ [Inject] [Refresh] [Log] [Capture:frz] │
│ [Lock]   [Monitor] [Command]           │
│ [Input]  [Policy]   [Apps]             │
│                                        │
│ Student ●    Payload ●                 │
│ Hooks        Intercepts                │
├────────────────────────────────────────┤
│ Ready — click Inject                   │
└────────────────────────────────────────┘
```

Details: [`docs/03-user-guide.en.md`](docs/03-user-guide.en.md) · [中文](docs/03-使用手册.md)

---

## Target Environment

| Item | Value |
|---|---|
| OS | UOS Desktop 20 E |
| Architecture | **aarch64** |
| Kernel | `5.10.97-arm64-desktop` |
| Session | Wayland (kwin); Student runs on **XWayland** via `QT_QPA_PLATFORM=xcb` |
| User | `uid=1000`, **not in the `input` group** → cannot touch `/dev/input` directly |
| Yama | absent → same-UID `ptrace` works out of the box |
| Mythware version | v2.7.3715 (2024-12-12) |

Install path `/opt/mythware/classroom-management/`:

| File | Role |
|---|---|
| `Student` | main program, ET_EXEC non-PIE, unstripped ← **injection target** |
| `StudentAgent` | watchdog; main process of the systemd user unit |
| `StudentService` | root; web / USB / application policies |
| `libcastng.so.0.1` (192MB) | cast NG / freerdp engine |
| `libcast-x11.so` (126MB) | X11 backend, **dlopen'd at runtime** |
| `libDesk.so.2` | screen capture |
| `libGetAppsInfo.so.2` | application policies |

Listening ports: UDP `4788 5512 5662 5665 5666`, TCP `4806`,
multicast `225.2.2.11:5542` (control) / `:5547` (media)

---

## Troubleshooting Quick Reference

```bash
cat /tmp/jyfree-payload.log   # payload-internal log
./bin/jyfree -d 5             # controller debug log
./bin/jyfree --process        # did we find Student?
id -u                        # does the UID match?
```

| Symptom | Cause |
|---|---|
| `PTRACE_ATTACH denied` | UID differs from `Student` |
| `payload not ready in time` | arch mismatch / payload not found / symbols outdated |
| `target process in state 'T'` | Student is SIGSTOP'd — run `thaw` first |
| `counters` all zero | Normal — teacher hasn't acted; check `modules` |
| Capture count stays 0 | Teacher broadcast is an RDP stream (`libcast-x11`), a different path |

Details: [`docs/04-troubleshooting.en.md`](docs/04-troubleshooting.en.md) · [中文](docs/04-排错手册.md)

---

## Known Limitations

1. **Inline hook addresses are hardcoded** for Student **v2.7.3715**.
   For a different version, re-derive them:
   ```bash
   nm /opt/mythware/classroom-management/Student | grep ShowLockScreen
   ```
   then update `include/payload_state.h`.
2. **Policies already loaded at boot cannot be changed from user space**
   (`cms_nf.ko` lives in the kernel) — only subsequent policy delivery is blocked.
3. **Unauthenticated UDP command channels** (`4788/5512/5662/...`) are not
   reverse-engineered; nothing is forged.
4. **QLocalServer local protocol** is not reverse-engineered (socket is
   `srwxrwxrwx`, world-connectable).
5. **Teacher broadcast is an RDP stream** — the `XGetImage` override may not
   cover it (needs live classroom verification).
6. **Under Wayland, X11 hooks only affect X clients.** Student itself runs on
   XWayland and testing looks positive, but counter deltas need confirmation.
7. The `setuid` `Launcher` is **explicitly out of scope**.

---

## Documentation

**[📖 Full documentation index →](docs/README.en.md)**

| Document | Contents |
|---|---|
| [01 · Reverse Engineering](docs/01-reverse-engineering.en.md) | Environment, process model, protocol, **every hook site and symbol address**, injection scheme, open questions |
| [02 · Build Guide](docs/02-build-guide.en.md) | Dependencies, building, cross-compiling, arch requirements, install |
| [03 · User Guide](docs/03-user-guide.en.md) | Options, commands, feature bits, GUI, systemd |
| [04 · Troubleshooting](docs/04-troubleshooting.en.md) | Symptom → cause → fix |
| [05 · Publishing Guide](docs/05-github-upload.en.md) | **What to upload / what not to**, publishing steps, legal risks |

---

## Contributing

Issues and PRs welcome. See [`CONTRIBUTING.md`](CONTRIBUTING.md).

## Acknowledgements

- [imengyu/JiYuTrainer](https://github.com/imengyu/JiYuTrainer) — the original Windows project (MIT)
- [BengbuGuards/MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit) — protocol research reference
- Everyone who contributed measurements from the target machine

## License

[MIT](LICENSE) · For authorized local research and educational use only.
