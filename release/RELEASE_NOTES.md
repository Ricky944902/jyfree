# jyfree v2.0.0 — 极域电子教室绕过工具（统信UOS / aarch64）

> ## ⚠️ 未经真机验证 — 请勿直接使用
>
> **本项目从未在真实的 aarch64 + 极域环境中运行过。**
>
> 代码通过了编译且逻辑自洽，但以下环节**完全没有验证**：
>
> - ❌ `ptrace` 注入能否在真实 `Student` 上成功
> - ❌ 符号地址是否匹配极域 Student **v2.7.3715**（换版本必失效）
> - ❌ 钩子安装后 `Student` 是否会崩溃
> - ❌ 各特性位拦截是否真的生效
> - ❌ Wayland/XWayland 下 X11 钩子的实际效果
>
> 请把它当作 **ptrace/hook 技术学习材料**，而不是可用成品。

---

## 快速开始

```bash
tar xzf jyfree-2.0.0-aarch64.tar.gz
cd jyfree-2.0.0-aarch64
chmod +x jyfree libjyfree.so

./jyfree --process      # 确认能找到极域进程
./jyfree -d 5           # 注入并观察
```

> `libjyfree.so` **必须与 `jyfree` 同目录**（工具从 `/proc/self/exe` 同目录开始找）。

**不需要 root，不需要安装任何库。** 但必须与极域 `Student` **同 UID** 运行。

---

## 资产

| 文件 | 说明 |
|---|---|
| `jyfree-2.0.0-aarch64.tar.gz` | 完整发布包（484K） |
| `jyfree-2.0.0-aarch64.tar.gz.sha256` | 校验和 |

### 压缩包内容

```
jyfree-2.0.0-aarch64/
├── jyfree               主程序（静态链接，仅依赖 libc）
├── libjyfree.so         注入 payload（须与 jyfree 同目录）
├── 使用指南.md          ★ 完整使用指南
├── 请先读我.txt          快速提醒
├── README.md / README.en.md
├── CHANGELOG.md / LICENSE
├── src/ include/ Makefile    源码（便于审计）
└── scripts/
    ├── x11-sysroot.sh       导出 X11 开发文件（目标机上跑，无需 sudo）
    ├── extract-mythware.sh  极域文件提取（TUI）
    └── build.sh             一键编译
```

---

## 兼容性

| 组件 | 依赖 | 说明 |
|---|---|---|
| `jyfree` | 无 | **静态链接**，自带 libc |
| `libjyfree.so` | `libc.so.6` ≥ GLIBC_2.17 | 任何 aarch64 glibc 都有 |

`GLIBC_2.17` 是 aarch64 架构的**起始版本**，Ubuntu/Debian/UOS 全系列都有。

交叉编译时通过 `include/glibc_compat.h` 的 `.symver` 把 `dl*`/`pthread*`/`shm*`
绑定到旧版本，避免 glibc 2.34 引入的版本号问题。

> 图形界面版（`jyfree-gui`）本次**未包含** —— 需要 aarch64 的 X11 开发文件。
> 命令行版功能完整，GUI 只是点按钮的外壳。
> 想要 GUI：在目标机上跑 `scripts/x11-sysroot.sh` 打包（无需 sudo），
> 拷回开发机后 `make gui ARCH=aarch64 CROSS=aarch64-linux-gnu- X11_SYSROOT=...`

---

## 功能

通过 `ptrace` 注入 payload，在 `Student` 进程内安装 **16 个内联钩子**与
**11 个 GOT 钩子**，拦截 7 类功能：

| 特性位 | 名称 | 效果 |
|---|---|---|
| `0x01` | `lock` | 锁屏 / 黑屏不出现 |
| `0x02` | `monitor` | 教师看不到你的屏幕 |
| `0x04` | `command` | 远程命令不执行 |
| `0x08` | `apps` | 不被杀进程、不被关窗口 |
| `0x10` | `policy` | 策略不下发到 root 服务 |
| `0x20` | `input` | 教师键鼠无效 |
| `0x40` | `capture` | 截屏冻结 / 全黑 / 放行 |

特性位通过共享内存（`/dev/shm/jyfree.state`）传递，**改动立即生效**，
无需重新注入。

---

## 核心实现

```
jyfree (控制器, 用户态, 与 Student 同 UID)
 ├─ ptrace 注入: ATTACH → 远程 mmap(2MB 独立栈) → 远程 dlopen → DETACH
 ├─ 共享内存 ←→ 特性位 / 拦截计数 / 心跳
 └─ 看门狗: Student 崩溃或被 systemd 重启后自动重注入

libjyfree.so (payload)
 ├─ 内联钩子 ×16   ldr x16,#8 ; br x16 ; .quad detour  (16 字节, 无需 trampoline)
 ├─ GOT 钩子 ×11   dl_iterate_phdr 扫描所有已加载 ELF
 │                 XSendEvent 只拦 type 2..6, 放行 WM_DELETE_WINDOW
 └─ 协调线程       1s 心跳 + 新模块重扫 (libcast-x11 是按需 dlopen 的)
```

### 几个值得注意的工程细节

- **符号解析不用 `dlopen`** —— 命令行版静态链接后 `dlopen` 不可用，
  改用 `src/elf_sym.c` 直接 `mmap` 磁盘上的 ELF 解析 `.dynsym`，
  支持 `ET_DYN` 与 `ET_EXEC`。已用 8 组用例验证，偏移与 `nm` 完全一致。
- **远程调用用独立栈** —— 复用目标线程的栈会被 `dlopen` 冲掉，
  改为在目标进程内 `mmap` 2MB 专门当栈。
- **跳桩跟随** —— `libGetAppsInfo.so.2` 部分导出是 4 字节 `B` 跳转桩，
  直接打 16 字节补丁会覆盖后续真实代码，`resolve_thunk()` 先跟随跳转。
- **共享库地址重定位** —— `libcastng` / `libGetAppsInfo` 是共享库，
  `nm` 偏移 ≠ 运行地址，改用 `dlsym`/`dladdr` 解析。

---

## 已知限制

1. **符号地址硬编码**，仅适用极域 Student **v2.7.3715 (2024-12-12)**
2. **开机已加载的策略无法从用户态更改**（`cms_nf.ko` 在内核侧）——
   只能阻断后续策略下发
3. **UDP 命令通道**（`4788/5512/5662/5665/5666`）协议未逆向，未做伪造
4. **QLocalServer 本地协议**未逆向（socket `srwxrwxrwx` 全局可连）
5. **教师广播是 RDP 流**，`capture` 可能不生效（`libDesk` 与
   `libcast-x11` 是两条不同路径，待课堂验证）
6. **Wayland 下 X11 钩子只影响 X 客户端** —— Student 本身在 XWayland，
   实测待确认
7. **关闭广播窗口**功能未实现
8. `setuid` 的 `Launcher` 明确不在本工具范围

---

## 反馈

真机测试请附上诊断信息（见 `使用指南.md` 第八章的收集脚本），
特别是：

```bash
nm /opt/mythware/classroom-management/Student | grep -E 'ShowLockScreen|StartMonitorPassive'
```

极域版本一旦不同，符号地址就需要重新确认。

---

仅用于本地授权研究与学习原理。极域 (Mythware) 为广州视睿软件科技有限公司
注册商标，本项目与其无任何关联。

---

**Full documentation →** [仓库 README](https://github.com/Ricky944902/jyfree#readme)