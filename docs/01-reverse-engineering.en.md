# 01 · Reverse Engineering

**[简体中文](01-逆向分析.md)** · **[English](01-reverse-engineering.en.md)**

This document records measured reverse-engineering results for **Mythware
Classroom on UOS (aarch64)**, and explains how jyfree intercepts it.

All conclusions come from measurements on the target machine
(`UOS Desktop 20 E`, kernel `5.10.97-arm64-desktop`).

---

## 1. Environment

| Item | Value |
|---|---|
| OS | UOS Desktop 20 E (Debian-based) |
| Architecture | **aarch64** |
| Session | Wayland (kwin), but Student runs on **XWayland** via `QT_QPA_PLATFORM=xcb` (`DISPLAY=:0`) |
| LSM | `capability,elfverify,useclinux,uosmanager` (no SELinux/AppArmor) |
| Yama | **absent** → same-UID `ptrace` works out of the box |
| Seccomp | Student `Seccomp:0`, `NoNewPrivs:0` |
| User | `uid=1000`, **not in the `input` group** (cannot touch `/dev/input` directly) |
| Toolchain | gcc 8.3, python3.7, xdotool; **no gdb/strace/rust** |

---

## 2. Installation Layout

`/opt/mythware/classroom-management/`

| File | Attributes | Role |
|---|---|---|
| `Launcher` | **setuid root** | v2.7.3715 (2024-12-12), args `--app/--root`. **Not used by this tool** (out of scope) |
| `Student` | ELF64 **ET_EXEC (non-PIE, fixed addresses)**, **unstripped** | main program, ~924KB, **injection target** |
| `StudentAgent` | 62KB, user | watchdog / tray; main process of the systemd user unit |
| `StudentService` | 189KB, **root** | web policy (netfilter / `cms_nf.ko`), USB, application management. **No input capability** |
| `cms_nf.ko` | kernel module (loaded at boot) | kernel-side network lockdown |
| `libcastng.so.0.1` | **192MB** | cast NG engine, embeds freerdp |
| `libcast-x11.so` | **126MB** | X11 control backend, **dlopen'd at runtime** |
| `libDesk.so.2` | — | screen capture (`XGetImage`), H264 encoding |
| `libGetAppsInfo.so.2` | 62KB | application policies: `KillProcess` / `CloseTopWindow` / `QLockInput` |
| `libScreenBroadcast.so.2` | — | screen broadcast (no direct X dependency) |
| `qt/` | — | bundled Qt5 (xcb platform plugin) |

---

## 3. Process Model

```
systemd --user
 └─ com.mythware.mcm-student.service      (RefuseManualStop=yes)
     ExecStart=StudentAgent, Restart=always, RestartSec=5s
     ├─ StudentAgent     watchdog: connects to Student's socket, restarts Student
     │                    if X/Wayland checks fail
     ├─ StudentService   root, bridges /var/tmp/com.mythware.cms.StudentService-3e8.socket
     └─ Student          --restart, actually launched/restarted by StudentAgent
```

Key points:

- **Singleton mechanism**: `/var/tmp/com.mythware.cms.Student-3e8.socket`
  (`3e8` = 1000 = uid), a QLocalServer socket with mode `srwxrwxrwx`
  (**world-connectable**).
- **Crash loop**: `CastAudioBase`'s destructor triggers
  `QThread destroyed while running` → abort → coredump; StudentAgent restarts it
  with `--restart`. Observed at 14:00 and 14:05.
  → **this is why jyfree needs a watchdog**.
- Exit code `1000` = singleton conflict ("application already running").
- Student was once observed in state `T` (SIGSTOP) → injection must check and skip.

---

## 4. Feature Configuration (from the `Student.log` banner)

```
EnableKeyMouseControl: true      EnableRemotePowerOn: true
EnableScreencastAudio: true      EnableUSBPolicy: true
UseH264Encoder: true             Version: V1.0 (国产OS版本)
ProductId: com.mythware.classroommanagement
RootDirectory: ~/Documents/极域(电子)课堂互动教学系统软件（国产OS版本） V1.0
```

