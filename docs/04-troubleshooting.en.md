# 04 · Troubleshooting

**[简体中文](04-排错手册.md)** · **[English](04-troubleshooting.en.md)**

## Quick Self-Check

```bash
./bin/jyfree --process     # did we find Student?
id -u; ps -o uid= -p $(pgrep Student | head -1)   # do the UIDs match?
./scripts/build.sh --check # is the build environment right?
cat /tmp/jyfree-payload.log   # what did the payload say?
./bin/jyfree -d 5           # controller debug log
```

---

## 1. Student Not Found

```
[WARN] 未找到 Student 进程
```

### 1.1 Mythware hasn't started yet

The watchdog waits automatically. Once it starts, injection happens on its own,
or you can run it manually:

```bash
> inject
```

### 1.2 Wrong process name

```bash
./bin/jyfree --process
pgrep -a -f mythware
pgrep -a Student
```

The Linux edition's process is named `Student` (not the Windows
`StudentMain.exe`).

### 1.3 You're looking at the wrong process

Mythware runs three processes:

| Process | UID | Role |
|---|---|---|
| `Student` | user | **injection target** |
| `StudentAgent` | user | watchdog, tray |
| `StudentService` | **root** | policy execution (not injectable) |

---

## 2. `PTRACE_ATTACH denied`

```
[ERROR] PTRACE_ATTACH 被拒绝 (pid=12345): 需要同 UID 运行
```

### Cause

`ptrace` requires the tracer and the target to share a UID.

### Check

```bash
# this tool's UID
id -u

# Student's UID
ps -o uid=,user= -p $(pgrep Student | head -1)
```

### Fix

Run the tool **as the user who owns the Mythware session**:

```bash
# switch to that user
su - <mythware-username>

# or execute as them
sudo -u <mythware-username> ./bin/jyfree
```

> Mythware is an auto-started desktop program, so that is almost always the
> current login user. **Do not run this as root** — root can ptrace, but the
> payload would then run *inside* Student as `Student`, making shared-memory
> permissions and counter semantics inconsistent.

**No Yama `ptrace_scope` change is needed** — the target machine has no Yama.

---

## 3. `payload not ready within timeout`

```
[ERROR] payload 未在超时内就绪 (检查 /tmp/jyfree-payload.log)
```

### 3.1 Architecture mismatch (most common)

The payload's inline hook addresses (`0x441ed4`, …) are **aarch64-only**.

```bash
file bin/libjyfree.so
# must be: ELF 64-bit LSB shared object, ARM aarch64
```

If it isn't aarch64, rebuild with a cross toolchain:

```bash
./scripts/build.sh --cross
```

### 3.2 The payload can't be found

```bash
ls -l bin/libjyfree.so
ls -l $(dirname $(readlink -f $(which jyfree)))/libjyfree.so
```

`payload_path()` searches: the directory of `/proc/self/exe` →
`/usr/local/lib/` → `/usr/lib/` → `./`.
**`jyfree` and `libjyfree.so` must live in the same directory.**

### 3.3 Symbol addresses don't match the version

```bash
grep "内联钩子" /tmp/jyfree-payload.log
```

If the log shows write failures or a segfault, the version probably changed:

```bash
nm /opt/mythware/classroom-management/Student | grep ShowLockScreen
```

Compare against `SYM_ShowLockScreen` in `include/payload_state.h` and update it
if they differ.

### 3.4 Shared memory couldn't be created

```
[ERROR] 创建共享内存失败
```

```bash
ls -ld /dev/shm
mount | grep shm
```

Fix:

```bash
sudo mount -t tmpfs -o rw,nosuid,nodev,noatime tmpfs /dev/shm
```

---

## 4. `Target process is in state 'T'`

```
[WARN] 目标进程处于 'T' 状态, 跳过注入 (需先 SIGCONT)
```

Student has been frozen with `SIGSTOP`. This state was also observed during the
reverse-engineering work; the cause is undetermined (vendor restart timing or a
manual experiment).

```bash
> thaw
```

or:

```bash
kill -CONT $(pgrep Student | head -1)
```

---

## 5. Injection Succeeds but `counters` Are All Zero

```
拦截计数:
  锁屏 0
  监视 0
  ...
```

This is **normal** — counters only grow when the teacher actually *acts*.

```bash
# 1. Is the payload alive?
> status
  payload 心跳:   1757000000000     ← changing every second = healthy
  已装钩子数:     16

# 2. Is the target library loaded?
> modules
  [已加载] libDesk.so.2
  [未加载] libcast-x11.so       ← teacher hasn't started broadcasting
```

Interpretation:

