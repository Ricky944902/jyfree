#!/bin/bash
# 极域电子教室 Deb包提取工具 (TUI版)
# 从已安装极域的电脑上自动提取deb安装包
# 终端图形化界面，无需root权限

set -e

# ==================== 颜色定义 ====================
RED='\033[1;31m'
GREEN='\033[1;32m'
YELLOW='\033[1;33m'
BLUE='\033[1;34m'
MAGENTA='\033[1;35m'
CYAN='\033[1;36m'
WHITE='\033[1;37m'
GRAY='\033[0;37m'
DARK='\033[0;30m'
NC='\033[0m'

# 背景色
BG_BLUE='\033[44m'
BG_WHITE='\033[47m'
BG_CYAN='\033[46m'
BG_GREEN='\033[42m'
BG_RED='\033[41m'

# ==================== 全局变量 ====================
VERSION="1.0.0"
TITLE="极域电子教室 Deb包提取工具"
INSTALL_DIR=""
OUTPUT_DIR="./mythware-extracted"
EXTRACTED_COUNT=0
TOTAL_FILES=0

# ==================== 终端控制函数 ====================
clear_screen() {
    tput clear
}

hide_cursor() {
    tput civis
}

show_cursor() {
    tput cnorm
}

move_to() {
    tput cup $1 $2
}

draw_box() {
    local y=$1 x=$2 w=$3 h=$4 title="$5"
    
    # 绘制边框
    move_to $y $x
    echo -ne "${CYAN}╔"
    for ((i=0; i<w-2; i++)); do echo -n "═"; done
    echo -ne "╗${NC}"
    
    for ((i=1; i<h-1; i++)); do
        move_to $((y+i)) $x
        echo -ne "${CYAN}║${NC}"
        for ((j=0; j<w-2; j++)); do echo -n " "; done
        echo -ne "${CYAN}║${NC}"
    done
    
    move_to $((y+h-1)) $x
    echo -ne "${CYAN}╚"
    for ((i=0; i<w-2; i++)); do echo -n "═"; done
    echo -ne "╝${NC}"
    
    # 标题
    if [ -n "$title" ]; then
        local title_len=${#title}
        local title_x=$((x + (w - title_len) / 2))
        move_to $y $title_x
        echo -ne "${WHITE}${title}${NC}"
    fi
}

draw_button() {
    local y=$1 x=$2 text="$3" active=$4
    local len=${#text}
    local total=$((len + 4))
    
    move_to $y $x
    if [ "$active" = "1" ]; then
        echo -ne "${BG_CYAN}${WHITE} [ ${text} ] ${NC}"
    else
        echo -ne "${CYAN} [ ${text} ] ${NC}"
    fi
}

draw_progress() {
    local y=$1 x=$2 width=$3 percent=$4
    local filled=$((width * percent / 100))
    local empty=$((width - filled))
    
    move_to $y $x
    echo -ne "${CYAN}[${NC}"
    
    for ((i=0; i<filled; i++)); do
        echo -ne "${GREEN}█${NC}"
    done
    
    for ((i=0; i<empty; i++)); do
        echo -ne "${GRAY}░${NC}"
    done
    
    echo -ne "${CYAN}]${NC} ${WHITE}${percent}%%${NC}"
}

draw_status() {
    local y=$1 x=$2 text="$3" color="$4"
    move_to $y $x
    echo -ne "${color}${text}${NC}"
}

# ==================== 主界面 ====================
show_main_menu() {
    local choice=0
    local options=("搜索极域安装位置" "指定安装目录提取" "从deb包文件提取" "查看已提取文件" "退出")
    local option_count=${#options[@]}
    
    while true; do
        clear_screen
        hide_cursor
        
        # 绘制标题
        draw_box 2 5 70 5 "$TITLE"
        
        # 绘制版本信息
        move_to 4 60
        echo -ne "${GRAY}v${VERSION}${NC}"
        
        # 绘制主窗口
        draw_box 9 10 50 18 "主菜单"
        
        # 绘制选项
        for ((i=0; i<option_count; i++)); do
            local y=$((12 + i * 3))
            move_to $y 15
            
            if [ $i -eq $choice ]; then
                echo -ne "${BG_CYAN}${WHITE} ▶ ${options[$i]} ${NC}"
            else
                echo -ne "   ${options[$i]}"
            fi
        done
        
        # 绘制按钮区域
        draw_button 24 15 "确定" "1"
        draw_button 24 30 "退出" "0"
        
        # 绘制帮助信息
        move_to 28 10
        echo -ne "${GRAY}使用 ↑↓ 选择，Enter 确认，ESC 退出${NC}"
        
        # 绘制系统信息
        move_to 30 10
        echo -ne "${DARK}系统: $(uname -n) | 内核: $(uname -r)${NC}"
        
        # 获取输入
        local key=$(read_key)
        
        case "$key" in
            "up")
                choice=$(( (choice - 1 + option_count) % option_count ))
                ;;
            "down")
                choice=$(( (choice + 1) % option_count ))
                ;;
            "enter")
                case $choice in
                    0) search_mythware ;;
                    1) specify_directory ;;
                    2) extract_from_deb ;;
                    3) view_extracted ;;
                    4) exit_program ;;
                esac
                ;;
            "escape")
                exit_program
                ;;
        esac
    done
}