- Config: `~/.config/Mythware/ClassroomManagement.conf`
- Log: `~/.local/share/Mythware/ElcLog/.../Student.log`
- Cast engine log: `~/.local/share/mythware/cms-ng/log/libcast/default/libcast.log`

---

## 5. Network & Protocol

- Teacher machine measured at `10.70.45.100` (MAC `cc:b0:a8:4a:77:46`);
  student machine at `10.70.45.124`
- **cast NG (freerdp-based)**
  - Reliable multicast control: `225.2.2.11:5542`
    (rmc, window 0.5s, payload 1440)
  - Video/audio multicast: `225.2.2.11:5547`
    (window 5s, maxBandwidth 204800 kbps, redundancy 1.2)
- **Student listens on**: UDP `4788, 5512, 5662, 5665, 5666`; TCP `4806`
- Local IPC (QLocalServer, Qt frame format — **not** reverse-engineered):
  `/var/tmp/com.mythware.cms.{Student,StudentAgent,StudentService,StudentServer}-3e8.socket`
  carrying `(int cmd, QByteArray)` — this is the **policy delivery channel**.

> jyfree **forges no protocol packets**. The heartbeat only tracks whether the
> payload is still alive.

---

## 6. Features the Vendor Already Stubbed Out

The Linux edition reduced many Windows-edition features to `mov w0,#0; ret`:

`StartRemoteControl`, `StopRemoteControl`, `StartGroupTeachPassive`,
`StartShareBoardPassive`, `StartLiveShowPassive` (teacher demo / live show),
`InitVoice`, `StartVoiceTeachPassive`, `StartSpeakPassive`

---

## 7. Interception Points

### 7.1 Inline Hooks — `CStudentMainWork` (main program)

Student is **ET_EXEC non-PIE**, so `nm` addresses are runtime addresses.

| Symbol (mangled) | Address | Role | Bit |
|---|---|---|---|
| `_ZN16CStudentMainWork14ShowLockScreenEi` | `0x441ed4` | lock-screen window | `lock` |
| `_ZN16CStudentMainWork15ShowBlackScreenEi14FLAG_BACK_TYPERK7QStringjj` | `0x43fa4c` | black screen (class) | `lock` |
| `_ZN16CStudentMainWork19StartMonitorPassiveEP16tagMONITORPARAMS` | `0x44e670` | teacher monitoring | `monitor` |
| `_ZN16CStudentMainWork22StartRdpMonitorPassiveEP16tagMONITORPARAMS` | `0x44dbe0` | RDP-based monitoring | `monitor` |
| `_ZN16CStudentMainWork25ProcessDeskMonitorCommandEP19tagDESKDEMO_COMMAND` | `0x44eb44` | desktop demo / view command | `monitor` |
| `_ZN16CStudentMainWork16ExecuteRemoteCmdE8RSD_MODE` | `0x441a80` | remote command (0..3 jump table) | `command` |
| `_ZN16CStudentMainWork20ProcessRemoteCommandEP22tagREMOTECOMMANDPARAMS` | `0x4438c8` | remote command handler | `command` |
| `_ZN16CStudentMainWork30UpdatePoliciesToStudentServiceEv` | `0x44d34c` | aggregate policies → root service | `policy` |
| `_ZN16CStudentMainWork31UpdateUsbPolicyToStudentServiceEv` | `0x44cd28` | USB policy delivery | `policy` |
| `_ZN16CStudentMainWork31UpdateWebPolicyToStudentServiceEv` | `0x44ce88` | web policy delivery | `policy` |

**Left functional on purpose (not hooked)**: `DestoryLockScreen`,
`RemoveLockScreenWidget`, `DestoryBlackScreenWidget`, `StopMonitorPassive`,
`BlackScreen::unlockScreenManual` (student-initiated unlock)

### 7.2 Inline Hooks — `libGetAppsInfo.so.2`

Plain C exports, loaded by Student. The symbols are **4-byte `b` trampolines**,
so the real targets must be resolved by following the jump first.

