#!/bin/bash
# ============================================================
#  x11-sysroot.sh — 从 UOS 机器导出 aarch64 X11 开发文件
#
#  用途: 让你不用在 UOS 上安装任何东西, 就能在本机 (x86) 交叉编译
#        出 jyfree-gui 图形界面版。
#
#  原理: 图形界面版只需要两样东西 —
#          1. X11 头文件   (/usr/include/X11/**)
#          2. libX11.so    (链接时需要)
#        把它们从 UOS 机器打包拷回开发机, 即可离线交叉编译。
#
#  权限: 全程只读, 不需要 root / sudo
#
#  用法 (在 UOS 机器上):
#     chmod +x scripts/x11-sysroot.sh
#     ./scripts/x11-sysroot.sh
#
#  产物:
#     ~/jyfree-x11-sysroot.tar.gz
#
#  然后把 tar.gz 拷到开发机, 按提示操作。
# ============================================================

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

SCRIPT_NAME="x11-sysroot"
OUT_TARBALL="$HOME/jyfree-x11-sysroot.tar.gz"
STAGE="$HOME/jyfree-x11-sysroot"

echo -e "${CYAN}"
echo "========================================"
echo "  X11 开发文件打包工具"
echo "========================================"
echo -e "${NC}"
echo ""

# ---------- 1. 架构检查 ----------
echo -e "${CYAN}[1/6] 架构检查${NC}"
ARCH=$(uname -m)
echo "  当前架构: $ARCH"

if [ "$ARCH" != "aarch64" ]; then
    echo -e "${YELLOW}"
    echo "  注意: 这个脚本应该在运行极域的 aarch64 机器上执行。"
    echo "        当前是 $ARCH, 导出的头文件可能架构不匹配。"
    echo -e "${NC}"
fi

if [ "$ARCH" = "x86_64" ]; then
    echo -e "${GREEN}  x86_64 也可以导出 (用于编译 GUI 给 x86 测试)${NC}"
fi
echo ""

# ---------- 2. 查找 X11 头文件 ----------
echo -e "${CYAN}[2/6] 查找 X11 头文件${NC}"
X11_INC=""
for d in /usr/include/X11 /usr/local/include/X11; do
    if [ -d "$d" ] && [ -f "$d/Xlib.h" ]; then
        X11_INC=$(dirname "$d")
        echo -e "${GREEN}  找到: $d${NC}"
        break
    fi
done

if [ -z "$X11_INC" ]; then
    echo -e "${RED}  未找到 X11 头文件${NC}"
    echo ""
    echo "  说明: 这台机器没装 X11 开发文件。"
    echo "  图形界面版 (jyfree-gui) 需要它们。"
    echo ""
    echo "  两个选择:"
    echo "    1) 装上 (仅此步需要 sudo):"
    echo "       ${CYAN}sudo apt install libx11-dev${NC}"
    echo "    2) 不装 —— 直接用命令行版 jyfree, 功能完全一样"
    echo ""
    echo "  本工具不会自动执行 sudo。"
    exit 1
fi

# 检查关键头文件
MISSING=""
for h in Xlib.h Xutil.h Xatom.h keysym.h; do
    [ -f "$X11_INC/X11/$h" ] || MISSING="$MISSING $h"
done
if [ -n "$MISSING" ]; then
    echo -e "${YELLOW}  缺少头文件:$MISSING${NC}"
    echo -e "${YELLOW}  (jyfree 只用到 Xlib/Xutil/Xatom/keysym, 其余可忽略)${NC}"
fi
echo ""

# ---------- 3. 查找 libX11 ----------
echo -e "${CYAN}[3/6] 查找 libX11 库${NC}"
LIB_DIR=""
LIB_FILE=""
for d in /usr/lib/aarch64-linux-gnu /lib/aarch64-linux-gnu \
         /usr/lib /lib /usr/lib64 /usr/lib/x86_64-linux-gnu; do
    if [ -e "$d/libX11.so" ] || [ -e "$d/libX11.so.6" ]; then
        LIB_DIR="$d"
        if [ -e "$d/libX11.so" ]; then
            LIB_FILE="$d/libX11.so"
        else
            LIB_FILE="$d/libX11.so.6"
        fi
        break
    fi
done

