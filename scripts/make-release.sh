# ============================================================
#  scripts/make-release.sh — 组装 GitHub Release 资产
#
#  产物:
#     release/jyfree-2.0.0-aarch64.tar.gz
#     release/jyfree-2.0.0-aarch64.tar.gz.sha256
#
#  用法:
#     ./scripts/make-release.sh                          # 编译 aarch64 并打包
#     ./scripts/make-release.sh --no-build               # 用现有产物打包
#     ./scripts/make-release.sh --arch x86_64            # 换架构
# ============================================================

set -e

cd "$(dirname "$0")/.."

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

VERSION=$(grep -oP '#define JIYU_VERSION_STRING "\K[^"]+' include/jiyu.h)
ARCH="aarch64"
CROSS="aarch64-linux-gnu-"
BUILD=1

while [ $# -gt 0 ]; do
    case "$1" in
        --no-build) BUILD=0; shift ;;
        --arch)     ARCH="$2"; shift 2 ;;
        --cross)    CROSS="$2"; shift 2 ;;
        *) echo "未知参数: $1"; exit 1 ;;
    esac
done

if [ "$ARCH" != "aarch64" ]; then
    CROSS=""
fi

ROOT="$(pwd)"
REL="jyfree-${VERSION}-${ARCH}"
STAGE="$ROOT/release/stage"
TARBALL="$ROOT/release/${REL}.tar.gz"

echo -e "${CYAN}"
echo "========================================"
echo "  jyfree ${VERSION} Release 打包"
echo "  架构: ${ARCH}"
echo "========================================"
echo -e "${NC}"

# ---------- 编译 ----------
if [ "$BUILD" = "1" ]; then
    echo -e "${CYAN}[1/4] 编译${NC}"
    make distclean >/dev/null 2>&1 || true
    if [ "$ARCH" = "aarch64" ] && [ "$CROSS" != "jiyu-none" ]; then
        make ARCH="$ARCH" CROSS="$CROSS" cli payload
    else
        make ARCH="$ARCH" cli payload
    fi
else
    echo -e "${CYAN}[1/4] 跳过编译 (使用现有产物)${NC}"
fi
echo ""

# ---------- 检查产物 ----------
echo -e "${CYAN}[2/4] 检查产物${NC}"
MISSING=""
[ -f bin/jyfree ]         || MISSING="$MISSING bin/jyfree"
[ -f bin/libjyfree.so ]   || MISSING="$MISSING bin/libjyfree.so"
[ -f docs/使用指南.md ]     || MISSING="$MISSING docs/使用指南.md"

if [ -n "$MISSING" ]; then
    echo -e "${RED}  缺少文件:$MISSING${NC}"
    exit 1
fi

echo -e "${GREEN}  jyfree          $(du -h bin/jyfree | cut -f1)${NC}"
echo -e "${GREEN}  libjyfree.so    $(du -h bin/libjyfree.so | cut -f1)${NC}"
if [ -f bin/jyfree-gui ]; then
    echo -e "${GREEN}  jyfree-gui      $(du -h bin/jyfree-gui | cut -f1)  (图形界面版)${NC}"
else
    echo -e "${YELLOW}  jyfree-gui      未构建 (需要 X11 开发文件)${NC}"
fi
echo ""

# ---------- 组装 ----------
echo -e "${CYAN}[3/4] 组装发布包${NC}"
rm -rf "$STAGE"
mkdir -p "$STAGE"

cp bin/jyfree           "$STAGE/"
cp bin/libjyfree.so     "$STAGE/"
[ -f bin/jyfree-gui ] && cp bin/jyfree-gui "$STAGE/" || true

# 文档
cp docs/使用指南.md      "$STAGE/"
cp README.md            "$STAGE/README.md"
cp README.en.md         "$STAGE/README.en.md"
cp LICENSE              "$STAGE/"
cp CHANGELOG.md         "$STAGE/" 2>/dev/null || true

# 工具脚本 (便于目标机上取用)
mkdir -p "$STAGE/scripts"
cp scripts/x11-sysroot.sh   "$STAGE/scripts/" 2>/dev/null || true
cp scripts/extract-mythware.sh "$STAGE/scripts/" 2>/dev/null || true
cp scripts/build.sh        "$STAGE/scripts/" 2>/dev/null || true

# 源码 (便于审计)
mkdir -p "$STAGE/src" "$STAGE/include"
cp src/*.c     "$STAGE/src/"     2>/dev/null || true
cp include/*.h "$STAGE/include/" 2>/dev/null || true
cp Makefile    "$STAGE/"         2>/dev/null || true

# 放一个醒目的提醒文件
cat > "$STAGE/请先读我.txt" << 'EOF'
============================================================
  jyfree - 使用前必读
============================================================

本工具从未在真实的 aarch64 + 极域环境中运行过。

它通过了编译, 逻辑自洽, 但以下环节完全未验证:

  [ ] ptrace 注入能否在真实 Student 上成功
  [ ] 符号地址是否匹配 Student v2.7.3715 (换版本必失效)
  [ ] 钩子安装后 Student 是否会崩溃
  [ ] 各特性位拦截是否真的生效
  [ ] Wayland/XWayland 下 X11 钩子的实际效果

请把它当作 ptrace/hook 技术学习材料, 而非可直接使用的成品。

快速开始:
  1. chmod +x jyfree libjyfree.so
     (两个文件必须在同一目录)
  2. ./jyfree --process        先确认能找到极域进程
  3. ./jyfree --modules        看已加载哪些极域库
  4. ./jyfree -d 5             注入, 观察是否成功
  5. 失败就停, 查 使用指南.md 第八章 排错

权限: 不需要 root, 但必须与极域 Student 同 UID 运行。

仅用于本地授权研究与学习原理。
极域 (Mythware) 为广州视睿软件科技有限公司注册商标。
============================================================
EOF

echo -e "${GREEN}  包含:${NC}"
find "$STAGE" -type f | sed "s|$STAGE/|    |" | sort
echo ""

# ---------- 打包 ----------
echo -e "${CYAN}[4/4] 打包 + 校验和${NC}"
# 让压缩包内目录叫 jyfree-2.0.0-aarch64 而不是 stage
mv "$STAGE" "$ROOT/release/${REL}"
tar czf "$TARBALL" -C "$ROOT/release" "$REL"

SUM=$(sha256sum "$TARBALL" | awk '{print $1}')
echo "$SUM  $(basename "$TARBALL")" > "$TARBALL.sha256"

SIZE=$(du -h "$TARBALL" | cut -f1)
echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  发布包已生成${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "  文件:   $TARBALL  ($SIZE)"
echo "  校验和: $SUM"
echo ""
echo -e "${CYAN}下一步 (需要你执行, 我无法访问 GitHub)${NC}"
echo "  1. 上传资产:"
echo "     ${CYAN}gh release create v${VERSION} \\${NC}"
echo "       ${CYAN}  release/$(basename "$TARBALL") \\${NC}"
echo "       ${CYAN}  release/$(basename "$TARBALL").sha256 \\${NC}"
echo "       ${CYAN}  --title \"jyfree ${VERSION}\" \\${NC}"
echo "       ${CYAN}  --notes-file release/RELEASE_NOTES.md${NC}"
echo ""
echo "  2. 或者到 GitHub 网页:"
echo "     Releases → Create a new release → 拖入上述文件"
echo ""
echo -e "${YELLOW}发布时请保留 README 首屏的「未经真机验证」警告。${NC}"