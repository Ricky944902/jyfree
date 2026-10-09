# 02 · Build Guide

**[简体中文](02-编译指南.md)** · **[English](02-build-guide.en.md)**

## 1. Dependencies

### Required (all provided by the base system)

| Dependency | Purpose |
|---|---|
| `gcc` ≥ 6 | compiler |
| `make` | build |
| `libc.so.6` | **the only runtime library dependency** — present on every Linux |

**No development libraries are needed at build time.** Since glibc ≥ 2.34,
`pthread` / `dlopen` / `shm_open` / `ptrace` / `process_vm_readv` all live in
`libc`; the separate `libpthread` / `libdl` / `librt` files are now empty stub
libraries.

```bash
# Debian / UOS
sudo apt install build-essential

# Fedora / RHEL
sudo dnf install gcc make
```

### Optional (graphical build only)

| Dependency | Purpose |
|---|---|
| `libx11-dev` | needed only by `jyfree-gui` |

```bash
sudo apt install libx11-dev        # Debian / UOS
sudo dnf install libX11-devel      # Fedora / RHEL
```

**The build still succeeds without it** — `make` auto-detects X11 and skips the
GUI target, producing only the CLI and the payload. The CLI is fully
functional; the GUI is just a convenience shell around it.

### Cross toolchain (optional)

```bash
sudo apt install gcc-aarch64-linux-gnu
```

### Verify

```bash
./scripts/build.sh --check
```

Sample output:

```
ok  make
ok  gcc (13)
ok  glibc 2.39 (pthread/dl/rt merged into libc)
ok  X11 dev library (GUI build available)

Verdict: the CLI needs only libc (system-provided); no extra packages required
```

### Legacy systems (glibc < 2.34)

If the target has glibc older than 2.34, link the three stubs explicitly:

```bash
make OLD_GLIBC=1
```

---

## 2. One-Shot Build

```bash
./scripts/build.sh            # native build + dependency check + arch check
./scripts/build.sh --check    # environment check only, no build
./scripts/build.sh --debug    # debug build (-O0 -g3 -DDEBUG)
./scripts/build.sh --install  # build and install into ~/.local
./scripts/build.sh --clean    # clean
./scripts/build.sh --cross    # cross-compile for aarch64
./scripts/build.sh --help     # help
```

---

## 3. Building via Makefile Directly

```bash
make              # everything: jyfree + jyfree-gui + libjyfree.so
make cli          # CLI only
make gui          # GUI only
make payload      # payload only
make debug        # debug build
make clean        # remove intermediates (obj/)
make distclean    # also remove bin/
make arch-check   # verify architecture
make install      # install
make help         # list every target
```

Outputs:

```
bin/jyfree          CLI
bin/jyfree-gui      GUI
bin/libjyfree.so    payload (must sit next to jyfree)
```

Intermediates go to `obj/`.

---

## 4. Cross-Compiling (building aarch64 on x86)

**Via script**

```bash
./scripts/build.sh --cross
./scripts/build.sh --cross aarch64-none-linux-gnu-
```

**Via Makefile**

```bash
make ARCH=aarch64 CROSS=aarch64-linux-gnu-
```

Copy the artifacts to the target:

```bash
scp bin/* user@target:/usr/local/bin/
```

---

## 5. Architecture Requirement (important)

> **The inline hook addresses in the payload are aarch64-only.**

`Student` is an **ET_EXEC non-PIE** binary, so addresses from `nm` are the
runtime addresses, for example:

```
ShowLockScreen  @ 0x441ed4
StartMonitorPassive @ 0x44e670
```

jyfree writes these addresses straight into the payload's patches. Therefore:

| Build arch | Result |
|---|---|
| aarch64 | ✅ inline hooks **and** GOT hooks both work |
| x86_64 etc. | ❌ GOT hooks still work; inline hooks write to unrelated addresses (may crash) |

Building on a non-aarch64 host is **only useful for verifying that the code
compiles**. It must not be run.

`make arch-check` and `./scripts/build.sh --check` will warn you.

---

## 6. Build Variables

| Variable | Default | Meaning |
|---|---|---|
| `ARCH` | `$(uname -m)` | target architecture |
| `CROSS` | empty | cross toolchain prefix |
| `OPT` | `-O2` | optimization level |
| `PREFIX` | `/usr/local` | install prefix |