| Symbol | Address | Role | Bit |
|---|---|---|---|
| `DoAppPolicyControl` | `0x8340` | application policy entry point | `apps` |
| `CloseTopWindow` | `0x8344` | close topmost window | `apps` |
| `KillProcess` | `0x82f8` | kill student processes (calls `kill`/`system`) | `apps` |
| `CloseApps` | `0x85f0` | close applications | `apps` |
| `CloseWndByWindowId` | `0x856c` | close window by window id | `apps` |
| `GetApplicationList` | `0x82f0` | process enumeration | **not hooked** |
| `StartGetIcons` | `0x8348` | icon enumeration | **not hooked** |

> `GetApplicationList` is deliberately left alone: hooking a function that
> returns a struct carries ABI risk (a size/version mismatch crashes the process).

This library also imports `XTestFake*` + `XSendEvent`. **`XSendEvent` must not be
blocked globally** — only input events `type ∈ {2,3,4,5,6}` are intercepted,
because the library also uses it to send `WM_DELETE_WINDOW` in order to close the
broadcast window.

### 7.3 Inline Hooks — `libcastng.so.0.1` (freerdp input callbacks)

**The final landing point of teacher-initiated remote control.**

| Symbol | Address |
|---|---|
| `_Z22tf_peer_keyboard_eventP9rdp_inputtt` | `0x22ebf0` |
| `_Z30tf_peer_unicode_keyboard_eventP9rdp_inputtt` | `0x22e940` |
| `_Z19tf_peer_mouse_eventP9rdp_inputttt` | `0x22e750` |
| `_Z28tf_peer_extended_mouse_eventP9rdp_inputttt` | `0x22e560` |
| `_ZN10CastClient12setInputGrabEb` (thunk) | `0x2083c0` |

castng has **no direct X11/xcb imports** → screen capture is delegated to libDesk.

### 7.4 GOT Hooks — the X11 Layer (all loaded ELFs)

`dl_iterate_phdr` walks every loaded module → parses `PT_DYNAMIC`
(`DT_JMPREL`/`DT_RELA`) → matches names against
`R_AARCH64_{JUMP_SLOT,GLOB_DAT}` → `mprotect` then rewrite.

| Target | Handling |
|---|---|
| `XGetImage` / `XShmGetImage` | fetch the real frame, then overwrite pixels (freeze / black) |
| `XGrabKeyboard` / `XGrabPointer` | return `0` (Success) without actually grabbing |
| `XTestFakeKeyEvent` / `XTestFakeButtonEvent` / `XTestFakeMotionEvent` | swallow |
| `XWarpPointer` | block pointer locking |
| `XkbLockModifiers` | refuse |
| `XSendEvent` | intercept only `type ∈ {2..6}` |

> `libcast-x11.so` (126MB) imports `XGrabKeyboard` / `XUngrabKeyboard` /
> `XUngrabPointer` / `XTestFake*` / `XkbLockModifiers` / `XGetImage` /
> `XWarpPointer` / `XSendEvent` / `XShm*`.
> Its RELA entries were measured (e.g. `XGrabKeyboard` GOT slot
> `r_offset=0x131d188`).
> Because it is **dlopen'd on demand**, the payload rescans for new modules
> every second.

**Why this works**: Qt itself talks xcb, not Xlib → hooking Xlib does not affect
Qt's windowing; but Mythware's `libcast-x11` *does* go through Xlib → hooking
Xlib does affect it.

### 7.5 Screen Capture — `libDesk.so.2`

| Symbol | Address | Note |
|---|---|---|
| `XGetImage` (GOT) | — | **the only real capture entry point observed so far** |
| `CDesktopCapture::StartSendThread` | `0x132f0` | capture thread startup |
| `QtScreenCapture::grabScreen(QString)` | `0x16264` | per-window capture (returns QImage, **sret ABI — not hooked**) |
| `CDesktopCapture::RequestFullScreen` | PLT | full-screen monitor request |

---

