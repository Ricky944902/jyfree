# 更新日志

本项目的版本记录。

格式参考 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/)，
版本号遵循 [语义化版本](https://semver.org/lang/zh-CN/)。

---

## [2.0.0] — 2026-10-08

首个针对 **统信UOS (aarch64)** 极域学生端的完整重写。

### 新增

- **ptrace 注入引擎** —— `PTRACE_ATTACH` → 远程 `mmap` → 远程 `dlopen` → 还原 regs → `DETACH`
- **注入 payload** `libjyfree.so` —— `dlopen` 进 `Student` 进程内部自装钩子
- **内联钩子 ×16** —— aarch64 16 字节补丁（`ldr x16,#8 ; br x16 ; .quad detour`），无需 trampoline
- **GOT 钩子 ×11** —— `dl_iterate_phdr` 扫描所有已加载 ELF，匹配
  `R_AARCH64_{JUMP_SLOT,GLOB_DAT}`
- **7 个特性位** —— `lock` `monitor` `command` `apps` `policy` `input` `capture`，
  通过共享内存实时切换
- **共享内存** `/dev/shm/jyfree.state` —— 特性位 + 拦截计数器 + payload 心跳
- **看门狗** —— 1s 轮询，`Student` 被 systemd 重启 / pid 复用 / 心跳停滞时自动重注入
- **截屏三种模式** —— `pass` / `freeze` / `black`
- **`uninject`** —— 触发 payload destructor，还原所有内联钩子
- **GUI 版** `jyfree-gui` —— Xlib 实现，含实时日志窗口与特性位开关
- **交互式 CLI** —— 16 条运行时命令
- **配置持久化** —— `~/.config/jiyu-trainer/config.json`
- **日志** —— 控制器日志（10MB 轮转）+ payload 独立日志
- **构建系统** —— Makefile（含交叉编译）、`scripts/build.sh` 一键编译
- **中英双语文档** —— README + 4 篇详细文档

### 拦截点（Student v2.7.3715）

| 类别 | 函数 | 地址 |
|---|---|---|
| 锁屏 | `CStudentMainWork::ShowLockScreen` | `0x441ed4` |
| 锁屏 | `::ShowBlackScreen` | `0x43fa4c` |
| 监视 | `::StartMonitorPassive` | `0x44e670` |
| 监视 | `::StartRdpMonitorPassive` | `0x44dbe0` |
| 监视 | `::ProcessDeskMonitorCommand` | `0x44eb44` |
| 命令 | `::ExecuteRemoteCmd` | `0x441a80` |
| 命令 | `::ProcessRemoteCommand` | `0x4438c8` |
| 策略 | `::UpdatePoliciesToStudentService` | `0x44d34c` |
| 策略 | `::UpdateUsbPolicyToStudentService` | `0x44cd28` |
| 策略 | `::UpdateWebPolicyToStudentService` | `0x44ce88` |
| 应用 | `libGetAppsInfo::KillProcess` | `0x82f8` |
| 应用 | `::CloseTopWindow` | `0x8344` |
| 应用 | `::DoAppPolicyControl` | `0x8340` |
| 输入 | `tf_peer_keyboard_event` | `0x22ebf0` |
| 输入 | `tf_peer_mouse_event` | `0x22e750` |
| 输入 | `CastClient::setInputGrab` | `0x2083c0` |

### 移除

v1.x 的以下实现全部废弃，因为与实测结论不符：

- ~~伪造心跳包（`MYTH` magic）~~ —— 改为共享内存心跳
- ~~按端口伪造组播协议~~ —— 未逆向，不伪造
- ~~直接操作 `/dev/input/event*` 作为主路径~~ —— 用户不在 `input` 组，改为进程内钩子
- ~~iptables 阻断作为主方案~~ —— 降级为可选补充
- ~~`studentmain` / `StudentMain.exe` 进程名~~ —— Linux 版实为 `Student`

---

## [1.1.0]

- 配置文件读写
- 日志文件输出与轮转
- 进程冻结 / 恢复（`SIGSTOP` / `SIGCONT`）
- GUI 日志查看窗口
- GUI 设置对话框
- systemd 服务与 `.desktop` 集成

> ⚠️ 1.x 基于对 Windows 版的推测实现，**未在真实 Linux 极域环境验证**，
> 架构与进程名均有误。已被 2.0.0 完全取代。

---

## [1.0.0]

- 初始版本：进程发现、输入解锁、屏幕绕过、心跳模拟（Windows 版移植思路）

---

## 计划中

- **UDP 通道逆向** —— `4788/5512/5662/5665/5666` 的命令格式
- **QLocalServer 协议** —— 本地 IPC 的 Qt 帧格式（socket `srwxrwxrwx` 全局可连）
- **广播路径截屏** —— 让 `capture` 作用于 RDP 流（当前只覆盖 `libDesk` 的 `XGetImage`）
- **版本自适应** —— 运行时从 `.dynsym` 解析符号，摆脱硬编码地址
- **Wayland 原生** —— 不依赖 XWayland 的截屏路径

---

[简体中文](CHANGELOG.md) · **[English](CHANGELOG.en.md)**