read_key() {
    local key
    read -rsn1 key
    
    case "$key" in
        $'\x1b')
            read -rsn1 -t 0.1 key2
            if [ "$key2" = "[" ]; then
                read -rsn1 -t 0.1 key3
                case "$key3" in
                    "A") echo "up" ;;
                    "B") echo "down" ;;
                    "C") echo "right" ;;
                    "D") echo "left" ;;
                    *) echo "escape" ;;
                esac
            else
                echo "escape"
            fi
            ;;
        "") echo "enter" ;;
        "q") echo "escape" ;;
        *) echo "$key" ;;
    esac
}

# ==================== 搜索极域 ====================
search_mythware() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "搜索极域安装位置"
    
    # 显示搜索进度
    draw_status 8 10 "正在搜索系统进程..." "$YELLOW"
    draw_progress 10 10 50 0
    sleep 0.5
    
    # 通过进程查找
    local pid=$(pgrep -f "studentmain\|StudentMain\|mythware" 2>/dev/null | head -1)
    
    if [ -n "$pid" ]; then
        draw_progress 10 10 50 30
        draw_status 12 10 "找到运行中的极域进程: PID=$pid" "$GREEN"
        
        local exe_path=$(readlink -f /proc/$pid/exe 2>/dev/null)
        if [ -n "$exe_path" ]; then
            INSTALL_DIR=$(dirname "$exe_path")
            draw_progress 10 10 50 100
            draw_status 14 10 "安装目录: $INSTALL_DIR" "$GREEN"
            
            sleep 1
            show_extract_menu
            return 0
        fi
    fi
    
    draw_progress 10 10 50 50
    draw_status 12 10 "未找到运行进程，搜索文件系统..." "$YELLOW"
    sleep 0.5
    
    # 搜索常见路径
    local search_paths=(
        "/opt/mythware" "/opt/Mythware" "/usr/local/mythware"
        "/usr/share/mythware" "/home/*/mythware"
    )
    
    for path in "${search_paths[@]}"; do
        for expanded in $path; do
            if [ -d "$expanded" ]; then
                INSTALL_DIR="$expanded"
                draw_progress 10 10 50 100
                draw_status 14 10 "找到安装目录: $INSTALL_DIR" "$GREEN"
                sleep 1
                show_extract_menu
                return 0
            fi
        done
    done
    
    # 使用find搜索
    draw_status 12 10 "深度搜索中，请稍候..." "$YELLOW"
    draw_progress 10 10 50 70
    
    local found=$(find /opt /usr/local /usr/share -name "studentmain" -o -name "StudentMain" 2>/dev/null | head -1)
    if [ -n "$found" ]; then
        INSTALL_DIR=$(dirname "$found")
        draw_progress 10 10 50 100
        draw_status 14 10 "找到安装目录: $INSTALL_DIR" "$GREEN"
        sleep 1
        show_extract_menu
        return 0
    fi
    
    draw_progress 10 10 50 100
    draw_status 14 10 "未找到极域安装目录" "$RED"
    draw_status 16 10 "请确保极域已安装，或使用'指定目录'选项" "$YELLOW"
    
    wait_for_key
}

