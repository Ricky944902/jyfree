#!/bin/bash
# jyfree 安装脚本
#
# 用法:
#   ./install.sh              用户级安装 (无需 root, 安装到 ~/.local)
#   sudo ./install.sh         系统级安装 (安装到 /usr/local)
#   sudo ./install.sh --arch-check   检查架构匹配

set -e

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

PREFIX=""
SYSTEM=0

if [ "$EUID" -eq 0 ]; then
    SYSTEM=1
    PREFIX="/usr/local"
else
    PREFIX="$HOME/.local"
fi

# 架构检查
if [ "$1" = "--arch-check" ]; then
    echo "主机架构:   $(uname -m)"
    echo "内核版本:   $(uname -r)"
    echo ""
    if [ "$(uname -m)" = "aarch64" ]; then
        echo -e "${GREEN}架构匹配: 硬编码符号地址有效${NC}"
    else
        echo -e "${YELLOW}架构不匹配${NC}"
        echo "payload 中的内联钩子地址 (如 ShowLockScreen @ 0x441ed4)"
        echo "是 aarch64 专用。在非 aarch64 上会失效或崩溃。"
        echo ""
        echo "跨平台交叉编译:"
        echo "  make ARCH=aarch64 CROSS=aarch64-linux-gnu-"
    fi
    exit 0
fi

echo -e "${CYAN}jyfree - 极域电子教室绕过工具 v2.0.0${NC}"
echo ""

# ==================== 检查 ====================
echo -e "${CYAN}[1/5] 检查架构${NC}"
if [ "$(uname -m)" != "aarch64" ]; then
    echo -e "${YELLOW}  警告: 当前架构 $(uname -m) 不是 aarch64${NC}"
    echo -e "${YELLOW}  内联钩子地址将不匹配, 建议交叉编译${NC}"
fi

echo -e "${CYAN}[2/5] 检查依赖${NC}"
MISSING=""
for lib in libX11 libpthread libdl librt; do
    :
done

# 检查极域安装 (可选)
if [ -d "/opt/mythware/classroom-management" ]; then
    echo -e "${GREEN}  发现极域安装${NC}"
    if [ -f "/opt/mythware/classroom-management/Student" ]; then
        echo -e "${GREEN}    Student 主程序存在${NC}"
    fi
else
    echo -e "${YELLOW}  未发现极域安装 (不影响本工具编译)${NC}"
fi

# 检查 X11
if command -v pkg-config >/dev/null 2>&1; then
    if pkg-config --exists x11 2>/dev/null; then
        echo -e "${GREEN}  X11 开发库就绪${NC}"
    else
        MISSING="$MISSING libx11-dev"
    fi
fi

# ==================== 编译 ====================
echo -e "${CYAN}[3/5] 编译${NC}"
if command -v make >/dev/null 2>&1; then
    make clean >/dev/null 2>&1 || true
    make
    make gui || {
        echo -e "${YELLOW}  GUI 构建失败 (可能缺少 X11 头文件)${NC}"
    }
else
    echo -e "${RED}  未找到 make, 请手动编译${NC}"
    exit 1
fi

# ==================== 安装 ====================
echo -e "${CYAN}[4/5] 安装到 $PREFIX${NC}"
mkdir -p "$PREFIX/bin"
mkdir -p "$PREFIX/lib"

install -m 755 jyfree "$PREFIX/bin/"
echo -e "${GREEN}  + $PREFIX/bin/jyfree${NC}"

# payload 必须与 jyfree 同目录 (payload_path() 优先在 exe 同目录找)
if [ -f libjyfree.so ]; then
    install -m 755 libjyfree.so "$PREFIX/bin/"
    echo -e "${GREEN}  + $PREFIX/bin/libjyfree.so${NC}"
fi

if [ -f jyfree-gui ]; then
    install -m 755 jyfree-gui "$PREFIX/bin/"
    echo -e "${GREEN}  + $PREFIX/bin/jyfree-gui${NC}"
fi

# ==================== 桌面集成 ====================
echo -e "${CYAN}[5/5] 桌面与 systemd 集成${NC}"
if [ "$SYSTEM" = "1" ]; then
    DESKTOP_DIR="/usr/share/applications"
    SYSTEMD_DIR="/etc/systemd/user"
else
    DESKTOP_DIR="$HOME/.local/share/applications"
    SYSTEMD_DIR="$HOME/.config/systemd/user"
fi

mkdir -p "$DESKTOP_DIR" "$SYSTEMD_DIR"

if [ -f deploy/jyfree.desktop ]; then
    sed "s|/usr/local/bin|$PREFIX/bin|g" deploy/jyfree.desktop > "$DESKTOP_DIR/jyfree.desktop"
    chmod +x "$DESKTOP_DIR/jyfree.desktop"
    echo -e "${GREEN}  + $DESKTOP_DIR/jyfree.desktop${NC}"
fi

if [ -f deploy/jyfree.service ]; then
    sed "s|/usr/local/bin|$PREFIX/bin|g" deploy/jyfree.service > "$SYSTEMD_DIR/jyfree.service"
    echo -e "${GREEN}  + $SYSTEMD_DIR/jyfree.service${NC}"
    echo ""
    echo -e "${YELLOW}  启用开机自启:${NC}"
    echo "    systemctl --user daemon-reload"
    echo "    systemctl --user enable jyfree.service"
fi

echo ""
echo -e "${GREEN}========================================${NC}"
echo -e "${GREEN}  安装完成${NC}"
echo -e "${GREEN}========================================${NC}"
echo ""
echo "运行:"
echo "  $PREFIX/bin/jyfree            # CLI, 注入 + 交互"
echo "  $PREFIX/bin/jyfree --status   # 查看注入状态"
echo "  $PREFIX/bin/jyfree --modules  # 查看已加载的极域库"
echo "  $PREFIX/bin/jyfree-gui        # 图形界面"
echo ""
echo -e "${YELLOW}重要:${NC}"
echo "  必须与 Student 同 UID 运行 (ptrace 注入要求同 UID, 无需 root)"
echo "  若 Student 已在运行, 直接执行 jyfree 即可注入"
echo ""
echo "排错:"
echo "  cat /tmp/jyfree-payload.log    # payload 内部日志"
echo "  jyfree --debug 5               # 控制器详细日志"