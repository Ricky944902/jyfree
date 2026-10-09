# 05 · GitHub Publishing Guide

**[简体中文](05-GitHub发布指南.md)** · **[English](05-github-upload.en.md)**

This document explains **what to upload, what is documentation only, what must
never be uploaded**, and gives the complete publishing procedure.

---

## At a Glance

| Category | Count | Upload |
|---|---|---|
| **Source code** (`src/` `include/` `Makefile`) | 11 | ✅ Required |
| **Scripts + deployment** (`scripts/` `deploy/`) | 5 | ✅ Required |
| **Documentation** (`.md` + `LICENSE` + `.gitignore`) | 20 | ✅ Required |
| **Build artifacts** | `bin/` `obj/` | ❌ No |
| **Mythware files** | `*.deb` `mythware-extracted/` | ❌ **Never (legal risk)** |
| **Local logs / config** | `*.log` `config.json` | ❌ No |

**36 files total to upload.**

---

## 1. Source Code (11 files, must upload)

After `git clone`, other people must be able to build.

```
Makefile                    build configuration (root, cannot be omitted)

include/                    headers (3)
  ├── jiyu.h                   controller public API
  ├── payload_state.h        ★ feature bits / symbol addresses / shm structs
  └── gui.h                   Xlib GUI

src/                        sources (7)
  ├── main.c                   CLI entry + command table
  ├── inject.c                 ptrace engine + shared memory + watchdog
  ├── hook_payload.c        ★  payload (built into libjyfree.so)
  ├── process.c                /proc scanning for Student
  ├── gui.c                    Xlib GUI implementation
  ├── gui_main.c               GUI entry
  └── utils.c                  config / logging / debug
```

All must be uploaded, otherwise nobody can build.

---

## 2. Scripts + Deployment (5 files, must upload)

```
scripts/
  ├── build.sh              ★  one-shot build (deps + arch check)
  ├── install.sh               one-shot install
  └── extract-mythware.sh      Mythware file extractor (TUI)

deploy/
  ├── jyfree.service             systemd user unit
  └── jyfree.desktop             desktop entry
```

> `scripts/extract-mythware.sh` is a *tool* script (it extracts Mythware files
> for analysis). It is **not** Mythware itself, so it may be published.
> Its **output** directory `mythware-extracted/` must **never** be published
> (see §5).

---

## 3. Documentation (20 files, must upload)

```
root (9)
  ├── README.md              ★ Chinese main doc (GitHub shows this by default)
  ├── README.en.md             English
  ├── CHANGELOG.md / .en.md    changelog ×2
  ├── CONTRIBUTING.md / .en.md contribution guide ×2
  ├── LICENSE                  MIT license (required for compliant open source)
  └── .gitignore             ★ ignore rules (required)

docs/ (11)
  ├── README.md / README.en.md         documentation index ×2
  ├── 01-逆向分析.md / 01-reverse-engineering.en.md
  ├── 02-编译指南.md / 02-build-guide.en.md
  ├── 03-使用手册.md / 03-user-guide.en.md
  ├── 04-排错手册.md / 04-troubleshooting.en.md
  └── 05-GitHub发布指南.md / 05-github-upload.en.md   (this file)
```

### Why `.gitignore` must be uploaded

It is the **first line of defence against accidentally committing build
artifacts and Mythware files**. Others who clone your repo use it too.

---

## 4. Build Artifacts (do not upload — already in `.gitignore`)

```
bin/                        ✗ jyfree  jyfree-gui  libjyfree.so
obj/                        ✗ *.o  *.lo
```

**Rationale:**

- Anyone can regenerate them with `make`
- Your binaries are x86; useless for the aarch64 target
- Bloats the repo, and binary diffs can't be reviewed

`.gitignore` already covers:

```gitignore
bin/
obj/
*.o
*.lo
*.pic.o
*.so
```

---

## 5. ⚠️ Never Upload (legal risk)

### The Mythware packages and files