# ==================== 指定目录 ====================
specify_directory() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "指定安装目录"
    
    move_to 8 10
    echo -ne "${WHITE}请输入极域安装目录路径:${NC}"
    
    move_to 10 10
    echo -ne "${CYAN}>>> ${NC}"
    show_cursor
    read -r INSTALL_DIR
    hide_cursor
    
    if [ ! -d "$INSTALL_DIR" ]; then
        draw_status 12 10 "目录不存在: $INSTALL_DIR" "$RED"
        wait_for_key
        return 1
    fi
    
    draw_status 12 10 "目录有效: $INSTALL_DIR" "$GREEN"
    sleep 0.5
    
    show_extract_menu
}

# ==================== 从deb包提取 ====================
extract_from_deb() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "从deb包文件提取"
    
    move_to 8 10
    echo -ne "${WHITE}请输入deb包文件路径:${NC}"
    
    move_to 10 10
    echo -ne "${CYAN}>>> ${NC}"
    show_cursor
    read -r DEB_FILE
    hide_cursor
    
    if [ ! -f "$DEB_FILE" ]; then
        draw_status 12 10 "文件不存在: $DEB_FILE" "$RED"
        wait_for_key
        return 1
    fi
    
    draw_status 12 10 "文件有效: $DEB_FILE" "$GREEN"
    sleep 0.5
    
    # 提取deb包内容
    mkdir -p "$OUTPUT_DIR"
    
    draw_status 14 10 "正在提取deb包..." "$YELLOW"
    draw_progress 16 10 50 0
    
    if command -v dpkg-deb &> /dev/null; then
        dpkg-deb -x "$DEB_FILE" "$OUTPUT_DIR/" 2>/dev/null
    else
        cd "$OUTPUT_DIR" 2>/dev/null
        ar x "$DEB_FILE" 2>/dev/null || true
        for f in data.tar.*; do
            if [ -f "$f" ]; then
                tar -xf "$f" 2>/dev/null || true
            fi
        done
        cd - > /dev/null
    fi
    
    draw_progress 16 10 50 100
    draw_status 18 10 "提取完成！" "$GREEN"
    
    wait_for_key
    show_extract_menu
}

# ==================== 提取菜单 ====================
show_extract_menu() {
    local choice=0
    local options=("提取所有文件" "仅提取主程序" "仅提取配置文件" "仅提取驱动文件" "返回主菜单")
    local option_count=${#options[@]}
    
    while true; do
        clear_screen
        hide_cursor
        
        draw_box 2 5 70 4 "文件提取 - $INSTALL_DIR"
        
        # 显示目录内容
        draw_box 9 10 50 12 "目录内容"
        
        local y=11
        for item in $(ls "$INSTALL_DIR" 2>/dev/null | head -10); do
            draw_status $y 12 "$item" "$WHITE"
            y=$((y + 1))
        done
        
        if [ $y -eq 11 ]; then
            draw_status 11 12 "(空目录或无法访问)" "$GRAY"
        fi
        
        # 绘制选项
        draw_box 9 65 40 12 "操作"
        
        for ((i=0; i<option_count; i++)); do
            local y=$((12 + i * 2))
            move_to $y 67
            
            if [ $i -eq $choice ]; then
                echo -ne "${BG_CYAN}${WHITE} ▶ ${options[$i]} ${NC}"
            else
                echo -ne "   ${options[$i]}"
            fi
        done
        
        # 帮助
        move_to 28 10
        echo -ne "${GRAY}↑↓ 选择 | Enter 确认 | ESC 返回${NC}"
        
        local key=$(read_key)
        
        case "$key" in
            "up")
                choice=$(( (choice - 1 + option_count) % option_count ))
                ;;
            "down")
                choice=$(( (choice + 1) % option_count ))
                ;;
            "enter")
                case $choice in
                    0) extract_all ;;
                    1) extract_main_only ;;
                    2) extract_config_only ;;
                    3) extract_driver_only ;;
                    4) return ;;
                esac
                ;;
            "escape")
                return
                ;;
        esac
    done
}

