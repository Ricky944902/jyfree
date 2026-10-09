#!/bin/bash
# ============================================================
#  jyfree 一键编译脚本
#
#  用法:
#    ./scripts/build.sh                    # 本机编译
#    ./scripts/build.sh --cross            # 交叉编译 aarch64
#    ./scripts/build.sh --cross <前缀>      # 指定交叉工具链前缀
#    ./scripts/build.sh --debug            # 调试版
#    ./scripts/build.sh --install          # 编译并安装
#    ./scripts/build.sh --clean            # 清理
#    ./scripts/build.sh --check            # 仅检查环境
# ============================================================

set -e

cd "$(dirname "$0")/.."

CYAN='\033[0;36m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m'

MODE="native"
CROSS_PREFIX=""
OPT_EXTRA=""

print_header() {
    echo -e "${CYAN}"
    echo "========================================"
    echo "  jyfree 编译工具"
    echo "========================================"
    echo -e "${NC}"
}

check_deps() {
    echo -e "${CYAN}[检查依赖]${NC}"
    
    local missing=0
    
    if ! command -v make >/dev/null 2>&1; then
        echo -e "  ${RED}x${NC} make 未安装"
        missing=1
    else
        echo -e "  ${GREEN}ok${NC} make"
    fi
    
    if ! command -v gcc >/dev/null 2>&1; then
        # 可能是交叉编译场景
        if [ -n "$CROSS_PREFIX" ] && command -v "${CROSS_PREFIX}gcc" >/dev/null 2>&1; then
            echo -e "  ${GREEN}ok${NC} ${CROSS_PREFIX}gcc"
        else
            echo -e "  ${RED}x${NC} gcc 未安装"
            missing=1
        fi
    else
        echo -e "  ${GREEN}ok${NC} gcc ($(gcc -dumpversion))"
    fi
    
    # X11 头文件 (只有 GUI 需要)
    if [ -f /usr/include/X11/Xlib.h ] || [ -f /usr/include/x86_64-linux-gnu/X11/Xlib.h ] \
       || [ -f /usr/include/aarch64-linux-gnu/X11/Xlib.h ]; then
        echo -e "  ${GREEN}ok${NC} X11 头文件"
    else
        echo -e "  ${YELLOW}!!${NC} X11 头文件缺失 (GUI 版将无法编译)"
        echo -e "     Debian/UOS: ${CYAN}sudo apt install libx11-dev${NC}"
    fi
    
    # payload 需要的
    echo -e "  ${GREEN}ok${NC} pthread / dl / rt"
    
    if [ "$missing" = "1" ]; then
        echo -e ""
        echo -e "${RED}依赖缺失, 请先安装:${NC}"
        echo -e "  Debian/UOS: ${CYAN}sudo apt install build-essential libx11-dev${NC}"
        exit 1
    fi
}

check_arch() {
    echo -e "${CYAN}[架构检查]${NC}"
    echo "  主机:   $(uname -m)"
    echo "  目标:   $(uname -m)"
    
    if [ "$(uname -m)" = "aarch64" ]; then
        echo -e "  ${GREEN}ok${NC} aarch64, payload 硬编码符号地址有效"
    else
        echo -e "  ${YELLOW}!!${NC} 非 aarch64"
        echo -e "     payload 内联钩子地址 (0x441ed4 等) 不适用"
        echo -e "     需要在 aarch64 机器上编译, 或使用交叉编译"
    fi
}

build_native() {
    echo -e "${CYAN}[编译]${NC} 本机"
    make OPT="$OPT_EXTRA" all
}

build_cross() {
    local prefix="${CROSS_PREFIX:-aarch64-linux-gnu-}"
    
    if ! command -v "${prefix}gcc" >/dev/null 2>&1; then
        echo -e "${RED}交叉工具链不存在: ${prefix}gcc${NC}"
        echo ""
        echo -e "安装方式 (Debian/UOS):"
        echo -e "  ${CYAN}sudo apt install gcc-aarch64-linux-gnu${NC}"
        exit 1
    fi
    
    echo -e "${CYAN}[编译]${NC} 交叉编译 -> ${prefix}gcc"
    make ARCH=aarch64 CROSS="$prefix" OPT="$OPT_EXTRA" all
    
    echo ""
    echo -e "${GREEN}交叉编译完成${NC}"
    echo "产物在 bin/, 用 scp 传到目标 aarch64 机器:"
    echo -e "  ${CYAN}scp bin/* user@target:/usr/local/bin/${NC}"
}

# ==================== 参数解析 ====================
case "$1" in
    --cross)
        MODE="cross"
        CROSS_PREFIX="${2:-}"
        ;;
    --cross-*)
        MODE="cross"
        CROSS_PREFIX="${1#--cross-}"
        ;;
    --debug)
        MODE="debug"
        ;;
    --install)
        MODE="install"
        ;;
    --clean)
        MODE="clean"
        ;;
    --check)
        MODE="check"
        ;;
    -h|--help)
        MODE="help"
        ;;
    *)
        MODE="native"
        if [ -n "$1" ]; then
            echo -e "${RED}未知参数: $1${NC}"
            echo ""
            ./scripts/build.sh --help
            exit 1
        fi
        ;;
esac

print_header

case "$MODE" in
    help)
        # 打印文件头部的注释块 (到第一个非 # 行为止)
        awk 'NR>1 { if ($0 !~ /^#/) exit; sub(/^# ?/, ""); print }' "$0"
        ;;
    
    check)
        check_deps
        echo ""
        check_arch
        ;;
    
    clean)
        echo -e "${CYAN}[清理]${NC}"
        make distclean
        echo -e "${GREEN}已清理${NC}"
        ;;
    
    debug)
        check_deps
        check_arch
        echo ""
        echo -e "${CYAN}[编译]${NC} 调试版 (-O0 -g3 -DDEBUG)"
        make debug
        ;;
    
    install)
        check_deps
        check_arch
        echo ""
        echo -e "${CYAN}[编译并安装]${NC}"
        make install
        echo ""
        echo -e "${GREEN}安装完成, 运行: ${NC}jyfree --help"
        ;;
    
    cross)
        check_deps
        check_arch
        echo ""
        build_cross
        ;;
    
    native)
        check_deps
        check_arch
        echo ""
        build_native
        echo ""
        echo -e "${GREEN}编译完成${NC}"
        echo ""
        echo "产物:"
        ls -lh bin/ 2>/dev/null | tail -n +2 | sed 's/^/  /'
        echo ""
        echo -e "${YELLOW}下一步${NC}"
        echo "  ./bin/jyfree --help      查看用法"
        echo "  ./bin/jyfree --status   查看注入状态 (需已启动极域)"
        echo "  sudo make install       安装到 /usr/local"
        ;;
esac