Examples:

```bash
make OPT="-O0 -g3 -fsanitize=address" cli     # ASan debug build
make PREFIX="$HOME/.local" install             # install into the home dir
make OPT="-O3 -march=armv8-a" cli             # tune for ARMv8
```

### The payload's special flags

The payload uses its own `PAYLOAD_CFLAGS`:

```
-Wall -Wextra -O2 -Iinclude -fPIC -DJIYU_PAYLOAD_BUILD=1
```

- `-fPIC` — it gets `dlopen`ed into another process, so it must be
  position-independent
- `-DJIYU_PAYLOAD_BUILD=1` — enables the payload's internal logging.
  The payload runs *inside* Student, so it **cannot** depend on the
  controller's `jiyu_debug_*` symbols; otherwise `dlopen` fails with an
  unresolved-symbol error.

---

## 7. Installing

```bash
sudo make install                        # /usr/local/bin
make install PREFIX="$HOME/.local"       # user directory
./scripts/install.sh                     # interactive (auto-detects root)
```

Installed files:

```
$PREFIX/bin/jyfree
$PREFIX/bin/jyfree-gui
$PREFIX/bin/libjyfree.so      ← must be in the same directory as jyfree
```

> `payload_path()` search order:
> the directory of `/proc/self/exe` → `/usr/local/lib/` → `/usr/lib/` → `./`
> When placing files manually, keep `jyfree` and `libjyfree.so` together.

### Desktop & systemd Integration

`scripts/install.sh` also installs:

```
~/.local/share/applications/jyfree.desktop   menu entry
~/.config/systemd/user/jyfree.service        user-level service
```

Enable auto-start:

```bash
systemctl --user daemon-reload
systemctl --user enable jyfree.service
```

---

## 8. Common Build Problems

### `X11/Xlib.h: No such file or directory`

```bash
sudo apt install libx11-dev
```

Or skip the GUI entirely:

```bash
make cli payload
```

### `'shm_open' undeclared`

Missing `librt`. glibc ≥ 2.34 merged it into libc, but older versions need it
linked explicitly:

```bash
# verify LIBS contains -lrt in the Makefile
LIBS := -lpthread -ldl -lrt
```

### Errors about `brk` / `FORBIDDEN` instructions

The payload's patch instructions are **hand-written machine code**, not compiler
syntax:

```c
uint32_t patch[4] = { 0x58000050, 0xd61f0200, ... };
```

### The payload compiles but injection fails

See [`04-troubleshooting.en.md`](04-troubleshooting.en.md).

---

## 9. Post-Build Self-Check

```bash
# 1. Verify architecture
file bin/jyfree bin/libjyfree.so

# 2. Confirm the payload can be located
./bin/jyfree --status

# 3. Confirm the CLI runs
./bin/jyfree --help
./bin/jyfree --process      # requires Mythware to be running

# 4. Confirm the payload log gets written
./bin/jyfree                 # after injecting
cat /tmp/jyfree-payload.log
```

---

## 10. Source File Map

```
include/
  jiyu.h            controller public API (every declaration)
  payload_state.h   ★ shared by both sides: bits / symbol addresses / shm struct
  gui.h             Xlib GUI structures and API

src/
  main.c            CLI entry, command table, argument parsing
  gui_main.c        GUI entry
  gui.c             Xlib implementation (window/buttons/log window/settings)
  inject.c          ptrace engine + shared memory + watchdog
  hook_payload.c    payload (built into libjyfree.so)
  process.c         /proc scanning for Student, systemd integration
  utils.c           config read/write, logging, debug output
```

> **Removed in v2.1**: `heartbeat.c` (`/proc/pid/net/*` is network-namespace
> scoped, so it cannot tell you what a *process* listens on), `screen.c`,
> `input.c`, `netfilter.c` — all v1 leftovers. Their functionality is now
> provided by feature bits plus the payload hooks; keeping them would mislead
> readers into thinking those features work.

> **When editing `payload_state.h`, keep both sides in sync** — it is included
> by both the controller and the payload; a mismatched struct layout corrupts
> memory.

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