if [ -z "$LIB_DIR" ]; then
    echo -e "${RED}  未找到 libX11${NC}"
    echo "  桌面系统必然自带 libX11.so.6, 找不到说明这不是完整桌面环境"
    exit 1
fi
echo -e "${GREEN}  找到: $LIB_FILE${NC}"
echo ""

# ---------- 4. 准备暂存目录 ----------
echo -e "${CYAN}[4/6] 打包内容${NC}"
rm -rf "$STAGE"
mkdir -p "$STAGE/include"
mkdir -p "$STAGE/lib"

# 拷贝头文件 (保留目录结构, 用相对路径)
cp -r "$X11_INC/X11" "$STAGE/include/"
echo "  头文件: $(find "$STAGE/include" -type f | wc -l) 个"

# 拷贝库文件 (优先 .so 链接, 没有就拿 .so.6)
if [ -e "$LIB_DIR/libX11.so" ]; then
    cp -L "$LIB_DIR/libX11.so" "$STAGE/lib/libX11.so"
else
    cp "$LIB_DIR/libX11.so.6" "$STAGE/lib/libX11.so"
fi
echo "  库文件: libX11.so ($(du -h "$STAGE/lib/libX11.so" | cut -f1))"

# libX11 依赖的库 (链接时可能需要)
DEP_LIBS=""
for l in libxcb libXau libXdmcp libbsd libmd; do
    for d in "$LIB_DIR" /usr/lib/aarch64-linux-gnu /lib/aarch64-linux-gnu /usr/lib; do
        if [ -e "$d/$l.so" ] || [ -e "$d/$l.so.6" ] || [ -e "$d/$l.so.0" ]; then
            src=$(ls "$d/$l.so"* 2>/dev/null | head -1)
            [ -n "$src" ] && cp -L "$src" "$STAGE/lib/" 2>/dev/null && DEP_LIBS="$DEP_LIBS $l"
            break
        fi
    done
done
[ -n "$DEP_LIBS" ] && echo "  依赖库:$DEP_LIBS"

# 写一个元信息文件
{
    echo "JYFREE_X11_SYSROOT"
    echo "arch=$ARCH"
    echo "kernel=$(uname -r)"
    echo "header_src=$X11_INC"
    echo "lib_src=$LIB_FILE"
    echo "created=$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    echo "libc=$(ldd --version 2>/dev/null | head -1)"
} > "$STAGE/META.txt"

echo ""

# ---------- 5. 生成 tarball ----------
echo -e "${CYAN}[5/6] 生成压缩包${NC}"
cd "$HOME"
tar czf "$OUT_TARBALL" "jyfree-x11-sysroot"
echo -e "${GREEN}  $OUT_TARBALL${NC}"
echo "  大小: $(du -h "$OUT_TARBALL" | cut -f1)"
echo ""

# ---------- 6. 校验和 ----------
echo -e "${CYAN}[6/6] 校验和${NC}"
SUM=$(sha256sum "$OUT_TARBALL" | awk '{print $1}')
echo "  sha256: $SUM"
echo "$SUM  $(basename "$OUT_TARBALL")" > "$OUT_TARBALL.sha256"
echo ""

# ---------- 完成 ----------
echo -e "${GREEN}"
echo "========================================"
echo "  完成"
echo "========================================"
echo -e "${NC}"
echo ""
echo "产物:"
echo "  $OUT_TARBALL"
echo "  $OUT_TARBALL.sha256"
echo ""
echo -e "${CYAN}下一步${NC}"
echo "  1. 把 tar.gz 传到开发机 (x86 那台):"
echo "     ${CYAN}scp <user>@<uos-host>:~/jyfree-x11-sysroot.tar.gz ~/${NC}"
echo ""
echo "  2. 在开发机解压并交叉编译 GUI:"
echo "     ${CYAN}tar xzf ~/jyfree-x11-sysroot.tar.gz${NC}"
echo "     ${CYAN}make gui ARCH=aarch64 CROSS=aarch64-linux-gnu- \\${NC}"
echo "         ${CYAN}X11_SYSROOT=\$HOME/jyfree-x11-sysroot${NC}"
echo ""
echo "  3. 完成后目标机直接用:"
echo "     ${CYAN}scp bin/jyfree-gui <user>@<uos-host>:~/${NC}"
echo ""
echo -e "${YELLOW}不需要这个也能用:${NC}"
echo "  命令行版 jyfree 功能完整, 不依赖图形界面。"