## 8. Permission Model

- `Student` / `StudentAgent` are **ordinary user processes** → same-UID
  `ptrace` / `dlopen` / `mprotect` are all viable
- Keyboard/mouse control, lock screen, monitoring, and process killing all live
  **inside the user-space Student process**
- Only these remain privileged: web policy (`cms_nf.ko`), USB Minifilter,
  and the setuid `Launcher`
- **Policies already loaded at boot cannot be changed from user space** →
  jyfree can only block *subsequent* policy delivery (a known limitation)
- The systemd user unit sets `RefuseManualStop=yes` → you cannot
  `systemctl stop` it; you must send signals directly.

---

## 9. jyfree's Injection Scheme

```
jyfree (controller, aarch64 user space)
 ├─ inject_attach      PTRACE_ATTACH → save regs
 ├─ inject_call        write brk #0 on the target stack, PC=func, LR=trap addr
 ├─ inject_dlopen      remote mmap for the path → remote dlopen(RTLD_NOW)
 ├─ inject_detach      restore regs → DETACH
 ├─ watchdog           1s poll: pid change / start_time change / heartbeat stall
 └─ shared memory      /dev/shm/jyfree.state (bits + counters + heartbeat)

libjyfree.so (payload, dlopen'd into Student)
 ├─ constructor        attach shm → install inline hooks → scan GOT → start thread
 ├─ inline hooks       ldr x16,#8 ; br x16 ; .quad detour   (16 bytes, no trampoline)
 ├─ GOT hooks          dl_iterate_phdr across all ELFs
 ├─ coordinator        1s heartbeat + detect newly loaded libcast-x11
 └─ destructor         restore every inline hook (on uninject)
```

**Return convention**: `w0 = 0`, matching the vendor's stub convention.
If a call site checks a "handled" boolean, return `1` instead.

---

## 10. Open Questions / Known Uncertainties

1. **Actual effect of X11 hooks under Wayland** — the session is XWayland, so
   `XGrabKeyboard` / `XTestFake*` may inherently only affect X clients.
   Verify with `counters` deltas during a real class.
2. **Who SIGSTOPs Student** — undetermined (vendor restart timing vs. manual
   experiment). The watchdog branches on process state.
3. **Whether monitor/broadcast frames really pass through `libDesk`'s
   `XGetImage`** — needs live verification via the capture counter.
   Note: **teacher broadcast is an RDP stream** going through
   `libcast-x11.so`, which is a *different* path from libDesk.
4. **Inline hook return convention** — see §9.
5. **Unauthenticated UDP command channels** (`4788/5512/5662/…`, the Linux
   counterpart of the Windows `Jiyu_udp_attack`) — not reverse-engineered yet,
   a possible future upgrade.
6. **QLocalServer local protocol** (world-connectable `srwxrwxrwx` socket) —
   Qt frame format not reverse-engineered; a potential command-injection surface.
7. **The setuid `Launcher`** — a suspicious privilege-escalation surface,
   **explicitly out of scope for this project**.
8. Student-initiated hand-raise/unlock, and closing the broadcast window by
   `_NET_WM_PID` via xdotool — optional, not implemented.

---

## 11. Re-deriving Symbol Addresses for a New Version

```bash
nm /opt/mythware/classroom-management/Student | grep -E 'ShowLockScreen|StartMonitorPassive'
readelf -s libGetAppsInfo.so.2 | grep -E 'KillProcess|CloseTopWindow'
```

Then update the `SYM_*` constants in `include/payload_state.h`.

---

For authorized local research and educational use only. Mythware is a
registered trademark of Guangzhou Shirui Software Technology Co., Ltd.
---

<div align="center">

**[⬆ Main documentation](../README.en.md)** · **[中文主文档 →](../README.md)**
**[Documentation index](README.en.md)** · **[中文索引 →](README.md)**

For authorized local research and educational use only.
Mythware is a registered trademark of Guangzhou Shirui Software Technology
Co., Ltd. This project is not affiliated with or endorsed by them.

</div>