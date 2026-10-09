<div align="center">

# 📖 Documentation Index

**jyfree** — Mythware Classroom Bypass Tool (UOS / aarch64)

**[简体中文](README.md)** · **[English](README.en.md)**

</div>

---

## Documents

| # | Document | Contents | Status |
|---|---|---|---|
| 01 | [逆向分析](01-逆向分析.md) · [EN](01-reverse-engineering.en.md) | Environment, process model, protocol, **every hook site and symbol address**, injection scheme, open questions | ✅ |
| 02 | [编译指南](02-编译指南.md) · [EN](02-build-guide.en.md) | Dependencies, building, cross-compiling, arch requirements, install, build problems | ✅ |
| 03 | [使用手册](03-使用手册.md) · [EN](03-user-guide.en.md) | Options, commands, feature bits, GUI, systemd, typical workflows | ✅ |
| 04 | [排错手册](04-排错手册.md) · [EN](04-troubleshooting.en.md) | Symptom → cause → fix, collecting a full report | ✅ |
| 05 | [GitHub 发布指南](05-GitHub发布指南.md) · [EN](05-github-upload.en.md) | **What to upload / what not to**, publishing steps, legal risks | ✅ |

---

## Project Files

| Path | Description |
|---|---|
| [`../README.md`](../README.md) | Main documentation (Chinese, primary) |
| [`../README.en.md`](../README.en.md) | Main documentation (English) |
| [`../Makefile`](../Makefile) | Build configuration |
| [`../LICENSE`](../LICENSE) | MIT |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | Contribution guide |
| [`../CHANGELOG.md`](../CHANGELOG.md) | Changelog |
| [`../include/payload_state.h`](../include/payload_state.h) | ★ feature bits / symbol addresses / shared-memory structs |
| [`../scripts/build.sh`](../scripts/build.sh) | One-shot build script |

---

## Quick Entry

```bash
make                        # build
./bin/jyfree                # inject + interactive
./bin/jyfree --help         # CLI options
./bin/jyfree -d 5           # maximum verbosity
cat /tmp/jyfree-payload.log # payload-internal log
```

---

## Reading By Need

| I want to… | Read |
|---|---|
| Understand the mechanism and where addresses come from | [01 Reverse Engineering](01-reverse-engineering.en.md) §6, §7, §9 |
| Cross-compile for aarch64 | [02 Build Guide](02-build-guide.en.md) §4 |
| Know why the same UID is mandatory | [01 Reverse Engineering](01-reverse-engineering.en.md) §8 |
| Look up a command | [03 User Guide](03-user-guide.en.md) §4 |
| Enable systemd auto-start | [03 User Guide](03-user-guide.en.md) §8 |
| Fix a failed injection | [04 Troubleshooting](04-troubleshooting.en.md) §2, §3 |
| Learn why the capture counter stays 0 | [04 Troubleshooting](04-troubleshooting.en.md) §6 |
| Support a different Mythware version | [04 Troubleshooting](04-troubleshooting.en.md) §3.3 |
| **Read before pushing to GitHub** | [05 Publishing Guide](05-github-upload.en.md) |
| What must never be committed | [05 Publishing Guide](05-github-upload.en.md) §4 |

---

## Language Convention

- **Code comments, runtime output, GUI text**: Chinese-primary
  (the target audience is campus deployments in mainland China)
- **Documentation**: bilingual, Chinese is the default
- When contributing, please update both language versions together

---

<div align="center">

For authorized local research and educational use only.
Mythware is a registered trademark of Guangzhou Shirui Software Technology
Co., Ltd. This project is not affiliated with or endorsed by them.

**[⬆ Main documentation](../README.en.md)** · **[中文索引 →](README.md)**

</div>