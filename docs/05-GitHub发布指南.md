# 05 · GitHub 发布指南

**[简体中文](05-GitHub发布指南.md)** · **[English](05-github-upload.en.md)**

这份文档说明：**哪些文件要上传、哪些只是说明文档、哪些绝对不能上传**，以及完整的发布步骤。

---

## 一览表

| 分类 | 数量 | 上传 |
|---|---|---|
| **源代码**（`src/` `include/` `Makefile`） | 11 | ✅ 必传 |
| **脚本 + 部署**（`scripts/` `deploy/`） | 5 | ✅ 必传 |
| **说明文档**（`.md` + `LICENSE` + `.gitignore`） | 20 | ✅ 必传 |
| **编译产物** | `bin/` `obj/` | ❌ 不传 |
| **极域文件** | `*.deb` `mythware-extracted/` | ❌ **绝对不传（法律风险）** |
| **本地日志 / 配置** | `*.log` `config.json` | ❌ 不传 |

**合计上传 36 个文件。**

---

## 一、源代码（11 个文件，必须上传）

别人 `git clone` 后要能直接编译。

```
Makefile                    编译配置（根目录，不可省略）

include/                    头文件（3）
  ├── jiyu.h                   控制器公共接口
  ├── payload_state.h        ★ 特性位 / 符号地址 / 共享内存结构体
  └── gui.h                   Xlib GUI

src/                        源文件（7）
  ├── main.c                   CLI 入口 + 命令表
  ├── inject.c                 ptrace 注入引擎 + 共享内存 + 看门狗
  ├── hook_payload.c        ★  payload（编译成 libjyfree.so）
  ├── process.c                /proc 扫描找 Student
  ├── gui.c                    Xlib 界面实现
  ├── gui_main.c               GUI 入口
  └── utils.c                  配置/日志/调试
```

这些必须上传，否则别人无法编译。

---

## 二、脚本 + 部署（5 个文件，必须上传）

```
scripts/
  ├── build.sh              ★  一键编译（含依赖与架构检查）
  ├── install.sh               一键安装
  └── extract-mythware.sh      极域文件提取（TUI）

deploy/
  ├── jyfree.service             systemd user 单元
  └── jyfree.desktop             桌面应用项
```

> `scripts/extract-mythware.sh` 是**工具脚本**（提取极域文件供分析），
> **不是**极域文件本身，可以上传。
> 但它的输出目录 `mythware-extracted/` **绝对不能上传**（见第四节）。

---

## 三、说明文档（20 个文件，必须上传）

```
根目录（9）
  ├── README.md              ★ 中文主文档（GitHub 首页显示这个）
  ├── README.en.md             English
  ├── CHANGELOG.md / .en.md    更新日志 ×2
  ├── CONTRIBUTING.md / .en.md 贡献指南 ×2
  ├── LICENSE                  MIT 许可（开源合规必须有）
  └── .gitignore             ★ 忽略规则（必须有）

docs/（11）
  ├── README.md / README.en.md         文档索引 ×2
  ├── 01-逆向分析.md / 01-reverse-engineering.en.md
  ├── 02-编译指南.md / 02-build-guide.en.md
  ├── 03-使用手册.md / 03-user-guide.en.md
  ├── 04-排错手册.md / 04-troubleshooting.en.md
  └── 05-GitHub发布指南.md / 05-github-upload.en.md   （本文件）
```

### 为什么 `.gitignore` 必须上传

它是**防止误传编译产物和极域文件的第一道防线**，别人克隆后也会用到。

---

## 四、编译产物（不传，已被 `.gitignore` 拦截）

```
bin/                        ✗ jiyree  jyfree-gui  libjyfree.so
obj/                        ✗ *.o  *.lo
```

**理由：**

- 别人自己 `make` 就能生成
- 你的二进制是 x86 的，对 aarch64 目标机没用
- 仓库体积暴涨，二进制 diff 无法审查

`.gitignore` 已覆盖：

```gitignore
bin/
obj/
*.o
*.lo
*.pic.o
*.so
```

---

## 五、⚠️ 绝对不能上传（法律风险）

### 极域的安装包和文件

