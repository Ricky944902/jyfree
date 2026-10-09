# ============================================================
#  jyfree - 极域电子教室绕过工具 (统信UOS / aarch64)
#  编译配置
#
#  常用命令:
#    make                 构建 jyfree + jyfree-gui + libjyfree.so
#    make cli             仅构建 jyfree (命令行版)
#    make gui             仅构建 jyfree-gui (图形界面版)
#    make payload         仅构建 libjyfree.so (注入用的 payload)
#    make debug           调试版本 (-O0 -g -DDEBUG)
#    make install         安装 (PREFIX 默认 /usr/local)
#    make clean           清理
#    make arch-check      检查架构是否匹配目标机
#
#  交叉编译 (在 x86 机器上构建 aarch64 版):
#    make ARCH=aarch64 CROSS=aarch64-linux-gnu-
# ============================================================

# ---------- 工具链 ----------
ARCH  ?= $(shell uname -m)
CROSS ?=

ifeq ($(ARCH),aarch64)
    CC := $(CROSS)gcc
else ifeq ($(ARCH),x86_64)
    CC := $(CROSS)gcc
else
    CC := gcc
endif

WARN := -Wall -Wextra
OPT  ?= -O2
INC  := -Iinclude

CFLAGS  := $(WARN) $(OPT) $(INC)
LDFLAGS :=

# 命令行版是否静态链接 (默认 1)
# 静态链接后二进制自带 libc, 在任何 aarch64 Linux 上都能跑,
# 不受目标系统 glibc 版本影响。代价是二进制约 800KB。
STATIC ?= 1

# glibc 兼容性:
#
# 若编译机的 glibc 比目标机新, 动态链接会产生两类问题:
#   1) 符号被打上高版本号 (__libc_start_main@GLIBC_2.34, dlopen@GLIBC_2.34,
#      pthread_create@GLIBC_2.34 等), 旧 glibc 找不到 → 直接启动失败
#   2) shm_open 只在 libc 里被解析, 老系统需要 librt.so.1
#
# 解决:
#   STATIC=1 (默认) 命令行版静态链接 → 完全消除 glibc 版本依赖,
#                                   在任何 aarch64 Linux 上都能跑
#   payload (.so) 无法静态链接, 用 OLD_SDL_COMPAT=1 时补上 -lrt -ldl -lpthread
#            让老系统的动态链接器能从 librt/librt/libdl 找到符号
#
# 目标系统 glibc 若 >= 2.34, 可用 make NEW_GLIBC=1 省掉这些兼容库

ifdef NEW_GLIBC
LIBS :=
else
LIBS := -lrt -ldl -lpthread
endif

# 静态链接时的额外标志 (只用 libc, 不需要 NSS/getaddrinfo 警告规避)
ifeq ($(STATIC),1)
CLI_LDFLAGS   := -static -static-libgcc
CLI_LIBS      :=
else
CLI_LDFLAGS   :=
CLI_LIBS      := $(LIBS)
endif

# payload 同样处理
PAYLOAD_CFLAGS := $(WARN) $(OPT) $(INC) -fPIC -DJIYU_PAYLOAD_BUILD=1

# X11 仅 GUI 版需要。用"真实链接测试"判断, 而不是 pkg-config:
# 交叉编译时宿主机可能有 x11.pc, 但交叉工具链未必有 aarch64 的 X11 库。
# payload (.so) 不能静态链接, 单独给它兼容库
PAYLOAD_LIBS := $(if $(NEW_GLIBC),,-lrt -ldl -lpthread)

HAVE_X11 := $(shell printf '#include <X11/Xlib.h>\nint main(void){return XOpenDisplay(0)!=0;}\n' \
             | $(CC) -x c - -lX11 -o /dev/null >/dev/null 2>&1 && echo 1 || echo 0)

# payload 专用: 位置无关 + 自己的日志实现 (不能依赖控制器符号)

# ---------- 目录 ----------
OBJDIR := obj
BINDIR := bin

# ---------- 源文件 ----------
CTRL_SRCS := src/process.c \
             src/utils.c \
             src/elf_sym.c \
             src/inject.c

CLI_SRCS    := src/main.c $(CTRL_SRCS)
GUI_SRCS    := src/gui_main.c src/gui.c $(CTRL_SRCS)
PAYLOAD_SRCS := src/hook_payload.c

# ---------- 目标 ----------
CLI_BIN    := $(BINDIR)/jyfree
GUI_BIN    := $(BINDIR)/jyfree-gui
PAYLOAD_SO := $(BINDIR)/libjyfree.so

CLI_OBJS    := $(patsubst src/%.c,$(OBJDIR)/%.o,$(CLI_SRCS))
GUI_OBJS    := $(patsubst src/%.c,$(OBJDIR)/%.o,$(GUI_SRCS))
PAYLOAD_OBJS := $(patsubst src/%.c,$(OBJDIR)/%.lo,$(PAYLOAD_SRCS))

# ---------- 规则 ----------
.PHONY: all cli gui payload debug install uninstall clean distclean arch-check help

all: cli payload $(if $(filter 1,$(HAVE_X11)),gui)

cli:     $(CLI_BIN)
payload: $(PAYLOAD_SO)

ifeq ($(HAVE_X11),1)
gui: $(GUI_BIN)
else
gui:
	@echo ""
	@echo "  [跳过] 未检测到 X11 开发库, 不构建图形界面版"
	@echo "         Debian/UOS: sudo apt install libx11-dev"
	@echo "         Fedora/RHEL: sudo dnf install libX11-devel"
	@echo ""
endif

