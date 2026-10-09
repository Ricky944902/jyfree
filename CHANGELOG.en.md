# Changelog

Release history for this project.

Format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/);
versioning follows [Semantic Versioning](https://semver.org/).

---

## [2.0.0] — 2026-10-08

First complete rewrite targeting **Mythware Student on UOS (aarch64)**.

### Added

- **ptrace injection engine** — `PTRACE_ATTACH` → remote `mmap` → remote
  `dlopen` → restore regs → `DETACH`
- **Injection payload** `libjyfree.so` — `dlopen`'d inside `Student`, installs
  its own hooks
- **16 inline hooks** — aarch64 16-byte patch
  (`ldr x16,#8 ; br x16 ; .quad detour`), no trampoline needed
- **11 GOT hooks** — `dl_iterate_phdr` across every loaded ELF, matching
  `R_AARCH64_{JUMP_SLOT,GLOB_DAT}`
- **7 feature bits** — `lock` `monitor` `command` `apps` `policy` `input`
  `capture`, switchable in real time through shared memory
- **Shared memory** `/dev/shm/jyfree.state` — feature bits + intercept
  counters + payload heartbeat
- **Watchdog** — 1s poll; re-injects after a systemd restart, pid reuse, or a
  5-second heartbeat stall
- **Three capture modes** — `pass` / `freeze` / `black`
- **`uninject`** — triggers the payload destructor, restoring all inline hooks
- **GUI build** `jyfree-gui` — Xlib, with a live log window and feature toggles
- **Interactive CLI** — 16 runtime commands
- **Config persistence** — `~/.config/jiyu-trainer/config.json`
- **Logging** — controller log (10MB rotation) + a separate payload log
- **Build system** — Makefile with cross-compilation, plus a one-shot
  `scripts/build.sh`
- **Bilingual documentation** — README + 4 detailed documents

### Interception points (Student v2.7.3715)

| Category | Function | Address |
|---|---|---|
| Lock | `CStudentMainWork::ShowLockScreen` | `0x441ed4` |
| Lock | `::ShowBlackScreen` | `0x43fa4c` |
| Monitor | `::StartMonitorPassive` | `0x44e670` |
| Monitor | `::StartRdpMonitorPassive` | `0x44dbe0` |
| Monitor | `::ProcessDeskMonitorCommand` | `0x44eb44` |
| Command | `::ExecuteRemoteCmd` | `0x441a80` |
| Command | `::ProcessRemoteCommand` | `0x4438c8` |
| Policy | `::UpdatePoliciesToStudentService` | `0x44d34c` |
| Policy | `::UpdateUsbPolicyToStudentService` | `0x44cd28` |
| Policy | `::UpdateWebPolicyToStudentService` | `0x44ce88` |
| Apps | `libGetAppsInfo::KillProcess` | `0x82f8` |
| Apps | `::CloseTopWindow` | `0x8344` |
| Apps | `::DoAppPolicyControl` | `0x8340` |
| Input | `tf_peer_keyboard_event` | `0x22ebf0` |
| Input | `tf_peer_mouse_event` | `0x22e750` |
| Input | `CastClient::setInputGrab` | `0x2083c0` |

### Removed

The following v1.x implementations were discarded because measurements
contradicted them:

- ~~Forged heartbeat packets (`MYTH` magic)~~ → replaced by a shared-memory heartbeat
- ~~Faking the multicast protocol on known ports~~ → nothing is forged without RE
- ~~Touching `/dev/input/event*` directly as the main path~~ → the user isn't in
  the `input` group; now uses in-process hooks
- ~~iptables blocking as the primary method~~ → demoted to an optional extra
- ~~`studentmain` / `StudentMain.exe` process names~~ → the Linux build is
  actually `Student`

---

## [1.1.0]

- Config file read/write
- Log file output with rotation
- Process freeze / thaw (`SIGSTOP` / `SIGCONT`)
- GUI log viewer window
- GUI settings dialog
- systemd service and `.desktop` integration

> ⚠️ 1.x was based on inferences from the Windows edition and **was never
> verified against a real Linux Mythware install**. Its architecture and
> process names were both wrong. Fully superseded by 2.0.0.

---

## [1.0.0]

- Initial version: process discovery, input unlock, screen bypass, heartbeat
  simulation (port of the Windows approach)

---

## Planned

- **UDP channel RE** — command format of `4788/5512/5662/5665/5666`
- **QLocalServer protocol** — Qt frame format of the local IPC (the socket is
  world-connectable)
- **Capture on the broadcast path** — make `capture` affect the RDP stream
  (currently only covers `libDesk`'s `XGetImage`)
- **Version independence** — resolve symbols from `.dynsym` at runtime instead
  of hardcoding addresses
- **Native Wayland** — a capture path that doesn't rely on XWayland

---

[简体中文](CHANGELOG.md) · **[English](CHANGELOG.en.md)**