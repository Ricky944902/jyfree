# 贡献指南

**[简体中文](CONTRIBUTING.md)** · **[English](CONTRIBUTING.en.md)**

感谢你考虑为 jyfree 贡献代码。

---

## 语言约定

本项目**以中文为主**：

| 内容 | 语言 |
|---|---|
| 代码注释 | 中文 |
| 运行时输出 / 日志 | 中文 |
| GUI 文本 | 中文 |
| 文档 | **中英双语**（中文为默认） |

**提交 PR 时请同步更新两种语言的文档。** 例如修改了编译流程，
需要同时改 `docs/02-编译指南.md` 和 `docs/02-build-guide.en.md`。

---

## 提交 Issue

提交前请先搜索是否已有同类问题。

**请务必附上诊断信息**（脚本见 [`docs/04-排错手册.md`](docs/04-排错手册.md) §10）：

```bash
{
  echo "=== 系统 ===";  uname -a
  echo "=== 进程 ===";  ./bin/jyfree --process
  echo "=== 已加载库 ==="; ./bin/jyfree --modules
  echo "=== 注入状态 ==="; ./bin/jyfree -d 5 -s
  echo "=== payload 日志 ==="; tail -50 /tmp/jyfree-payload.log
  echo "=== 符号地址 ==="
  nm /opt/mythware/classroom-management/Student 2>/dev/null \
    | grep -E 'ShowLockScreen|StartMonitorPassive'
} > /tmp/jyfree-report.txt 2>&1
```

然后把 `/tmp/jyfree-report.txt` 附在 issue 里。

### 有用的 issue 标签

| 标签 | 含义 |
|---|---|
| `需要现场验证` | 需要在真实极域环境验证 |
| `符号地址` | 涉及 `payload_state.h` 里的硬编码地址 |
| `Wayland` | 涉及 XWayland 行为差异 |
| `文档` | 文档改进 |
| `平台` | 编译 / 依赖问题 |

---

## 开发环境

```bash
# 编译
make
make debug                    # -O0 -g3 -DDEBUG

# 清理
make clean

# 架构检查
make arch-check
```

### 架构注意事项

payload 的内联钩子地址是 **aarch64 专用**。在 x86 上编译只能验证代码通过，
**不能实际运行**。跨平台开发请用：

```bash
make ARCH=aarch64 CROSS=aarch64-linux-gnu-
```

---

## 代码规范

### 风格

跟随现有代码：

- 4 空格缩进
- `//` 注释，**中文**
- 函数命名 `snake_case`，全局变量 `g_` 前缀，静态变量 `s_` / `g_` 前缀
- 编译选项固定 `-Wall -Wextra`，**不允许新增警告**

```bash
make 2>&1 | grep -E 'warning|error'   # 必须为空
```

### ⚠️ 关键：`include/payload_state.h` 两侧同步

这个头文件**同时被控制器和 payload 包含**，定义了：

- 特性位（`JY_FEAT_*`）
- 符号地址（`SYM_*`）
- 共享内存结构体（`JySharedState`）

**任何一侧修改都必须同步**，否则结构体布局错位会导致内存损坏：

```c
// 控制器 src/inject.c 与 payload src/hook_payload.c 都映射同一块 /dev/shm
// 结构体布局不一致 → 计数器错位 / 心跳读到垃圾
```

### 不要做的事

- ❌ 不要引入外部依赖（保持 `pthread` / `dl` / `rt` / `X11`）
- ❌ 不要使用 C++ 或 C99 之后的激进特性（目标 gcc 8.3）
- ❌ 不要在 payload 里引用控制器的符号（会 dlopen 失败）
- ❌ 不要硬编码新的架构相关地址而不加注释说明来源

### 加新钩子时

1. 在目标机器上确认符号与地址
   ```bash
   nm /opt/mythware/classroom-management/Student | grep <symbol>
   ```
2. 在 `include/payload_state.h` 加 `SYM_*` 常量，**注明版本号**
3. 在 `src/hook_payload.c` 写 detour 函数，**返回 `w0 = 0`**
4. 在 `install_inline_hooks()` 里挂钩
5. 在 `g_got_hooks[]` 表里加 X11 钩子（如适用）
6. 更新中英双语的 `docs/01-逆向分析.md`

---

## Pull Request

1. 从 `main` 开分支：`feat/xxx` / `fix/xxx` / `docs/xxx`
2. 保证零警告：
   ```bash
   make clean && make && make gui
   ```
3. 更新文档（**中英双语**）
4. PR 描述里写清楚：**改了什么**、**为什么**、**怎么验证的**

### PR 标题

```
feat: 拦截 CastClient::setInputGrab 防止输入抓取
fix: 修正 XShmGetImage 返回值处理
docs: 补充交叉编译步骤
```

---

## 路线图

以下方向欢迎 PR：

- [ ] **UDP 通道逆向** —— `4788/5512/5662/5665/5666` 的命令格式
- [ ] **QLocalServer 协议** —— 本地 IPC 的 Qt 帧格式（socket 全局可连）
- [ ] **广播路径截屏** —— 让 `capture` 也能作用于 RDP 流
- [ ] **版本自适应** —— 运行时从 `Student` 的 `.dynsym` 解析符号，
      摆脱硬编码地址
- [ ] **GUI 改进** —— 日志过滤、特性位热键、状态托盘

---

## 行为准则

友善、专业、尊重他人。不接受人身攻击、歧视或骚扰。
对技术问题持开放态度 —— 不同实现思路都可以讨论。

---

## 许可

贡献即表示同意你的代码以 **MIT 许可**发布。

---

**[English version →](CONTRIBUTING.en.md)**