# 链接: CLI
$(CLI_BIN): $(CLI_OBJS) | $(BINDIR)
	$(CC) $(CLI_LDFLAGS) -o $@ $^ $(CLI_LIBS)
	@echo "  [OK] $@  ($$($(CC) -dumpversion) $(ARCH))"

# 链接: GUI (需要 X11 开发库; 缺失时跳过而不是报错)
ifeq ($(HAVE_X11),1)
$(GUI_BIN): $(GUI_OBJS) | $(BINDIR)
	$(CC) $(CLI_LDFLAGS) -o $@ $^ $(CLI_LIBS) -lX11
	@echo "  [OK] $@"
else
$(GUI_BIN):
	@echo "  [跳过] 需要 X11 开发库 (libx11-dev)"
endif

# 链接: payload (注入到 Student 内的共享库)
$(PAYLOAD_SO): $(PAYLOAD_OBJS) | $(BINDIR)
	$(CC) $(LDFLAGS) -shared -o $@ $^ $(PAYLOAD_LIBS)
	@echo "  [OK] $@"

# 编译: 普通目标文件
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	@echo "  CC   $<"
	@$(CC) $(CFLAGS) -c $< -o $@

# 编译: payload 位置无关目标文件
$(OBJDIR)/%.lo: src/%.c | $(OBJDIR)
	@echo "  PIC  $<"
	@$(CC) $(PAYLOAD_CFLAGS) -c $< -o $@

$(OBJDIR) $(BINDIR):
	@mkdir -p $@

# ---------- 调试版本 ----------
debug:
	@$(MAKE) --no-print-directory OPT="-O0 -g3 -DDEBUG" clean
	@$(MAKE) --no-print-directory OPT="-O0 -g3 -DDEBUG" all
	@echo ""
	@echo "调试版本已构建到 $(BINDIR)/"

# ---------- 安装 ----------
PREFIX  ?= /usr/local
BINDEST := $(PREFIX)/bin

install: all
	@mkdir -p $(DESTDIR)$(BINDEST)
	install -m 755 $(CLI_BIN)    $(DESTDIR)$(BINDEST)/
	install -m 755 $(PAYLOAD_SO) $(DESTDIR)$(BINDEST)/
	@if [ -f $(GUI_BIN) ]; then \
	    install -m 755 $(GUI_BIN) $(DESTDIR)$(BINDEST)/; \
	fi
	@echo ""
	@echo "已安装到 $(DESTDIR)$(BINDEST):"
	@echo "  jyfree         命令行版 (仅依赖 libc)"
	@echo "  libjyfree.so   payload (必须与 jyfree 同目录)"
	@[ -f $(GUI_BIN) ] && echo "  jyfree-gui     图形界面版 (需要 X11)" || \
	    echo "  (未安装 jyfree-gui: 本机没有 X11 开发库)"

uninstall:
	rm -f $(DESTDIR)$(BINDEST)/jyfree \
	      $(DESTDIR)$(BINDEST)/jyfree-gui \
	      $(DESTDIR)$(BINDEST)/libjyfree.so

# ---------- 清理 ----------
clean:
	rm -rf $(OBJDIR)
	rm -f src/*.o src/*.lo src/*.pic.o
	rm -f jyfree jyfree-gui libjyfree.so

distclean: clean
	rm -rf $(BINDIR)

# ---------- 辅助 ----------
arch-check:
	@echo "主机架构:  $$(uname -m)"
	@echo "目标架构:  $(ARCH)"
	@echo "编译器:    $(CC)"
	@$(CC) --version 2>/dev/null | head -1
	@echo ""
	@if [ "$(ARCH)" = "aarch64" ]; then \
	    echo "[OK] 架构匹配, payload 中的硬编码符号地址有效"; \
	else \
	    echo "[!!] 架构不匹配"; \
	    echo "     payload 的内联钩子地址 (如 ShowLockScreen @ 0x441ed4)"; \
	    echo "     是 aarch64 专用, 在 $(ARCH) 上会失效或崩溃。"; \
	    echo ""; \
	    echo "     交叉编译:"; \
	    echo "       make ARCH=aarch64 CROSS=aarch64-linux-gnu-"; \
	fi

help:
	@echo "jyfree - 极域电子教室绕过工具 (统信UOS / aarch64)"
	@echo ""
	@echo "构建:"
	@echo "  make              构建全部 (jyfree + jyfree-gui + libjyfree.so)"
	@echo "  make cli          仅命令行版"
	@echo "  make gui          仅图形界面版"
	@echo "  make payload      仅 payload"
	@echo "  make debug        调试版 (-O0 -g3 -DDEBUG)"
	@echo ""
	@echo "安装:"
	@echo "  make install                    安装到 /usr/local"
	@echo "  make install PREFIX=~/.local    安装到用户目录"
	@echo ""
	@echo "清理:"
	@echo "  make clean         删除中间文件"
	@echo "  make distclean     连同 bin/ 一并删除"
	@echo ""
	@echo "辅助:"
	@echo "  make arch-check    检查架构匹配"
	@echo ""
	@echo "交叉编译 (在 x86 上构建 aarch64 版):"
	@echo "  make ARCH=aarch64 CROSS=aarch64-linux-gnu-"
	@echo ""
	@echo "兼容性开关:"
	@echo "  STATIC=0      命令行版改为动态链接 (默认 1, 静态更通用)"
	@echo "  NEW_GLIBC=1   目标 glibc >= 2.34 时可省掉兼容库与 .symver 绑定"
	@echo ""
	@echo "文档:"
	@echo "  docs/01-逆向分析.md    原理与钩子点"
	@echo "  docs/02-编译指南.md    编译与交叉编译"
	@echo "  docs/03-使用手册.md    全部命令说明"
	@echo "  docs/04-排错手册.md    故障排查"