# ==================== 提取函数 ====================
extract_all() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "提取所有文件"
    
    mkdir -p "$OUTPUT_DIR"
    
    draw_status 8 10 "正在提取文件..." "$YELLOW"
    draw_progress 10 10 50 0
    
    local count=0
    local total=$(find "$INSTALL_DIR" -type f 2>/dev/null | wc -l)
    local current=0
    
    find "$INSTALL_DIR" -type f 2>/dev/null | while read f; do
        current=$((current + 1))
        local percent=$((current * 100 / total))
        draw_progress 10 10 50 $percent
        
        local basename=$(basename "$f")
        draw_status 12 10 "处理: $basename" "$WHITE"
        
        cp "$f" "$OUTPUT_DIR/" 2>/dev/null && count=$((count + 1))
    done
    
    draw_progress 10 10 50 100
    draw_status 14 10 "提取完成！共 $count 个文件" "$GREEN"
    draw_status 16 10 "输出目录: $OUTPUT_DIR" "$CYAN"
    
    EXTRACTED_COUNT=$count
    
    wait_for_key
}

extract_main_only() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "提取主程序"
    
    mkdir -p "$OUTPUT_DIR"
    
    draw_status 8 10 "正在搜索主程序..." "$YELLOW"
    
    local found=0
    for name in studentmain StudentMain mythware-student StudentMain.exe; do
        if [ -f "$INSTALL_DIR/$name" ]; then
            cp "$INSTALL_DIR/$name" "$OUTPUT_DIR/"
            draw_status 10 10 "已提取: $name" "$GREEN"
            found=$((found + 1))
        fi
    done
    
    # 搜索子目录
    find "$INSTALL_DIR" -name "studentmain" -o -name "StudentMain" 2>/dev/null | while read f; do
        cp "$f" "$OUTPUT_DIR/"
        draw_status 12 10 "已提取: $(basename $f)" "$GREEN"
        found=$((found + 1))
    done
    
    if [ $found -eq 0 ]; then
        draw_status 10 10 "未找到主程序文件" "$RED"
    fi
    
    wait_for_key
}

extract_config_only() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "提取配置文件"
    
    mkdir -p "$OUTPUT_DIR"
    
    draw_status 8 10 "正在搜索配置文件..." "$YELLOW"
    
    local count=0
    for ext in conf cfg ini xml json; do
        find "$INSTALL_DIR" -name "*.$ext" 2>/dev/null | while read f; do
            cp "$f" "$OUTPUT_DIR/"
            draw_status $((10 + count)) 10 "已提取: $(basename $f)" "$GREEN"
            count=$((count + 1))
        done
    done
    
    if [ $count -eq 0 ]; then
        draw_status 10 10 "未找到配置文件" "$RED"
    fi
    
    wait_for_key
}