```
*.deb                           ✗
mythware-extracted/             ✗
mythware-deb-extracted/         ✗
mythware-analysis/              ✗
```

**Why this matters:**

1. **Copyright** — Mythware is commercial software from
   **Guangzhou Shirui Software Technology Co., Ltd.** Its binaries, libraries,
   and icons are copyrighted material. Committing them to your own GitHub repo
   is **unauthorized redistribution**.
2. **Legal consequence** — DMCA takedown, repository removal, or litigation.
   **DMCA does not care whether you had permission.**
3. **Account risk** — GitHub will ban or force-remove the repository.

### ✅ The correct approach

| Goal | Correct approach |
|---|---|
| Extract Mythware files for analysis | run `scripts/extract-mythware.sh` locally, keep output local |
| Post analysis in an issue | post **symbol addresses, function names, protocol structures** (factual), not the files |
| Provide samples to others | have them **extract from their own installed copy** |
| Quote code snippets | short excerpts to illustrate a problem, with attribution |

**Open-sourcing the tool ≠ shipping the targeted software.** Important distinction.

### Other things not to upload

```
*.log  jyfree-payload.log  config.json
.vscode/  .idea/  *.swp  *~  .DS_Store
jyfree-report.txt          (contains target machine info)
```

---

## 6. Complete Publishing Procedure

### Step 1 · Configure Git identity (the most commonly missed step)

```bash
git config --global user.name  "your-github-username"
git config --global user.email "your-github-email"
```

> Use the email bound to your GitHub account, or the avatar won't link.
> For anonymity: `your-username@users.noreply.github.com`.

**Skipping this yields `Author identity unknown` and the commit fails.**

### Step 2 · Initialize the repository

```bash
cd /d/JiYuTrainer        # Git Bash
git init
git branch -M main
```

### Step 3 · Clean and check

```bash
make distclean                    # remove build artifacts

find . -name "*.deb" -o -name "mythware-extracted" -type d
# should print nothing

git status --short --ignored | grep '^!!'
# you should see !! bin/  !! obj/  → .gitignore is working
```

### Step 4 · Add

```bash
git add .
```

### Step 5 · ⚠️ Verify (critical)

```bash
git diff --cached --name-only | wc -l     # should be 36
git status                                 # review item by item
```

Cross-check against §1–§3:

- `src/` `include/` `Makefile` `scripts/` `deploy/` `docs/` `*.md` → ✅
- `bin/` `obj/` → ❌ must not appear
- any `.deb` / `mythware-*` → ❌ **STOP**

If the count is wrong or something unwanted is staged:

```bash
git rm --cached <path>     # unstage without deleting locally
```

### Step 6 · Commit

```bash
git commit -m "feat: jyfree v2.0.0 - UOS/aarch64 Mythware interceptor"
```

### Step 7 · Create the GitHub repository (web UI)

1. **+ → New repository**
2. Repository name: `jyfree`
3. Description: `极域电子教室绕过工具 (统信UOS / aarch64)`
4. ⚠️ **Do NOT** tick "Add a README file"
5. ⚠️ **Do NOT** tick "Add .gitignore"
6. ⚠️ **Do NOT** tick "Choose a license"
7. **Create repository**

> Ticking any of them conflicts with your local files and the push gets rejected.

### Step 8 · Link and push

```bash
git remote add origin https://github.com/<username>/jyfree.git
git remote -v                 # confirm (don't add twice)
git push -u origin main
```

### Step 9 · Set topics (web UI)

**About** → description; **Topics**:

```
mythware jiyu classroom uos aarch64 arm64
reverse-engineering ptrace hook linux bypass
```

### Step 10 · Verify (most important)

```bash
cd ..
git clone https://github.com/<username>/jyfree.git jyfree-test
cd jyfree-test
ls -la                    # only sources, no bin/ obj/
./scripts/build.sh        # builds → others can use it
```

---

## 7. Common Errors

### `Author identity unknown`