| Observation | Conclusion |
|---|---|
| Heartbeat not changing | the payload thread died → the watchdog will re-inject, or run `inject` manually |
| Hook count = 0 | inline hooks were not installed → address mismatch, or the library isn't loaded yet |
| Hook count < 16 | some libraries weren't loaded at the time (normal — `libcast-x11` is loaded on demand) |
| Library loaded but counter 0 | the teacher hasn't triggered that feature yet |

---

## 6. Capture Interception Isn't Working

```
counters shows the capture count stuck at 0
```

### Two Paths

```
Path A: monitoring/capture  → libDesk.so.2 → XGetImage      ← covered by this tool
Path B: teacher broadcast  → RDP stream → libcast-x11.so   ← different path
```

Data on path B never passes through `XGetImage`, so the capture counter will
not climb.

### Determine Which Path Is Active

```bash
> modules
  [已加载] libDesk.so.2         ← monitoring is in use
  [已加载] libcast-x11.so       ← broadcasting is in use
```

Once broadcasting starts, `libcast-x11.so` gets dlopen'd and the payload hooks
it within one second (the log shows
`检测到 libcast-x11.so 已加载, 扫描其 GOT`).

### Known Limitation

`libcast-x11` carries frames over freerdp's RDP channel in a dedicated buffer,
which the `XGetImage` override may not reach. **This has not been verified** and
requires live classroom testing.

---

## 7. Input Interception Isn't Working

### 7.1 Qt windows are unaffected (expected)

Qt talks **xcb**, not Xlib → hooking Xlib does not affect Qt's windowing.
This is **by design**.

### 7.2 The teacher's mouse/keyboard still works

First confirm the freerdp input hooks are installed:

```bash
grep "libcastng" /tmp/jyfree-payload.log
```

If it says `libcastng 未加载, 跳过输入回调钩子`, the teacher hasn't started
remote control yet (`libcastng` may load only when the teacher connects).

Make sure the bit is on:

```bash
> feat input on
```

### 7.3 The XWayland Question

The session is Wayland, but Student runs on XWayland. `XGrabKeyboard` /
`XTestFake*` under XWayland **only affect X clients** — Mythware's own X11 code
is affected, but if teacher control travels a different channel (e.g. Qt
internals) it may not be caught.

Verify with the `输入` counter during a real class.

---

## 8. The Watchdog Keeps Re-injecting

The log keeps printing:

```
[INFO] 检测到 Student 新实例 (pid=NNNN), 重新注入
```

### Cause

`Student` is in a **crash loop**: `CastAudioBase`'s destructor triggers
`QThread destroyed while running` → abort → coredump → `StudentAgent` restarts it.

```bash
# confirm the crashes
dmesg | tail -20 | grep -i segfault
coredumpctl list 2>/dev/null | tail
```

### This is a vendor bug, not a problem with this tool.

It does not affect usage — the watchdog keeps up. Optional mitigation:

```bash
# suppress coredump writes to reduce I/O
sudo sysctl -w kernel.core_pattern='|/bin/true'
```

---

## 9. Build Problems

See [`02-build-guide.en.md`](02-build-guide.en.md) §8. Most common:

| Error | Fix |
|---|---|
| `X11/Xlib.h: No such file` | `sudo apt install libx11-dev`, or build only `make cli payload` |
| `'shm_open' undeclared` | add `-lrt` to `LIBS` in the Makefile |
| cross `aarch64-linux-gnu-gcc` not found | `sudo apt install gcc-aarch64-linux-gnu` |

---

## 10. Collecting a Full Report

When filing an issue, please attach:

```bash
{
  echo "=== System ==="
  uname -a
  cat /etc/os-release | head -3

  echo "=== Architecture ==="
  ./bin/jyfree --help | head -5

  echo "=== Mythware processes ==="
  ./bin/jyfree --process

  echo "=== Loaded libraries ==="
  ./bin/jyfree --modules

  echo "=== Injection status ==="
  ./bin/jyfree -d 5 -s

  echo "=== Payload log ==="
  tail -50 /tmp/jyfree-payload.log

  echo "=== Controller log ==="
  tail -50 ~/.config/jiyu-trainer/jiyu-trainer.log

  echo "=== Symbol address cross-check ==="
  nm /opt/mythware/classroom-management/Student 2>/dev/null \
    | grep -E 'ShowLockScreen|StartMonitorPassive|ExecuteRemoteCmd'
} > /tmp/jyfree-report.txt 2>&1

echo "report written: /tmp/jyfree-report.txt"
```

---

## 11. Emergency Restore

If the hooks cause Mythware to misbehave:

```bash
./bin/jyfree
> uninject      # triggers the destructor, restores every inline hook
> quit
```

Or kill Student outright and let `StudentAgent` start a clean one:

```bash
kill -9 $(pgrep Student | head -1)
```

Nuclear option — stop all of Mythware:

```bash
pkill -9 -f Student
pkill -9 -f StudentAgent
# StudentService runs as root, needs sudo
sudo pkill -9 StudentService
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