extract_driver_only() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "提取驱动文件"
    
    mkdir -p "$OUTPUT_DIR"
    
    draw_status 8 10 "正在搜索驱动文件..." "$YELLOW"
    
    local count=0
    for ext in sys ko; do
        find "$INSTALL_DIR" -name "*.$ext" 2>/dev/null | while read f; do
            cp "$f" "$OUTPUT_DIR/"
            draw_status $((10 + count)) 10 "已提取: $(basename $f)" "$GREEN"
            count=$((count + 1))
        done
    done
    
    # 也提取动态库
    find "$INSTALL_DIR" -name "*.so" -o -name "*.so.*" 2>/dev/null | while read f; do
        cp "$f" "$OUTPUT_DIR/"
        draw_status $((10 + count)) 10 "已提取: $(basename $f)" "$GREEN"
        count=$((count + 1))
    done
    
    if [ $count -eq 0 ]; then
        draw_status 10 10 "未找到驱动文件" "$RED"
    fi
    
    wait_for_key
}

# ==================== 查看已提取文件 ====================
view_extracted() {
    clear_screen
    hide_cursor
    
    draw_box 2 5 70 4 "已提取文件"
    
    if [ ! -d "$OUTPUT_DIR" ]; then
        draw_status 8 10 "输出目录不存在: $OUTPUT_DIR" "$RED"
        draw_status 10 10 "请先执行文件提取" "$YELLOW"
        wait_for_key
        return
    fi
    
    draw_status 8 10 "目录: $OUTPUT_DIR" "$CYAN"
    
    local y=10
    local count=0
    
    for f in "$OUTPUT_DIR"/*; do
        if [ -f "$f" ]; then
            local name=$(basename "$f")
            local size=$(stat -c%s "$f" 2>/dev/null || echo "?")
            local type=$(file -b "$f" 2>/dev/null | head -c 30)
            
            if [ $y -ge 25 ]; then
                draw_status $y 10 "... 更多文件 ..." "$GRAY"
                break
            fi
            
            draw_status $y 10 "$name" "$WHITE"
            draw_status $y 40 "$size bytes" "$GRAY"
            draw_status $y 55 "$type" "$GRAY"
            
            y=$((y + 1))
            count=$((count + 1))
        fi
    done
    
    if [ $count -eq 0 ]; then
        draw_status 10 10 "(目录为空)" "$GRAY"
    fi
    
    draw_status 27 10 "共 $count 个文件" "$CYAN"
    
    wait_for_key
}

# ==================== 工具函数 ====================
wait_for_key() {
    move_to 29 10
    echo -ne "${YELLOW}按任意键继续...${NC}"
    read -rsn1
}

exit_program() {
    clear_screen
    show_cursor
    
    echo -ne "${GREEN}"
    echo "╔══════════════════════════════════════════╗"
    echo "║                                          ║"
    echo "║     感谢使用极域Deb提取工具！           ║"
    echo "║                                          ║"
    echo "╚══════════════════════════════════════════╝"
    echo -ne "${NC}"
    
    exit 0
}

# ==================== 命令行参数处理 ====================
parse_args() {
    while [ $# -gt 0 ]; do
        case "$1" in
            -d|--dir)
                INSTALL_DIR="$2"
                shift 2
                ;;
            -o|--output)
                OUTPUT_DIR="$2"
                shift 2
                ;;
            -h|--help)
                echo "用法: $0 [选项]"
                echo ""
                echo "选项:"
                echo "  -d, --dir DIR      指定极域安装目录"
                echo "  -o, --output DIR   指定输出目录"
                echo "  -h, --help         显示帮助"
                exit 0
                ;;
            *)
                if [ -d "$1" ]; then
                    INSTALL_DIR="$1"
                elif [ -f "$1" ]; then
                    DEB_FILE="$1"
                fi
                shift
                ;;
        esac
    done
}

# ==================== 主函数 ====================
main() {
    parse_args "$@"
    
    # 如果指定了目录，直接进入提取菜单
    if [ -n "$INSTALL_DIR" ] && [ -d "$INSTALL_DIR" ]; then
        show_extract_menu
        return
    fi
    
    # 如果指定了deb文件
    if [ -n "$DEB_FILE" ] && [ -f "$DEB_FILE" ]; then
        extract_from_deb
        return
    fi
    
    # 显示主菜单
    show_main_menu
}

# 运行主函数
main "$@"