```bash
git config --global user.name  "your-username"
git config --global user.email "your-email"
git commit -m "..."       # retry the commit
```

### `src refspec main does not match any`

**Cause: no commit exists** (the previous step failed). Commit successfully first.

### `Updates were rejected`

The remote has commits (you ticked "Add README" when creating the repo):

```bash
git pull origin main --allow-unrelated-histories
# resolve conflicts, then
git push -u origin main
```

### `remote origin already exists`

You ran `git remote add` twice. Decide what to do:

```bash
git remote -v                 # check current URL
git remote set-url origin https://github.com/<username>/jyfree.git   # fix URL
# git remote remove origin    # or remove and re-add
```

### Already committed `bin/`

```bash
git rm -r --cached bin obj
git commit -m "chore: untrack build artifacts"
git push
```

> This only stops tracking; it does **not** purge existing history.

### Already committed a `*.deb` or Mythware files

**Take the repo down first — don't rewrite history yet:**

```
Settings → Danger Zone → Delete this repository
```

Takedown is fastest and lowest-risk. To keep the repo and purge history, use
`git filter-repo`, but that rewrites every commit hash and forces all
collaborators to re-clone — recreating is usually simpler.

---

## 8. Checklist

- [ ] Git identity configured
- [ ] `git diff --cached --name-only | wc -l` = 36
- [ ] No `bin/` or `obj/`
- [ ] No `.deb` / `mythware-*`
- [ ] `src/` `include/` `scripts/` `deploy/` present
- [ ] `README.md` `LICENSE` `.gitignore` present
- [ ] Chinese and English docs exist in pairs
- [ ] No broken internal links
- [ ] `make` succeeds from a fresh clone
- [ ] Repo description and topics set

---

## 9. Current Status

```
✅ 11 source files          — in place
✅ 5 scripts + deploy       — in place
✅ 20 documentation files   — in place
✅ .gitignore working       — bin/ obj/ correctly ignored
✅ 8 known bugs fixed        — see table below
✅ dead code removed        — 4 ineffective modules deleted
⬜ git identity
⬜ commit + push
```

### ✅ Fixed Bugs

| # | Issue | Fix |
|---|---|---|
| 1 | XImage field offsets wrong (`data@32`→16, `bytes_per_line@48`→44) | new `xi_parse()` with overflow checks |
| 2 | Remote call reserved only 256 bytes of stack | remote `mmap` a 2MB scratch stack, reused per attach |
| 3 | `uninject` passed `dlclose(0)` | saves `g_payload_handle`, uses the real handle |
| 4 | Thunks unresolved; shared-library addresses not relocated | new `resolve_thunk()` + `dlsym`/`dladdr` resolution |
| 5 | `XInitThreads()` never called | called first thing in `gui_init()` |
| 6 | `is_root` never set | parsed from `Uid:` in `/proc/<pid>/status` |
| 7 | `/proc/<pid>/net/*` used as if per-process | removed (that table is network-namespace scoped) |
| 8 | 16 dead functions + 4 ineffective modules | deleted `heartbeat.c` `screen.c` `input.c` `netfilter.c` |

### ⚠️ Still Unverified

**The code has never been run on real hardware (aarch64 + actual Mythware).**
Every fix above is based on static analysis of the Xlib / aarch64 ABI;
it guarantees clean compilation and self-consistent logic, nothing more.

Suggested first run on real hardware:

```bash
./bin/jyfree -d 5
# check whether injection succeeds, then read the payload log
cat /tmp/jyfree-payload.log
# enable feature bits one at a time and watch for Student crashes
> feat lock on
> counters
```

---

<div align="center">

**[⬆ Main documentation](../README.en.md)** · **[中文主文档 →](../README.md)**
**[Documentation index](README.en.md)** · **[中文索引 →](README.md)**

For authorized local research and educational use only.
Mythware is a registered trademark of Guangzhou Shirui Software Technology
Co., Ltd. This project is not affiliated with or endorsed by them.

</div>