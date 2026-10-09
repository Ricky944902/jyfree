<div align="center">

# 📖 文档索引

**jyfree** — 极域电子教室绕过工具（统信UOS / aarch64）

**[简体中文](README.md)** · **[English](README.en.md)**

</div>

---

## 文档清单

| # | 文档 | 内容 | 状态 |
|---|---|---|---|
| 01 | [逆向分析](01-逆向分析.md) · [EN](01-reverse-engineering.en.md) | 环境、进程模型、协议、**全部钩子点与符号地址**、注入方案、待验证项 | ✅ |
| 02 | [编译指南](02-编译指南.md) · [EN](02-build-guide.en.md) | 依赖、编译、交叉编译、架构要求、安装、编译问题 | ✅ |
| 03 | [使用手册](03-使用手册.md) · [EN](03-user-guide.en.md) | 参数、交互命令、特性位、GUI、systemd、典型工作流 | ✅ |
| 04 | [排错手册](04-排错手册.md) · [EN](04-troubleshooting.en.md) | 故障现象 → 原因 → 处理、收集完整信息 | ✅ |
| 05 | [GitHub 发布指南](05-GitHub发布指南.md) · [EN](05-github-upload.en.md) | **哪些文件该传/不该传**、完整发布步骤、法律风险 | ✅ |


---

## 项目文件

| 路径 | 说明 |
|---|---|
| [`../README.md`](../README.md) | 主文档（中文） |
| [`../README.en.md`](../README.en.md) | Main documentation (English) |
| [`../Makefile`](../Makefile) | 编译配置 |
| [`../LICENSE`](../LICENSE) | MIT |
| [`../CONTRIBUTING.md`](../CONTRIBUTING.md) | 贡献指南 |
| [`../CHANGELOG.md`](../CHANGELOG.md) | 更新日志 |
| [`../include/payload_state.h`](../include/payload_state.h) | ★ 特性位 / 符号地址 / 共享内存结构体 |
| [`../scripts/build.sh`](../scripts/build.sh) | 一键编译 |

---

## 快速入口

```bash
make                        # 编译
./bin/jyfree                # 注入 + 交互
./bin/jyfree --help         # 命令行参数
./bin/jyfree -d 5           # 最详细日志
cat /tmp/jyfree-payload.log # payload 内部日志
```

---

## 按需查阅

| 我想… | 看 |
|---|---|
| 弄清原理、符号地址从哪来 | [01 逆向分析](01-逆向分析.md) §6、§7、§9 |
| 交叉编译到 aarch64 | [02 编译指南](02-编译指南.md) §4 |
| 知道为什么必须同 UID | [01 逆向分析](01-逆向分析.md) §8 |
| 查某个命令怎么用 | [03 使用手册](03-使用手册.md) §4 |
| 配置 systemd 开机自启 | [03 使用手册](03-使用手册.md) §8 |
| 注入失败怎么办 | [04 排错手册](04-排错手册.md) §2、§3 |
| 为什么截屏计数是 0 | [04 排错手册](04-排错手册.md) §6 |
| 换极域版本怎么办 | [04 排错手册](04-排错手册.md) §3.3 |
| **上传 GitHub 前必读** | [05 GitHub 发布指南](05-GitHub发布指南.md) |
| 哪些文件不能传 | [05 GitHub 发布指南](05-GitHub发布指南.md) §4 |


---

## 语言约定

- **代码注释、运行时输出、GUI 文本**：中文为主（目标用户是国内校园场景）
- **文档**：中英双语，中文为默认
- 提交 PR 时请同步更新两种语言的对应文档

---

<div align="center">

仅用于本地授权研究与教学场景。
极域 (Mythware) 为广州视睿软件科技有限公司注册商标，本项目与其无任何关联。

**[⬆ 返回主文档](../README.md)** · **[English index →](README.en.md)**

</div>