```
*.deb                           ✗
mythware-extracted/             ✗
mythware-deb-extracted/         ✗
mythware-analysis/              ✗
```

**理由：**

1. **版权** —— 极域是**广州视睿软件科技有限公司**的商业软件，
   二进制、库文件、图标都是它的 copyrighted material。
   放进自己的 GitHub 属于**未经授权的再分发**。
2. **法律后果** —— 可能面临 DMCA takedown、下架、诉讼。
   **DMCA 不问你有没有授权。**
3. **账号风险** —— GitHub 会封禁或强制删除，连带你其他项目。

### ✅ 正确做法

| 想做什么 | 正确方式 |
|---|---|
| 提取极域文件分析 | 本地跑 `scripts/extract-mythware.sh`，输出留本地 |
| issue 里贴结论 | 贴**符号地址、函数名、协议结构**（事实信息），不贴文件 |
| 给别人样本 | 让对方**自己**从已安装的机器提取 |
| 引用代码片段 | 少量片段说明问题，注明来源 |

**开源工具 ≠ 携带被投机的软件。** 这是重要区别。

### 其他不要上传

```
*.log  jyfree-payload.log  config.json
.vscode/  .idea/  *.swp  *~  .DS_Store
jyfree-report.txt          （含目标机信息）
```

---

## 六、完整发布步骤

### 步骤 1 · 配置 Git 身份（最容易漏的一步）

```bash
git config --global user.name  "你的GitHub用户名"
git config --global user.email "你的GitHub邮箱"
```

> 邮箱要用 GitHub 账号绑定的那个，否则 commit 头像不会关联。
> 想匿名可用 `你的用户名@users.noreply.github.com`。

**不配这一步会报 `Author identity unknown`，commit 直接失败。**

### 步骤 2 · 初始化仓库

```bash
cd /d/JiYuTrainer        # Git Bash
git init
git branch -M main
```

### 步骤 3 · 清理并检查

```bash
make distclean                    # 删编译产物（Git Bash 需 make 可用）

find . -name "*.deb" -o -name "mythware-extracted" -type d
# 应无输出

git status --short --ignored | grep '^!!'
# 应看到 !! bin/  !! obj/  → 说明 .gitignore 生效
```

### 步骤 4 · 添加

```bash
git add .
```

### 步骤 5 · ⚠️ 核对（关键）

```bash
git diff --cached --name-only | wc -l     # 应为 36
git status                                 # 逐项核对
```

对照本文前三节：

- `src/` `include/` `Makefile` `scripts/` `deploy/` `docs/` `*.md` → ✅
- `bin/` `obj/` → ❌ 不该出现
- 任何 `.deb` / `mythware-*` → ❌ **立刻停止**

数量不对或有异常文件时：

```bash
git rm --cached <路径>     # 移出暂存（不删本地）
```

### 步骤 6 · 提交

```bash
git commit -m "feat: jyfree v2.0.0 - UOS/aarch64 极域拦截工具"
```

### 步骤 7 · 创建 GitHub 仓库（网页操作）

1. **+ → New repository**
2. Repository name：`jyfree`
3. Description：`极域电子教室绕过工具 (统信UOS / aarch64)`
4. ⚠️ **不要勾** "Add a README file"
5. ⚠️ **不要勾** "Add .gitignore"
6. ⚠️ **不要勾** "Choose a license"
7. **Create repository**

> 勾了会和本地文件冲突，导致 `git push` 被拒绝。

### 步骤 8 · 关联并推送

```bash
git remote add origin https://github.com/<用户名>/jyfree.git
git remote -v                 # 确认（不要重复 add）
git push -u origin main
```

### 步骤 9 · 设置话题（网页）

**About** 填描述，**Topics** 加：

```
mythware jiyu classroom uos aarch64 arm64
reverse-engineering ptrace hook linux bypass
```

### 步骤 10 · 验证（最重要）

```bash
cd ..
git clone https://github.com/<用户名>/jyfree.git jyfree-test
cd jyfree-test
ls -la                    # 只有源文件，没有 bin/ obj/
./scripts/build.sh        # 能编译 = 别人能用
```

---

## 七、常见错误

### `Author identity unknown`

