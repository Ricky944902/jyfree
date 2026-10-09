# Contributing

**[简体中文](CONTRIBUTING.md)** · **[English](CONTRIBUTING.en.md)**

Thanks for considering a contribution to jyfree.

---

## Language Convention

This project is **Chinese-primary**:

| Content | Language |
|---|---|
| Code comments | Chinese |
| Runtime output / logs | Chinese |
| GUI text | Chinese |
| Documentation | **bilingual** (Chinese is the default) |

**When submitting a PR, update both language versions of the docs.**
For example, if you change the build process, edit both
`docs/02-编译指南.md` and `docs/02-build-guide.en.md`.

---

## Filing Issues

Search for existing issues first.

**Always attach diagnostics** (script from
[`docs/04-troubleshooting.en.md`](docs/04-troubleshooting.en.md) §10):

```bash
{
  echo "=== System ===";   uname -a
  echo "=== Processes ==="; ./bin/jyfree --process
  echo "=== Libraries ==="; ./bin/jyfree --modules
  echo "=== Status ===";    ./bin/jyfree -d 5 -s
  echo "=== Payload log ==="; tail -50 /tmp/jyfree-payload.log
  echo "=== Symbols ==="
  nm /opt/mythware/classroom-management/Student 2>/dev/null \
    | grep -E 'ShowLockScreen|StartMonitorPassive'
} > /tmp/jyfree-report.txt 2>&1
```

Then attach `/tmp/jyfree-report.txt` to the issue.

### Useful Labels

| Label | Meaning |
|---|---|
| `needs-live-testing` | must be verified on real Mythware hardware |
| `symbols` | involves hardcoded addresses in `payload_state.h` |
| `wayland` | involves XWayland behavioural differences |
| `docs` | documentation improvement |
| `platform` | build / dependency issue |

---

## Development Setup

```bash
# build
make
make debug                    # -O0 -g3 -DDEBUG

# clean
make clean

# architecture check
make arch-check
```

### Architecture Note

The payload's inline hook addresses are **aarch64-only**. Building on x86
verifies that the code compiles but **will not run correctly**. For
cross-platform development:

```bash
make ARCH=aarch64 CROSS=aarch64-linux-gnu-
```

---

## Code Style

### Conventions

Follow the existing code:

- 4-space indentation
- `//` comments, in **Chinese**
- `snake_case` functions, `g_` for globals, `s_` / `g_` for statics
- Fixed flags `-Wall -Wextra`; **no new warnings allowed**

```bash
make 2>&1 | grep -E 'warning|error'   # must be empty
```

### ⚠️ Critical: keep `include/payload_state.h` in sync on both sides

This header is included by **both** the controller and the payload. It defines:

- feature bits (`JY_FEAT_*`)
- symbol addresses (`SYM_*`)
- the shared-memory struct (`JySharedState`)

**Both sides must be updated together** — a mismatched layout corrupts memory:

```c
// src/inject.c (controller) and src/hook_payload.c (payload) map the same /dev/shm block
// A layout mismatch → counters land in the wrong fields / garbage heartbeat
```

### Please Don't

- ❌ Add external dependencies (keep it to `pthread` / `dl` / `rt` / `X11`)
- ❌ Use C++ or aggressive post-C99 features (target is gcc 8.3)
- ❌ Reference controller symbols from the payload (breaks `dlopen`)
- ❌ Hardcode new architecture-specific addresses without a comment on the source

### Adding a New Hook

1. Confirm the symbol and address on the target machine
   ```bash
   nm /opt/mythware/classroom-management/Student | grep <symbol>
   ```
2. Add a `SYM_*` constant to `include/payload_state.h`, **noting the version**
3. Write the detour in `src/hook_payload.c`, **returning `w0 = 0`**
4. Register it in `install_inline_hooks()`
5. Add it to the `g_got_hooks[]` table if it's an X11 hook
6. Update both `docs/01-逆向分析.md` and `docs/01-reverse-engineering.en.md`

---

## Pull Requests

1. Branch off `main`: `feat/xxx` / `fix/xxx` / `docs/xxx`
2. Zero warnings:
   ```bash
   make clean && make && make gui
   ```
3. Update documentation (**both languages**)
4. In the PR description state: **what changed**, **why**, **how it was verified**

### PR Titles

```
feat: hook CastClient::setInputGrab to block input grabbing
fix: correct XShmGetImage return-value handling
docs: add cross-compilation steps
```

---

## Roadmap

PRs welcome in these areas:

- [ ] **UDP channel RE** — command format of `4788/5512/5662/5665/5666`
- [ ] **QLocalServer protocol** — Qt frame format of the local IPC
      (the socket is world-connectable)
- [ ] **Capture on the broadcast path** — make `capture` also affect the RDP stream
- [ ] **Version independence** — resolve symbols from Student's `.dynsym`
      at runtime instead of hardcoding addresses
- [ ] **GUI improvements** — log filtering, hotkeys for feature bits, tray icon

---

## Code of Conduct

Be kind, professional, and respectful. Harassment, discrimination, and personal
attacks are not tolerated. Stay open on technical matters — different
implementation approaches are all welcome for discussion.

---

## License

Contributions are accepted under the **MIT License**.

---

**[← 简体中文版本](CONTRIBUTING.md)**