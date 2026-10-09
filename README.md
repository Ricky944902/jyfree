<div align="center">

# jyfree

**极域电子教室绕过工具** — 统信UOS 版（aarch64）

通过 `ptrace` 注入 `Student` 进程，在其内部安装内联钩子与 GOT 钩子，
实时拦截**锁屏、监视、远程控制、应用策略、策略下发、键鼠注入、截屏**。

> **无需 root** — 注入只要求与 `Student` 同 UID。

[![语言](https://img.shields.io/badge/lang-简体中文-informational?style=flat-square&logo=github)](README.md)
[![English](https://img.shields.io/badge/lang-English-informational?style=flat-square&logo=github)](README.en.md)
[![License](https://img.shields.io/badge/license-MIT-blue?style=flat-square)](LICENSE)
[![Arch](https://img.shields.io/badge/arch-aarch64-orange?style=flat-square)](#目标环境)
[![C](https://img.shields.io/badge/c-C11-lightgrey?style=flat-square&logo=c)](src)
[![Deps](https://img.shields.io/badge/deps-pthread%20%7C%20dl%20%7C%20rt%20%7C%20X11-brightgreen?style=flat-square)](#依赖)

</div>

---

<div align="center">

**[简体中文](README.md)** · **[English](README.en.md)**

</div>

---

> ⚠️ **免责声明** — 本工具仅用于本地授权研究与教学场景。极域 (Mythware) 为
> 广州视睿软件科技有限公司的注册商标，本项目与其无任何关联。

---

> # ⚠️ 未经真机验证 — 请勿直接使用
>
> **本项目从未在真实的 aarch64 + 极域环境中运行过。**
>
> 代码通过了编译且逻辑自洽，但以下环节**完全没有验证**：
>
> - ❌ `ptrace` 注入能否在真实 `Student` 上成功
> - ❌ 符号地址是否匹配 Student **v2.7.3715**（换版本必失效）
> - ❌ 钩子安装后 `Student` 是否会崩溃
> - ❌ 各特性位拦截是否真的生效
> - ❌ Wayland/XWayland 下 X11 钩子的实际效果
>
> **请勿把它当成可用工具。** 本仓库的定位是
> **逆向分析产物 + ptrace/hook 技术学习材料**。
>
> 在真机上尝试的最低风险步骤：
>
> ```bash
> ./bin/jyfree -d 5          # 先只看注入是否成功
> cat /tmp/jyfree-payload.log
> ```
>
> 确认注入正常后，**一次只开一个特性位**，观察 Student 是否崩溃：
>
> ```bash
> > feat list
> > feat lock on            # 只开锁屏
> > counters                # 看计数有没有涨
> ```
>
> 出现异常立即：
>
> ```bash
> > uninject                # 还原所有内联钩子
> > quit
> ```

---

## 30 秒上手

```bash
git clone <this-repo> && cd JiYuTrainer
make

# 注入极域（需极域已启动，且与本工具同 UID）
./bin/jyfree

# 交互提示符
> status       # 注入状态
> counters     # 拦截计数 ← 验证钩子是否真的生效
> quit         # 退出并还原所有钩子
```

图形界面：`./bin/jyfree-gui`

---

## 它能做什么

极域 Linux 版把大量控制功能做成了用户态进程内的函数调用。
jyfree 在这些函数入口安装钩子，让它们直接返回：

| 特性位 | 名称 | 拦截的函数 | 效果 |
|---|---|---|---|
| `0x01` | `lock` | `CStudentMainWork::ShowLockScreen`<br>`::ShowBlackScreen` | 锁屏/黑屏不生效 |
| `0x02` | `monitor` | `::StartMonitorPassive`<br>`::StartRdpMonitorPassive`<br>`::ProcessDeskMonitorCommand` | 教师看不到屏幕 |
| `0x04` | `command` | `::ExecuteRemoteCmd`<br>`::ProcessRemoteCommand` | 远程命令不执行 |
| `0x08` | `apps` | `libGetAppsInfo::KillProcess`<br>`::CloseTopWindow` / `CloseApps` | 不会被杀进程/关窗口 |
| `0x10` | `policy` | `::UpdatePoliciesToStudentService`<br>`::UpdateUsbPolicy...` / `UpdateWebPolicy...` | 策略不下发 |
| `0x20` | `input` | `tf_peer_*_event`（freerdp）<br>`XGrab*` / `XTestFake*` / `XWarpPointer` | 教师键鼠无效 |
| `0x40` | `capture` | `XGetImage` / `XShmGetImage`（GOT） | 截屏冻结或全黑 |

特性位通过共享内存传递，**改动立即生效，无需重新注入**。

---

## 工作原理

```
jyfree (控制器, 用户态, 与 Student 同 UID)
 │
 ├─ ptrace 注入引擎
 │    ATTACH → 保存 regs → 远程 mmap → 远程 dlopen → 还原 regs → DETACH
 │
 ├─ 共享内存  /dev/shm/jyfree.state   ←→   特性位 / 拦截计数器 / 心跳
 │
 └─ 看门狗 (1s 轮询)
      Student 被 systemd 重启后自动重新注入

libjyfree.so (payload, 被 dlopen 进 Student 进程内部)
 │
 ├─ 内联钩子 ×16
 │    aarch64 补丁 (16 字节，无需 trampoline):
 │      ldr x16, #8
 │      br  x16
 │      .quad detour
 │
 ├─ GOT 钩子 ×11
 │    dl_iterate_phdr 遍历所有已加载 ELF
 │    解析 PT_DYNAMIC → DT_JMPREL/DT_RELA → 按名字匹配改写
 │
 └─ 协调线程
      每秒更新心跳 + 检测运行期 dlopen 的 libcast-x11.so
```

关键点：`Student` 是 **ET_EXEC 非 PIE** 二进制，
所以 `nm` 给出的地址就是运行地址，payload 直接把这些地址写进补丁。

```
$ nm /opt/mythware/classroom-management/Student | grep ShowLockScreen
0000000000441ed4 T _ZN16CStudentMainWork14ShowLockScreenEi
                 ^^^^^^^^ 就是运行地址
```

---

## 项目结构

```
JiYuTrainer/
├── README.md                     ← 本文件（中文，主文档）
├── README.en.md                  ← English
├── LICENSE                       ← MIT
├── CHANGELOG.md / .en.md
│
├── bin/                          ← 编译产物
│   ├── jyfree                    ★ 主操作文件（命令行版）
│   ├── jyfree-gui                   图形界面版
│   └── libjyfree.so                 payload（须与 jyfree 同目录）
│
├── include/                      ← 头文件
│   ├── jiyu.h                       控制器公共接口
│   ├── payload_state.h            ★ 两侧共享（特性位/符号地址/结构体）
│   └── gui.h                       Xlib GUI
│
├── src/                          ← 源代码
│   ├── main.c                       CLI 入口 + 命令表
│   ├── inject.c                     ptrace 注入引擎 + 共享内存 + 看门狗
│   ├── hook_payload.c            ★  payload（编译成 libjyfree.so）
│   ├── process.c                    /proc 扫描找 Student
│   ├── gui.c / gui_main.c           Xlib 图形界面
│   └── utils.c                      配置 / 日志 / 调试
│
├── scripts/                      ← 脚本
│   ├── build.sh                  ★  一键编译（依赖 + 架构检查）
│   ├── install.sh                   一键安装
│   └── extract-mythware.sh          极域文件提取（TUI）
│
├── deploy/                       ← 部署
│   ├── jyfree.service                systemd user 单元
│   └── jyfree.desktop                桌面应用项
│
├── docs/                         ← 详细文档（中英双语）
│   ├── README.md                     文档索引
│   ├── 01-逆向分析.md / .en.md
│   ├── 02-编译指南.md / .en.md
│   ├── 03-使用手册.md / .en.md
│   └── 04-排错手册.md / .en.md
│
└── obj/                          ← 编译中间文件（可删）
```

---

## 依赖

| 依赖 | 用途 | 必需 |
|---|---|---|
| `gcc` ≥ 6, `make` | 编译 | ✅ |
| `libx11-dev` | `jyfree-gui` 图形界面版 | 仅 GUI |
| `libpthread` `libdl` `librt` | ptrace / dlopen / shm_open | ✅ |
| aarch64 交叉工具链 | 在 x86 上编译 aarch64 版 | 交叉时 |

```bash
# UOS / Debian 系
sudo apt install build-essential libx11-dev
sudo apt install gcc-aarch64-linux-gnu      # 交叉编译时
```

---

## 编译

```bash
# 方式一：脚本（推荐，会检查依赖与架构）
./scripts/build.sh

# 方式二：Makefile
make              # 全部
make cli          # 仅命令行版
make gui          # 仅图形界面版
make payload      # 仅 payload
make debug        # 调试版 (-O0 -g3 -DDEBUG)
make install      # 安装到 /usr/local
make arch-check   # 检查架构匹配
make help         # 全部目标
```

**交叉编译**（在 x86 上构建 aarch64 版）：

```bash
./scripts/build.sh --cross
# 或
make ARCH=aarch64 CROSS=aarch64-linux-gnu-
```

> ⚠️ **架构要求** — payload 中的内联钩子地址（`0x441ed4` 等）是 **aarch64 专用**。
> 在 x86 上编译只能验证代码通过，**不能实际运行**。
> 用 `make arch-check` 检查。

详见 [`docs/02-编译指南.md`](docs/02-编译指南.md) · [English](docs/02-build-guide.en.md)

---

## 使用

### 主操作文件 `bin/jyfree`

```bash
./bin/jyfree              # 注入 + 进入交互模式
./bin/jyfree --status     # 查看注入状态后退出
./bin/jyfree --process    # 查看极域进程
./bin/jyfree --modules    # 查看已加载的极域库
./bin/jyfree -d 5         # 最详细日志
```

### 交互命令

| 命令 | 说明 |
|---|---|
| `status` | 注入状态、进程状态、已装钩子数 |
| `feat list` | 列出特性位 |
| `feat <名> on\|off` | 开关特性 |
| `capture <模式>` | `pass` / `freeze` / `black` / `white` |
| `counters` | **各特性拦截次数**（验证钩子是否生效） |
| `modules` | 已加载的极域库 + 计数 |
| `inject` / `uninject` | 手动注入 / 卸载还原 |
| `process` | 三个极域进程详情 |
| `freeze` / `thaw` | `SIGSTOP` / `SIGCONT` |
| `log [文件]` | payload 日志尾部 |
| `quit` | 退出（自动还原钩子） |

### 图形界面

```bash
./bin/jyfree-gui          # 点「注入」按钮
./bin/jyfree-gui -i       # 启动后立即注入
```

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

详见 [`docs/03-使用手册.md`](docs/03-使用手册.md) · [English](docs/03-user-guide.en.md)

---

## 目标环境

| 项 | 值 |
|---|---|
| 系统 | UOS Desktop 20 E |
| 架构 | **aarch64** |
| 内核 | `5.10.97-arm64-desktop` |
| 会话 | Wayland (kwin)；Student 以 `QT_QPA_PLATFORM=xcb` 跑在 **XWayland** |
| 用户 | `uid=1000`，**不在 `input` 组** → 无法直接操作 `/dev/input` |
| Yama | 不存在 → 同 UID `ptrace` 可用 |
| 极域版本 | v2.7.3715 (2024-12-12) |

安装路径 `/opt/mythware/classroom-management/`：

| 文件 | 说明 |
|---|---|
| `Student` | 主程序，ET_EXEC 非 PIE，未 strip ← **注入目标** |
| `StudentAgent` | 看门狗，systemd user 单元主进程 |
| `StudentService` | root，网页 / USB / 应用策略 |
| `libcastng.so.0.1` (192MB) | cast NG / freerdp 引擎 |
| `libcast-x11.so` (126MB) | X11 后端，**运行期 dlopen** |
| `libDesk.so.2` | 截屏采集 |
| `libGetAppsInfo.so.2` | 应用策略 |

监听端口：UDP `4788 5512 5662 5665 5666`，TCP `4806`，
组播 `225.2.2.11:5542`（控制）/ `:5547`（媒体）

---

## 排错速查

```bash
cat /tmp/jyfree-payload.log   # payload 内部日志
./bin/jyfree -d 5             # 控制器详细日志
./bin/jyfree --process        # 找到 Student 了吗?
id -u                        # UID 一致吗?
```

| 现象 | 原因 |
|---|---|
| `PTRACE_ATTACH 被拒绝` | UID 与 `Student` 不一致 |
| `payload 未在超时内就绪` | 架构不符 / payload 找不到 / 符号地址版本不符 |
| `目标进程处于 'T' 状态` | Student 被 SIGSTOP，先 `thaw` |
| `counters` 全 0 | 正常 —— 教师还没操作；先看 `modules` |
| 截屏计数不涨 | 教师广播走 RDP 流（`libcast-x11`），与 `libDesk` 不同路径 |

详见 [`docs/04-排错手册.md`](docs/04-排错手册.md) · [English](docs/04-troubleshooting.en.md)

---

## 已知限制

1. **内联钩子地址硬编码**，仅适用 Student **v2.7.3715**。
   换版本需重新确认：
   ```bash
   nm /opt/mythware/classroom-management/Student | grep ShowLockScreen
   ```
   然后更新 `include/payload_state.h`。
2. **开机已加载的策略无法从用户态更改**（`cms_nf.ko` 在内核侧）——
   只能阻断后续策略下发。
3. **UDP 未认证命令通道**（`4788/5512/5662/...`）协议未逆向，未做伪造。
4. **QLocalServer 本地协议**未逆向（socket `srwxrwxrwx` 全局可连）。
5. **教师广播是 RDP 流**，截屏覆盖可能不生效（待课堂验证）。
6. **Wayland 下 X11 钩子只影响 X 客户端** —— Student 本身在 XWayland，
   实测有效但需验证计数器增量。
7. `setuid` 的 `Launcher` **明确不在本工具范围**。

---

## 文档

**[📖 完整文档索引 →](docs/README.md)**

| 文档 | 内容 |
|---|---|
| [01 · 逆向分析](docs/01-逆向分析.md) | 环境、进程模型、协议、**全部钩子点与符号地址**、注入方案、待验证项 |
| [02 · 编译指南](docs/02-编译指南.md) | 依赖、编译、交叉编译、架构要求、安装 |
| [03 · 使用手册](docs/03-使用手册.md) | 参数、交互命令、特性位、GUI、systemd |
| [04 · 排错手册](docs/04-排错手册.md) | 故障现象 → 原因 → 处理 |
| [05 · GitHub 发布指南](docs/05-GitHub发布指南.md) | **哪些文件该传/不该传**、完整发布步骤、法律风险 |

---

## 贡献

欢迎提交 issue 和 PR。见 [`CONTRIBUTING.md`](CONTRIBUTING.md)。

## 致谢

- [imengyu/JiYuTrainer](https://github.com/imengyu/JiYuTrainer) — Windows 版原始项目（MIT）
- [BengbuGuards/MythwareToolkit](https://github.com/BengbuGuards/MythwareToolkit) — 极域协议研究参考
- 目标机器上所有实测数据提供者

## 许可

[MIT](LICENSE) · 仅用于本地授权研究与教学场景