```bash
git config --global user.name  "你的用户名"
git config --global user.email "你的邮箱"
git commit -m "..."       # 重新提交
```

### `src refspec main does not match any`

**原因：没有 commit**（上一步失败了）。先成功 commit，再 push。

### `Updates were rejected`

远程已有提交（创建仓库时勾了 README）：

```bash
git pull origin main --allow-unrelated-histories
# 解决冲突后
git push -u origin main
```

### `remote origin already exists`

`git remote add` 重复执行了。检查后再决定：

```bash
git remote -v                 # 看当前指向
git remote set-url origin https://github.com/<用户名>/jyfree.git   # 改地址
# git remote remove origin    # 或删掉重建
```

### 已误传 `bin/`

```bash
git rm -r --cached bin obj
git commit -m "chore: untrack build artifacts"
git push
```

> 这只停止跟踪，**不会清除已有历史**。

### 已误传 `*.deb` 或极域文件

**先下架，别先改历史：**

```
Settings → Danger Zone → Delete this repository
```

下架最快、风险最低。要保留仓库再用 `git filter-repo` 清历史，
但那会重写所有 commit hash，所有协作者必须重新克隆 —— 通常重建更省事。

---

## 八、检查清单

- [ ] Git 身份已配置
- [ ] `git diff --cached --name-only | wc -l` = 36
- [ ] 没有 `bin/` `obj/`
- [ ] 没有 `.deb` / `mythware-*`
- [ ] `src/` `include/` `scripts/` `deploy/` 齐全
- [ ] `README.md` `LICENSE` `.gitignore` 齐全
- [ ] 中英双语文档成对
- [ ] 文档内部链接无失效
- [ ] 全新克隆后 `make` 成功
- [ ] 仓库描述与话题已设置

---

## 九、当前状态

```
✅ 源代码 11 个文件        — 就位
✅ 脚本 + 部署 5 个        — 就位
✅ 说明文档 20 个          — 就位
✅ .gitignore 生效         — bin/ obj/ 已正确忽略
✅ 5 个已知 Bug 已修复     — 见下表
✅ 死代码已清理            — 删除 4 个无效模块
⬜ git 身份配置
⬜ 提交 + 推送
```

### ✅ 已修复的历史 Bug

| # | 问题 | 修复方式 |
|---|---|---|
| 1 | XImage 字段偏移错误（`data@32`→16、`bytes_per_line@48`→44） | 新增 `xi_parse()` 安全解析 + 溢出校验 |
| 2 | 远程调用只给 256 字节栈 | 改为远程 `mmap` 2MB 独立栈，每次 attach 复用 |
| 3 | `uninject` 传 `dlclose(0)` | 保存 `g_payload_handle`，卸载时用真实句柄 |
| 4 | 跳桩未解析 + 共享库地址未做 PIC 换算 | 新增 `resolve_thunk()` + `dlsym`/`dladdr` 解析 |
| 5 | 未调用 `XInitThreads()` | 在 `gui_init()` 最开头调用 |
| 6 | `is_root` 从不赋值 | 从 `/proc/<pid>/status` 的 `Uid:` 行解析 |
| 7 | `/proc/<pid>/net/*` 语义错误 | 删除（那是网络命名空间级的，不是进程级） |
| 8 | 16 个死函数 + 4 个无效模块 | 删除 `heartbeat.c` `screen.c` `input.c` `netfilter.c` |

### ⚠️ 仍未验证

**代码从未在真机（aarch64 + 真实极域）上运行过。** 上述修复均基于
Xlib/aarch64 ABI 的静态分析，只能保证编译通过和逻辑自洽。

首次真机运行建议：

```bash
./bin/jyfree -d 5
# 先看注入是否成功，再看 payload 日志
cat /tmp/jyfree-payload.log
# 逐步开启特性位，一次只开一个，观察 Student 是否崩溃
> feat lock on
> counters
```

---

<div align="center">

**[⬆ 主文档](../README.md)** · **[English main →](../README.en.md)**
**[文档索引](README.md)** · **[English index →](README.en.md)**

仅用于本地授权研究与教学场景。
极域 (Mythware) 为广州视睿软件科技有限公司注册商标，本项目与其无任何关联。

</div>