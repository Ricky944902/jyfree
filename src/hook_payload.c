// ============================================================
// libjyfree.so - 注入到 Student 进程的 payload
//
// 职责:
//   1. 挂载 /dev/shm/jyfree.state 共享内存, 读取特性位
//   2. 对 CStudentMainWork 的锁定/监视/命令/策略方法打内联钩子
//   3. 对 libGetAppsInfo.so.2 的应用策略函数打内联钩子
//   4. 对所有已加载 ELF 的 X11 GOT 条目打钩子 (输入/截屏)
//   5. 周期重扫, 处理运行期 dlopen 的 libcast-x11.so
//
// 返回值约定: 与厂商空桩一致, w0 = 0 (表示"已处理/无操作")
// ============================================================

#define _GNU_SOURCE 1
#define JIYU_PAYLOAD_BUILD 1
#include "payload_state.h"

// 把 dl/pthread/shm 符号绑定到 GLIBC_2.2.5, 使 payload 能在
// 旧 glibc (如 UOS V20 的 2.31) 上加载, 而非只支持 2.34+
#include "glibc_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <link.h>
#include <elf.h>
#include <dlfcn.h>
#include <sys/time.h>

// ==================== 内部状态 ====================
static JySharedState *g_state = NULL;
static int  g_shm_fd = -1;
static void *g_shm_ptr = NULL;
static pthread_t g_thread;
static volatile int g_running = 0;

// 原始指令备份 (每钩子 16 字节)
#define MAX_INLINE_HOOKS 32
static uint8_t  g_orig_code[MAX_INLINE_HOOKS][16];
static uintptr_t g_hook_targets[MAX_INLINE_HOOKS];
static int       g_hook_count_actual = 0;

// X11 原函数指针
static void *g_orig_XGetImage = NULL;
static void *g_orig_XShmGetImage = NULL;
static void *g_orig_XGrabKeyboard = NULL;
static void *g_orig_XGrabPointer = NULL;
static void *g_orig_XTestFakeKeyEvent = NULL;
static void *g_orig_XTestFakeButtonEvent = NULL;
static void *g_orig_XTestFakeMotionEvent = NULL;
static void *g_orig_XWarpPointer = NULL;
static void *g_orig_XkbLockModifiers = NULL;
static void *g_orig_XSendEvent = NULL;
static void *g_orig_XTestFakeRelativeMotionEvent = NULL;

// 记录上次见到的 libcast-x11 handle, 用于检测新加载/卸载
static void *g_last_x11_handle = NULL;

// 冻结帧缓存 (用于 JY_CAPTURE_FREEZE)
static void *g_last_frame = NULL;
static size_t g_last_frame_size = 0;
static uint32_t g_last_frame_w = 0;
static uint32_t g_last_frame_h = 0;
static pthread_mutex_t g_frame_lock = PTHREAD_MUTEX_INITIALIZER;

// ==================== payload 日志 ====================
static FILE *g_log = NULL;

static void jyf_log_open(void) {
    if (g_log) return;
    const char *path = "/tmp/jyfree-payload.log";
    g_log = fopen(path, "a");
}

void jyf_log_impl(const char *level, const char *fmt, ...) {
    jyf_log_open();
    if (!g_log) return;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    
    char msg[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    
    fprintf(g_log, "[%02d:%02d:%02d.%03ld][%s][pid=%d] %s\n",
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            (long)(tv.tv_usec / 1000), level, (int)getpid(), msg);
    fflush(g_log);
}

// ==================== 特性位查询 ====================
static inline bool feat_enabled(uint32_t bit) {
    if (!g_state) return false;
    return (g_state->feature_mask & bit) != 0;
}

static inline void bump(int idx) {
    if (g_state && idx >= 0 && idx < JY_CNT_MAX) {
        g_state->counters[idx]++;
    }
}

// ==================== 内联钩子 (aarch64) ====================
// patch: ldr x16, #8 ; br x16 ; .quad detour  (16 字节, 无 trampoline 需求)

static int inline_hook(uintptr_t target, uintptr_t detour, int slot) {
    if (slot >= MAX_INLINE_HOOKS) return -1;
    
    // 保存原始指令
    memcpy(g_orig_code[slot], (void *)target, 16);
    
    uint32_t patch[4] = {
        0x58000050,              // ldr x16, [pc, #8]
        0xd61f0200,              // br x16
        (uint32_t)(detour & 0xFFFFFFFF),
        (uint32_t)(detour >> 32)
    };
    
    memcpy((void *)target, patch, 16);
    __builtin___clear_cache((char *)target, (char *)target + 16);
    
    g_hook_targets[slot] = target;
    g_hook_count_actual++;
    
    JYF_LOG("内联钩子 #%d 安装: 0x%lx -> 0x%lx", slot, target, detour);
    return 0;
}

static void inline_unhook_all(void) {
    for (int i = 0; i < g_hook_count_actual; i++) {
        memcpy((void *)g_hook_targets[i], g_orig_code[i], 16);
        __builtin___clear_cache((char *)g_hook_targets[i],
                                 (char *)g_hook_targets[i] + 16);
        JYF_LOG("内联钩子已还原: 0x%lx", g_hook_targets[i]);
    }
    g_hook_count_actual = 0;
}

// ==================== detour 函数 ====================
// 返回 w0 = 0, 与厂商空桩约定一致

// 锁屏
static uintptr_t detour_ShowLockScreen(uintptr_t self, int flag) {
    (void)self; (void)flag;
    if (feat_enabled(JY_FEAT_LOCK)) {
        bump(JY_CNT_LOCK);
        JYF_LOG("拦截 ShowLockScreen(flag=%d) -> 放行不锁屏", flag);
        return 0;
    }
    // 特性关闭时直接返回 (不执行原逻辑)
    return 0;
}

// 黑屏肃静
static uintptr_t detour_ShowBlackScreen(uintptr_t self, int flag,
                                        uintptr_t back_type, uintptr_t str,
                                        uint32_t a, uint32_t b) {
    (void)self; (void)flag; (void)back_type; (void)str; (void)a; (void)b;
    if (feat_enabled(JY_FEAT_LOCK)) {
        bump(JY_CNT_LOCK);
        JYF_LOG("拦截 ShowBlackScreen -> 放行");
        return 0;
    }
    return 0;
}

// 教师监视
static uintptr_t detour_StartMonitorPassive(uintptr_t self, uintptr_t params) {
    (void)self; (void)params;
    if (feat_enabled(JY_FEAT_MONITOR)) {
        bump(JY_CNT_MONITOR);
        JYF_LOG("拦截 StartMonitorPassive -> 拒绝监视");
        return 0;
    }
    return 0;
}

// RDP 监视
static uintptr_t detour_StartRdpMonitorPassive(uintptr_t self, uintptr_t params) {
    (void)self; (void)params;
    if (feat_enabled(JY_FEAT_MONITOR)) {
        bump(JY_CNT_MONITOR);
        JYF_LOG("拦截 StartRdpMonitorPassive -> 拒绝 RDP 监视");
        return 0;
    }
    return 0;
}

// 桌面演示/监看命令
static uintptr_t detour_ProcessDeskMonitorCommand(uintptr_t self, uintptr_t cmd) {
    (void)self; (void)cmd;
    if (feat_enabled(JY_FEAT_MONITOR)) {
        bump(JY_CNT_MONITOR);
        JYF_LOG("拦截 ProcessDeskMonitorCommand -> 放行不执行");
        return 0;
    }
    return 0;
}

// 远程命令
static uintptr_t detour_ExecuteRemoteCmd(uintptr_t self, uintptr_t mode) {
    (void)self; (void)mode;
    if (feat_enabled(JY_FEAT_COMMAND)) {
        bump(JY_CNT_COMMAND);
        JYF_LOG("拦截 ExecuteRemoteCmd(mode=%lu) -> 拒绝", (unsigned long)mode);
        return 0;
    }
    return 0;
}

static uintptr_t detour_ProcessRemoteCommand(uintptr_t self, uintptr_t params) {
    (void)self; (void)params;
    if (feat_enabled(JY_FEAT_COMMAND)) {
        bump(JY_CNT_COMMAND);
        JYF_LOG("拦截 ProcessRemoteCommand -> 拒绝");
        return 0;
    }
    return 0;
}

// 策略下发到 root 服务
static uintptr_t detour_UpdatePolicies(uintptr_t self) {
    (void)self;
    if (feat_enabled(JY_FEAT_POLICY)) {
        bump(JY_CNT_POLICY);
        JYF_LOG("拦截 UpdatePoliciesToStudentService -> 阻断策略下发");
        return 0;
    }
    return 0;
}

static uintptr_t detour_UpdateUsbPolicy(uintptr_t self) {
    (void)self;
    if (feat_enabled(JY_FEAT_POLICY)) {
        bump(JY_CNT_POLICY);
        JYF_LOG("拦截 UpdateUsbPolicyToStudentService -> 阻断 USB 策略");
        return 0;
    }
    return 0;
}

static uintptr_t detour_UpdateWebPolicy(uintptr_t self) {
    (void)self;
    if (feat_enabled(JY_FEAT_POLICY)) {
        bump(JY_CNT_POLICY);
        JYF_LOG("拦截 UpdateWebPolicyToStudentService -> 阻断网页策略");
        return 0;
    }
    return 0;
}

// libGetAppsInfo: 杀进程
static uintptr_t detour_KillProcess(uintptr_t pid) {
    if (feat_enabled(JY_FEAT_APPS)) {
        bump(JY_CNT_APPS);
        JYF_LOG("拦截 KillProcess(pid=%lu) -> 拒绝", (unsigned long)pid);
        return 0;
    }
    return 0;
}

// libGetAppsInfo: 关顶层窗口
static uintptr_t detour_CloseTopWindow(uintptr_t wid) {
    if (feat_enabled(JY_FEAT_APPS)) {
        bump(JY_CNT_APPS);
        JYF_LOG("拦截 CloseTopWindow(wid=%lu) -> 拒绝", (unsigned long)wid);
        return 0;
    }
    return 0;
}

static uintptr_t detour_CloseApps(uintptr_t a) {
    (void)a;
    if (feat_enabled(JY_FEAT_APPS)) {
        bump(JY_CNT_APPS);
        JYF_LOG("拦截 CloseApps -> 拒绝");
        return 0;
    }
    return 0;
}

static uintptr_t detour_CloseWndByWindowId(uintptr_t wid) {
    if (feat_enabled(JY_FEAT_APPS)) {
        bump(JY_CNT_APPS);
        JYF_LOG("拦截 CloseWndByWindowId(%lu) -> 拒绝", (unsigned long)wid);
        return 0;
    }
    return 0;
}

// libGetAppsInfo: 应用策略总入口
static uintptr_t detour_DoAppPolicyControl(uintptr_t cmd, uintptr_t param) {
    (void)param;
    if (feat_enabled(JY_FEAT_APPS) || feat_enabled(JY_FEAT_POLICY)) {
        bump(JY_CNT_APPS);
        JYF_LOG("拦截 DoAppPolicyControl(cmd=%lu) -> 拒绝", (unsigned long)cmd);
        return 0;
    }
    return 0;
}

// libcastng: freerdp 输入回调 (教师反向控制的最终落点)
static uintptr_t detour_tf_peer_keyboard_event(uintptr_t input,
                                               uint16_t flags, uint16_t code) {
    (void)input; (void)flags; (void)code;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 tf_peer_keyboard_event(code=%u) -> 吞掉按键", code);
        return 0;
    }
    return 0;
}

static uintptr_t detour_tf_peer_unicode_keyboard_event(uintptr_t input,
                                                       uint16_t flags, uint16_t code) {
    (void)input; (void)flags; (void)code;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 tf_peer_unicode_keyboard_event -> 吞掉");
        return 0;
    }
    return 0;
}

static uintptr_t detour_tf_peer_mouse_event(uintptr_t input, uint16_t flags,
                                            uint16_t x, uint16_t y) {
    (void)input; (void)flags; (void)x; (void)y;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 tf_peer_mouse_event(%u,%u) -> 吞掉鼠标", x, y);
        return 0;
    }
    return 0;
}

static uintptr_t detour_tf_peer_extended_mouse_event(uintptr_t input, uint16_t flags,
                                                     uint16_t x, uint16_t y) {
    (void)input; (void)flags; (void)x; (void)y;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 tf_peer_extended_mouse_event -> 吞掉");
        return 0;
    }
    return 0;
}

static uintptr_t detour_CastClient_setInputGrab(uintptr_t self, bool grab) {
    (void)self;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 CastClient::setInputGrab(%d) -> 强制不抓取", (int)grab);
        return 0;  // 永远不抓取输入
    }
    return 0;
}

// ==================== XImage 安全访问 ====================
// 结构体布局取自 /usr/include/X11/Xlib.h (LP64):
//   int  width;            // +0
//   int  height;           // +4
//   int  xoffset;          // +8
//   int  format;           // +12
//   char *data;            // +16   ← 指针, 8 字节
//   int  byte_order;       // +24
//   int  bitmap_unit;      // +28
//   int  bitmap_bit_order; // +32
//   int  bitmap_pad;       // +36
//   int  depth;            // +40
//   int  bytes_per_line;   // +44
//   int  bits_per_pixel;   // +48
//   ...
// 注意: 在 32 位系统上指针为 4 字节, data 在 +12。这里只面向 aarch64。
#define XI_WIDTH          0
#define XI_HEIGHT         4
#define XI_DATA          16
#define XI_BYTES_PER_LINE 44

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bpl;
    unsigned char *data;
    uint32_t bytes_per_line;   // 实际字节数
    bool     valid;
} XiView;

// 安全解析 XImage*, 任何字段不合理都返回 valid=false
static XiView xi_parse(void *ximage) {
    XiView v;
    memset(&v, 0, sizeof(v));

    if (!ximage) return v;

    v.width  = *(uint32_t *)((char *)ximage + XI_WIDTH);
    v.height = *(uint32_t *)((char *)ximage + XI_HEIGHT);
    v.bpl    = *(uint32_t *)((char *)ximage + XI_BYTES_PER_LINE);
    v.data   = *(unsigned char **)((char *)ximage + XI_DATA);

    // 合理性校验: 尺寸不能为零, bpl 必须 >= width, 总大小不能溢出
    if (v.width == 0 || v.height == 0) return v;
    if (v.bpl == 0) return v;
    if (v.data == NULL) return v;
    if (v.bpl < v.width) return v;            // 至少每像素 1 字节
    if (v.width > 32768 || v.height > 32768) return v;  // 上限, 防溢出

    // 检查乘法溢出
    uint64_t total = (uint64_t)v.bpl * v.height;
    if (total > 512ULL * 1024 * 1024) return v;   // 512MB 上限

    v.bytes_per_line = (uint32_t)total;
    v.valid = true;
    return v;
}

// 统一处理截屏覆写: pass / freeze / black / white
static void xi_apply_mode(XiView *v) {
    if (!v->valid) return;

    switch (g_state->capture_mode) {
        case JY_CAPTURE_BLACK:
            memset(v->data, 0x00, v->bytes_per_line);
            JYF_LOG("截屏全黑: %ux%u", v->width, v->height);
            break;

        case JY_CAPTURE_WHITE:
            memset(v->data, 0xFF, v->bytes_per_line);
            JYF_LOG("截屏全白: %ux%u", v->width, v->height);
            break;

        case JY_CAPTURE_FREEZE: {
            if (g_last_frame && g_last_frame_size == v->bytes_per_line &&
                g_last_frame_w == v->width && g_last_frame_h == v->height) {
                memcpy(v->data, g_last_frame, v->bytes_per_line);
                JYF_LOG("截屏冻结: %ux%u", v->width, v->height);
            } else {
                // 首帧: 缓存真实画面, 之后才冻结
                void *nb = malloc(v->bytes_per_line);
                if (nb) {
                    memcpy(nb, v->data, v->bytes_per_line);
                    free(g_last_frame);
                    g_last_frame = nb;
                    g_last_frame_size = v->bytes_per_line;
                    g_last_frame_w = v->width;
                    g_last_frame_h = v->height;
                    JYF_LOG("截屏缓存首帧: %ux%u", v->width, v->height);
                } else {
                    JYF_ERR("冻结帧缓存分配失败 (%u 字节)", v->bytes_per_line);
                }
            }
            break;
        }

        default:
            break;  // JY_CAPTURE_PASS
    }
}

// ==================== X11 GOT 钩子 detour ====================
// 这些函数在 payload 内部直接实现, 通过 g_orig_* 调原函数

static uintptr_t hook_XGetImage(void *dpy, uintptr_t drawable, int x, int y,
                                unsigned int width, unsigned int height,
                                unsigned long plane_mask, int format) {
    void *img = NULL;
    if (g_orig_XGetImage) {
        img = ((void *(*)(void *, uintptr_t, int, int, unsigned int,
                          unsigned int, unsigned long, int))
               g_orig_XGetImage)(dpy, drawable, x, y, width, height,
                                 plane_mask, format);
    }
    
    bump(JY_CNT_CAPTURE);
    
    if (!feat_enabled(JY_FEAT_CAPTURE) || !img) {
        return (uintptr_t)img;
    }
    
    pthread_mutex_lock(&g_frame_lock);
    XiView v = xi_parse(img);
    if (v.valid) xi_apply_mode(&v);
    pthread_mutex_unlock(&g_frame_lock);
    
    return (uintptr_t)img;
}

static uintptr_t hook_XGrabKeyboard(void *dpy, uintptr_t grab_window,
                                    bool owner_events, int ptr_mode,
                                    int key_mode) {
    (void)dpy; (void)grab_window; (void)ptr_mode; (void)key_mode;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XGrabKeyboard(owner_events=%d) -> 返回成功但不抓取",
                (int)owner_events);
        return 0;  // Success = 0
    }
    if (g_orig_XGrabKeyboard) {
        return (uintptr_t)((int (*)(void *, uintptr_t, bool, int, int))
                           g_orig_XGrabKeyboard)(dpy, grab_window, owner_events,
                                                  ptr_mode, key_mode);
    }
    return 0;
}

static uintptr_t hook_XGrabPointer(void *dpy, uintptr_t grab_window,
                                   bool owner_events, int ptr_mode,
                                   int button_mode, uintptr_t confine_to,
                                   uintptr_t cursor) {
    (void)dpy; (void)grab_window; (void)ptr_mode; (void)button_mode;
    (void)confine_to; (void)cursor;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XGrabPointer(owner_events=%d) -> 返回成功但不抓取",
                (int)owner_events);
        return 0;
    }
    if (g_orig_XGrabPointer) {
        return (uintptr_t)((int (*)(void *, uintptr_t, bool, int, int,
                                    uintptr_t, uintptr_t))g_orig_XGrabPointer)(
            dpy, grab_window, owner_events, ptr_mode, button_mode,
            confine_to, cursor);
    }
    return 0;
}

static uintptr_t hook_XTestFakeKeyEvent(void *dpy, uint32_t keycode, bool is_press,
                                        unsigned long delay) {
    (void)dpy; (void)delay;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XTestFakeKeyEvent(keycode=%u, %s) -> 吞掉",
                keycode, is_press ? "down" : "up");
        return 0;
    }
    if (g_orig_XTestFakeKeyEvent) {
        return (uintptr_t)((int (*)(void *, uint32_t, bool, unsigned long))
                           g_orig_XTestFakeKeyEvent)(dpy, keycode, is_press, delay);
    }
    return 0;
}

static uintptr_t hook_XTestFakeButtonEvent(void *dpy, uint32_t button, bool is_press,
                                           unsigned long delay) {
    (void)dpy; (void)delay;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XTestFakeButtonEvent(button=%u, %s) -> 吞掉",
                button, is_press ? "down" : "up");
        return 0;
    }
    if (g_orig_XTestFakeButtonEvent) {
        return (uintptr_t)((int (*)(void *, uint32_t, bool, unsigned long))
                           g_orig_XTestFakeButtonEvent)(dpy, button, is_press, delay);
    }
    return 0;
}

static uintptr_t hook_XTestFakeMotionEvent(void *dpy, int screen, int x, int y,
                                           unsigned long delay) {
    (void)dpy; (void)screen; (void)delay;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XTestFakeMotionEvent(%d,%d) -> 吞掉指针移动", x, y);
        return 0;
    }
    if (g_orig_XTestFakeMotionEvent) {
        return (uintptr_t)((int (*)(void *, int, int, int, unsigned long))
                           g_orig_XTestFakeMotionEvent)(dpy, screen, x, y, delay);
    }
    return 0;
}

static uintptr_t hook_XWarpPointer(void *dpy, uintptr_t src_win, uintptr_t dst_win,
                                   int src_x, int src_y, unsigned int src_w,
                                   unsigned int src_h, int dst_x, int dst_y) {
    (void)dpy; (void)src_win; (void)dst_win; (void)src_x; (void)src_y;
    (void)src_w; (void)src_h;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XWarpPointer(%d,%d) -> 阻止指针锁定", dst_x, dst_y);
        return 0;
    }
    if (g_orig_XWarpPointer) {
        return (uintptr_t)((int (*)(void *, uintptr_t, uintptr_t, int, int,
                                    unsigned int, unsigned int, int, int))
                           g_orig_XWarpPointer)(dpy, src_win, dst_win, src_x, src_y,
                                                src_w, src_h, dst_x, dst_y);
    }
    return 0;
}

static uintptr_t hook_XkbLockModifiers(void *dpy, uint16_t min_keycode,
                                       uint16_t max_keycode, uint16_t modifiers) {
    (void)dpy; (void)min_keycode; (void)max_keycode; (void)modifiers;
    if (feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XkbLockModifiers -> 拒绝锁定修饰键");
        return 1;  // XkbSuccess = 1
    }
    return 0;
}

// XSendEvent: 只拦输入类事件 (type 2..6), 其余放行
// 因为 XSendEvent 也被用于发 WM_DELETE_WINDOW 关广播窗口
static uintptr_t hook_XSendEvent(void *dpy, uintptr_t w, bool propagate,
                                 unsigned long event_mask, uintptr_t event_send) {
    uint32_t ev_type = 0;
    if (event_send) {
        ev_type = *(uint32_t *)((char *)event_send + 8); // XEvent.type @ offset 8
    }
    
    // KeyPress=2, KeyRelease=3, ButtonPress=4, ButtonRelease=5, MotionNotify=6
    bool is_input_event = (ev_type >= 2 && ev_type <= 6);
    
    if (is_input_event && feat_enabled(JY_FEAT_INPUT)) {
        bump(JY_CNT_INPUT);
        JYF_LOG("拦截 XSendEvent(type=%u) -> 吞掉输入事件", ev_type);
        return 1;  // Status: True = 1
    }
    
    if (g_orig_XSendEvent) {
        return (uintptr_t)((int (*)(void *, uintptr_t, bool, unsigned long,
                                    uintptr_t))g_orig_XSendEvent)(
            dpy, w, propagate, event_mask, event_send);
    }
    return 0;
}

// XShmGetImage: 与 XGetImage 同理, 但走共享内存段
static uintptr_t hook_XShmGetImage(void *dpy, uintptr_t drawable, uintptr_t shmseg,
                                  int x, int y, unsigned int width,
                                  unsigned int height, unsigned long plane_mask,
                                  int format, uintptr_t image, int offset,
                                  void *data) {
    int status = 0;
    if (g_orig_XShmGetImage) {
        // XShmGetImage 返回 Status (Bool)。
        // XImage* 由调用方通过 image 参数传入, 原函数填充其 data 指针。
        typedef int (*xshm_fn_t)(void *, uintptr_t, uintptr_t, int, int,
                                 unsigned int, unsigned int, unsigned long,
                                 int, uintptr_t, int, void *);
        status = ((xshm_fn_t)g_orig_XShmGetImage)(
            dpy, drawable, shmseg, x, y, width, height,
            plane_mask, format, image, offset, data);
    }
    
    bump(JY_CNT_CAPTURE);
    
    // 原函数失败时不碰像素
    if (!status || !feat_enabled(JY_FEAT_CAPTURE)) {
        return (uintptr_t)status;
    }
    
    // image 是调用方提供的 XImage*, 其 data 指向共享内存段
    pthread_mutex_lock(&g_frame_lock);
    XiView v = xi_parse((void *)image);
    if (v.valid) xi_apply_mode(&v);
    pthread_mutex_unlock(&g_frame_lock);
    
    return (uintptr_t)status;
}

// ==================== 钩子表 ====================
typedef struct {
    const char *name;
    void *hook;
    void **orig_slot;
} GotHookEntry;

static const GotHookEntry g_got_hooks[] = {
    { "XGetImage",              (void *)hook_XGetImage,              &g_orig_XGetImage },
    { "XShmGetImage",           (void *)hook_XShmGetImage,           &g_orig_XShmGetImage },
    { "XGrabKeyboard",          (void *)hook_XGrabKeyboard,          &g_orig_XGrabKeyboard },
    { "XGrabPointer",           (void *)hook_XGrabPointer,           &g_orig_XGrabPointer },
    { "XTestFakeKeyEvent",      (void *)hook_XTestFakeKeyEvent,      &g_orig_XTestFakeKeyEvent },
    { "XTestFakeButtonEvent",   (void *)hook_XTestFakeButtonEvent,   &g_orig_XTestFakeButtonEvent },
    { "XTestFakeMotionEvent",   (void *)hook_XTestFakeMotionEvent,   &g_orig_XTestFakeMotionEvent },
    { "XTestFakeRelativeMotionEvent", (void *)hook_XTestFakeMotionEvent, &g_orig_XTestFakeRelativeMotionEvent },
    { "XWarpPointer",           (void *)hook_XWarpPointer,           &g_orig_XWarpPointer },
    { "XkbLockModifiers",       (void *)hook_XkbLockModifiers,       &g_orig_XkbLockModifiers },
    { "XSendEvent",             (void *)hook_XSendEvent,             &g_orig_XSendEvent },
};

#define GOT_HOOK_COUNT (sizeof(g_got_hooks) / sizeof(g_got_hooks[0]))

// ==================== GOT 扫描 ====================
// 通过 dl_iterate_phdr 遍历所有已加载 ELF, 解析 PT_DYNAMIC,
// 在 DT_JMPREL/DT_RELA 中按名字匹配并改写

struct got_scan_ctx {
    const char *want_name;
    uintptr_t   found_addr;
};

static int phdr_callback(struct dl_phdr_info *info, size_t size, void *data) {
    (void)size;
    struct got_scan_ctx *ctx = (struct got_scan_ctx *)data;
    const char *name = info->dlpi_name;
    
    // 跳过自身与 libc
    if (name && strstr(name, "libjyfree")) return 0;
    
    const ElfW(Phdr) *phdr = info->dlpi_phdr;
    uintptr_t base = (uintptr_t)info->dlpi_addr;
    
    // 遍历 program headers 找 PT_DYNAMIC
    for (int i = 0; i < info->dlpi_phnum; i++) {
        if (phdr[i].p_type != PT_DYNAMIC) continue;
        
        const ElfW(Dyn) *dyn = (const ElfW(Dyn) *)(base + phdr[i].p_vaddr);
        if (!dyn) continue;
        
        const ElfW(Sym) *symtab = NULL;
        const char *strtab = NULL;
        const ElfW(Rela) *jmprel = NULL;
        size_t pltrelsz = 0;
        const ElfW(Rela) *rela = NULL;
        size_t relasz = 0;
        
        for (const ElfW(Dyn) *d = dyn; d->d_tag != DT_NULL; d++) {
            switch (d->d_tag) {
                case DT_SYMTAB:  symtab = (const ElfW(Sym) *)(base + d->d_un.d_ptr); break;
                case DT_STRTAB:  strtab = (const char *)(base + d->d_un.d_ptr); break;
                case DT_JMPREL:  jmprel = (const ElfW(Rela) *)(base + d->d_un.d_ptr); break;
                case DT_PLTRELSZ: pltrelsz = (size_t)d->d_un.d_val; break;
                case DT_RELA:    rela = (const ElfW(Rela) *)(base + d->d_un.d_ptr); break;
                case DT_RELASZ:  relasz = (size_t)d->d_un.d_val; break;
                default: break;
            }
        }
        
        if (!symtab || !strtab) continue;
        
        size_t count;
        const ElfW(Rela) *rel = jmprel;
        size_t relsz = pltrelsz;
        
        // JMPREL 未命中时尝试普通 RELA
        if (!rel || !relsz) {
            rel = rela;
            relsz = relasz;
        }
        if (!rel || !relsz) continue;
        
        count = relsz / sizeof(ElfW(Rela));
        
        for (size_t k = 0; k < count; k++) {
            uint32_t type = (uint32_t)ELF64_R_TYPE(rel[k].r_info);
            uint32_t sym_idx = (uint32_t)ELF64_R_SYM(rel[k].r_info);
            
            // aarch64: R_AARCH64_JUMP_SLOT(1026) / R_AARCH64_GLOB_DAT(1025)
            if (type != 1026 && type != 1025) continue;
            if (sym_idx == 0) continue;
            
            const char *sym_name = strtab + symtab[sym_idx].st_name;
            if (!sym_name || !*sym_name) continue;
            
            if (strcmp(sym_name, ctx->want_name) != 0) continue;
            
            // r_offset 是虚拟地址, 需加基址 (PIE 时)
            uintptr_t got_addr = base + rel[k].r_offset;
            
            // 已在 payload 的 BSS 范围内说明已钩过
            if (got_addr > (uintptr_t)&g_state && 
                got_addr < (uintptr_t)&g_state + (1UL << 20)) {
                return 0;
            }
            
            ctx->found_addr = got_addr;
            JYF_LOG("GOT 命中 %s @ %s got=0x%lx", sym_name, name ? name : "(main)", got_addr);
            return 1;  // 停止遍历
        }
    }
    
    return 0;
}

static uintptr_t find_got(const char *sym_name) {
    struct got_scan_ctx ctx = { .want_name = sym_name, .found_addr = 0 };
    dl_iterate_phdr(phdr_callback, &ctx);
    return ctx.found_addr;
}

static int patch_got(void) {
    int patched = 0;
    
    for (size_t i = 0; i < GOT_HOOK_COUNT; i++) {
        uintptr_t got = find_got(g_got_hooks[i].name);
        if (!got) continue;
        
        // 保存原值到对应 slot
        void *orig = *(void **)got;
        if (orig) {
            *(g_got_hooks[i].orig_slot) = orig;
        }
        
        *(void **)got = g_got_hooks[i].hook;
        patched++;
        
        JYF_LOG("GOT 已改写: %s -> %p", g_got_hooks[i].name, g_got_hooks[i].hook);
    }
    
    return patched;
}

// ==================== 跳桩解析 (thunk following) ====================
// libGetAppsInfo.so.2 的部分导出符号在 ELF 里是 4 字节的 B 跳转桩:
//
//     14000000  <imm26>     b  <真实函数>
//
// 直接在跳桩地址打 16 字节补丁会覆盖掉后面的 12 字节真实代码。
// 必须先跟随跳转拿到真实函数地址, 再在那里挂钩。
//
// 我们的 detour 一律返回 w0=0, 从不回调原函数,
// 因此不需要 trampoline, 可以安全覆盖真实函数的前 16 字节。

#define AARCH64_OP_B      0x14000000u
#define AARCH64_MASK_B    0xFC000000u
#define AARCH64_THUNK_MAX 4      // 最多跟随几跳

static uintptr_t resolve_thunk(uintptr_t addr) {
    for (int hop = 0; hop < AARCH64_THUNK_MAX; hop++) {
        uint32_t insn = 0;
        if (addr == 0) return 0;
        if (*(volatile uint32_t *)addr == 0) return 0;   // 不可读
        
        memcpy(&insn, (const void *)addr, 4);
        
        if ((insn & AARCH64_MASK_B) != AARCH64_OP_B) {
            return addr;    // 不是 B 指令, 这就是真实函数地址
        }
        
        // 符号扩展 imm26, 左移 2 位
        int32_t imm26 = (int32_t)(insn & 0x03FFFFFF);
        if (imm26 & 0x02000000) imm26 |= (int32_t)0xFC000000;  // sign extend
        int64_t offset = (int64_t)imm26 * 4;
        
        addr = (uintptr_t)((int64_t)addr + offset);
        JYF_LOG("跟随跳桩: -> 0x%lx", (unsigned long)addr);
    }
    
    JYF_ERR("跳桩链过长, 放弃解析");
    return 0;
}

// ==================== libGetAppsInfo 符号解析 ====================
// 优先用 dlsym 拿运行地址 (自动处理 PIC/重定位),
// 失败时退回 nm 偏移 + 库基址。

typedef struct {
    const char *name;      // 导出符号名
    uintptr_t    nm_off;    // nm 偏移 (Student v2.7.3715)
    uintptr_t  *resolved;   // 输出: 真实函数地址
} SymSpec;

// 先取 dlsym 地址 (可能是跳桩), 再 resolve_thunk
static bool resolve_appsinfo_symbol(void *handle, const SymSpec *spec) {
    void *sym = dlsym(handle, spec->name);
    if (sym) {
        uintptr_t real = resolve_thunk((uintptr_t)sym);
        if (real) {
            *spec->resolved = real;
            return true;
        }
    }
    return false;
}

// dlsym 全部失败时, 用库基址 + nm 偏移
static void resolve_appsinfo_by_offset(uintptr_t base, const SymSpec *specs, int n) {
    for (int i = 0; i < n; i++) {
        if (*specs[i].resolved) continue;
        uintptr_t real = resolve_thunk(base + specs[i].nm_off);
        if (real) {
            *specs[i].resolved = real;
            JYF_LOG("libGetAppsInfo %s 回退到 base+0x%lx -> 0x%lx",
                    specs[i].name, (unsigned long)specs[i].nm_off, (unsigned long)real);
        }
    }
}

// ==================== 内联钩子安装 ====================
static int install_inline_hooks(void) {
    int slot = 0;
    
    // CStudentMainWork — Student 是 ET_EXEC 非PIE, nm 地址即运行地址
    // (主程序不是共享库, 没有 PIC 重定位问题)
    struct { uintptr_t addr; void *detour; const char *name; uint32_t bit; } mhooks[] = {
        { SYM_ShowLockScreen,            (void *)detour_ShowLockScreen,            "ShowLockScreen",            JY_FEAT_LOCK },
        { SYM_ShowBlackScreen,           (void *)detour_ShowBlackScreen,           "ShowBlackScreen",           JY_FEAT_LOCK },
        { SYM_StartMonitorPassive,       (void *)detour_StartMonitorPassive,       "StartMonitorPassive",       JY_FEAT_MONITOR },
        { SYM_StartRdpMonitorPassive,    (void *)detour_StartRdpMonitorPassive,    "StartRdpMonitorPassive",    JY_FEAT_MONITOR },
        { SYM_ProcessDeskMonitorCommand, (void *)detour_ProcessDeskMonitorCommand, "ProcessDeskMonitorCommand", JY_FEAT_MONITOR },
        { SYM_ExecuteRemoteCmd,          (void *)detour_ExecuteRemoteCmd,          "ExecuteRemoteCmd",          JY_FEAT_COMMAND },
        { SYM_ProcessRemoteCommand,      (void *)detour_ProcessRemoteCommand,      "ProcessRemoteCommand",      JY_FEAT_COMMAND },
        { SYM_UpdatePoliciesToService,   (void *)detour_UpdatePolicies,            "UpdatePoliciesToService",   JY_FEAT_POLICY },
        { SYM_UpdateUsbPolicyToService,  (void *)detour_UpdateUsbPolicy,           "UpdateUsbPolicyToService",  JY_FEAT_POLICY },
        { SYM_UpdateWebPolicyToService,  (void *)detour_UpdateWebPolicy,           "UpdateWebPolicyToService",  JY_FEAT_POLICY },
    };
    
    for (size_t i = 0; i < sizeof(mhooks)/sizeof(mhooks[0]); i++) {
        if (!(g_state->feature_mask & mhooks[i].bit)) continue;
        if (inline_hook(mhooks[i].addr, (uintptr_t)mhooks[i].detour, slot) == 0) {
            slot++;
            JYF_LOG("已钩 %s @ 0x%lx", mhooks[i].name, (unsigned long)mhooks[i].addr);
        } else {
            JYF_ERR("%s 挂钩失败", mhooks[i].name);
        }
    }
    
    // libGetAppsInfo.so.2 — 部分导出是 4 字节 B 跳桩, 必须解析后再钩
    static uintptr_t a_KillProcess, a_CloseTopWindow, a_CloseApps;
    static uintptr_t a_CloseWndByWindowId, a_DoAppPolicyControl;
    
    void *apps = dlopen(JY_LIB_GETAPPS, RTLD_NOW | RTLD_NOLOAD);
    if (!apps) {
        JYF_LOG("libGetAppsInfo.so.2 未加载, 跳过其内联钩子");
    } else if (g_state->feature_mask & JY_FEAT_APPS) {
        SymSpec specs[] = {
            { "KillProcess",         SYM_KillProcess,         &a_KillProcess },
            { "CloseTopWindow",      SYM_CloseTopWindow,      &a_CloseTopWindow },
            { "CloseApps",           SYM_CloseApps,           &a_CloseApps },
            { "CloseWndByWindowId",  SYM_CloseWndByWindowId,  &a_CloseWndByWindowId },
            { "DoAppPolicyControl",  SYM_DoAppPolicyControl,  &a_DoAppPolicyControl },
        };
        const int n = (int)(sizeof(specs) / sizeof(specs[0]));
        
        int found = 0;
        for (int i = 0; i < n; i++) {
            if (resolve_appsinfo_symbol(apps, &specs[i])) {
                JYF_LOG("libGetAppsInfo %s -> 0x%lx", specs[i].name,
                        (unsigned long)*specs[i].resolved);
                found++;
            }
        }
        
        // dlsym 部分失败时回退到 nm 偏移 + 库基址
        if (found < n) {
            Dl_info dl_info;
            if (dladdr(apps, &dl_info) && dl_info.dli_fbase) {
                JYF_LOG("libGetAppsInfo 基址 0x%lx", (unsigned long)dl_info.dli_fbase);
                resolve_appsinfo_by_offset((uintptr_t)dl_info.dli_fbase, specs, n);
            }
        }
        
        // 挂钩 (只挂解析成功的)
        struct { uintptr_t addr; void *detour; const char *name; } hooks[] = {
            { a_KillProcess,         (void *)detour_KillProcess,         "KillProcess" },
            { a_CloseTopWindow,      (void *)detour_CloseTopWindow,      "CloseTopWindow" },
            { a_CloseApps,           (void *)detour_CloseApps,           "CloseApps" },
            { a_CloseWndByWindowId,  (void *)detour_CloseWndByWindowId,  "CloseWndByWindowId" },
            { a_DoAppPolicyControl,  (void *)detour_DoAppPolicyControl,  "DoAppPolicyControl" },
        };
        
        for (size_t i = 0; i < sizeof(hooks)/sizeof(hooks[0]); i++) {
            if (!hooks[i].addr) {
                JYF_ERR("%s 地址未能解析, 跳过", hooks[i].name);
                continue;
            }
            if (inline_hook(hooks[i].addr, (uintptr_t)hooks[i].detour, slot) == 0) {
                slot++;
                JYF_LOG("已钩 %s @ 0x%lx", hooks[i].name, (unsigned long)hooks[i].addr);
            }
        }
    }
    
    // libcastng.so.0.1 - freerdp 输入回调 (教师控制的最终落点)
    // 同样不能用 nm 绝对地址: 这是共享库, 运行地址 = 基址 + 偏移。
    // 用 dlsym 拿运行地址 (自动处理重定位), 失败才回退 nm 偏移。
    void *castng = dlopen(JY_LIB_CASTNG, RTLD_NOW | RTLD_NOLOAD);
    if (!castng) {
        JYF_LOG("libcastng 未加载, 跳过输入回调钩子");
    } else if (g_state->feature_mask & JY_FEAT_INPUT) {
        static uintptr_t c_kbd, c_ukbd, c_mouse, c_emouse, c_grab;
        
        SymSpec cspecs[] = {
            { "_Z22tf_peer_keyboard_eventP9rdp_inputtt",        SYM_tf_peer_keyboard_event,        &c_kbd },
            { "_Z30tf_peer_unicode_keyboard_eventP9rdp_inputtt", SYM_tf_peer_unicode_keyboard_event, &c_ukbd },
            { "_Z19tf_peer_mouse_eventP9rdp_inputttt",          SYM_tf_peer_mouse_event,          &c_mouse },
            { "_Z28tf_peer_extended_mouse_eventP9rdp_inputttt", SYM_tf_peer_extended_mouse_event, &c_emouse },
            { "_ZN10CastClient12setInputGrabEb",                 SYM_CastClient_setInputGrab_thunk, &c_grab },
        };
        const int cn = (int)(sizeof(cspecs) / sizeof(cspecs[0]));
        
        int cfound = 0;
        for (int i = 0; i < cn; i++) {
            if (resolve_appsinfo_symbol(castng, &cspecs[i])) cfound++;
            else JYF_ERR("libcastng 符号未找到: %s", cspecs[i].name);
        }
        
        if (cfound < cn) {
            Dl_info di;
            if (dladdr(castng, &di) && di.dli_fbase) {
                resolve_appsinfo_by_offset((uintptr_t)di.dli_fbase, cspecs, cn);
            }
        }
        
        struct { uintptr_t addr; void *detour; const char *name; } chooks[] = {
            { c_kbd,    (void *)detour_tf_peer_keyboard_event,       "tf_peer_keyboard_event" },
            { c_ukbd,   (void *)detour_tf_peer_unicode_keyboard_event,"tf_peer_unicode_keyboard_event" },
            { c_mouse,  (void *)detour_tf_peer_mouse_event,          "tf_peer_mouse_event" },
            { c_emouse, (void *)detour_tf_peer_extended_mouse_event, "tf_peer_extended_mouse_event" },
            { c_grab,   (void *)detour_CastClient_setInputGrab,      "CastClient::setInputGrab" },
        };
        
        for (size_t i = 0; i < sizeof(chooks)/sizeof(chooks[0]); i++) {
            if (!chooks[i].addr) {
                JYF_ERR("%s 地址未能解析, 跳过", chooks[i].name);
                continue;
            }
            if (inline_hook(chooks[i].addr, (uintptr_t)chooks[i].detour, slot) == 0) {
                slot++;
                JYF_LOG("已钩 %s @ 0x%lx", chooks[i].name, (unsigned long)chooks[i].addr);
            }
        }
    }
    
    return slot;
}

// ==================== 协调线程 ====================
static void *payload_thread(void *arg) {
    (void)arg;
    
    uint64_t last_shm_check = 0;
    
    while (g_running) {
        // 每秒更新心跳
        if (g_state) {
            struct timeval tv;
            gettimeofday(&tv, NULL);
            g_state->heartbeat = (uint64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
        }
        
        // 每 2 秒重扫新加载的模块 (libcast-x11.so 是按需 dlopen 的)
        uint64_t now = (uint64_t)time(NULL);
        if (now != last_shm_check) {
            last_shm_check = now;
            
            // 检测 libcast-x11 是否刚被加载 (它按需 dlopen, 可能很晚才出现)
            void *x11 = dlopen(JY_LIB_CAST_X11, RTLD_NOW | RTLD_NOLOAD);
            if (x11 != g_last_x11_handle) {
                if (x11) {
                    // 新加载 -> 立即补钩
                    JYF_LOG("检测到 libcast-x11.so 已加载, 扫描其 GOT");
                    patch_got();
                } else {
                    JYF_LOG("libcast-x11.so 已卸载, 等待重新加载");
                }
                g_last_x11_handle = x11;
            }
        }
        
        usleep(500000);
    }
    
    return NULL;
}

// ==================== 共享内存 ====================
static int attach_shm(void) {
    if (g_state) return 0;
    
    g_shm_fd = shm_open(JYFREE_SHM_NAME, O_RDWR, 0600);
    if (g_shm_fd < 0) {
        // 尝试创建
        g_shm_fd = shm_open(JYFREE_SHM_NAME, O_RDWR | O_CREAT, 0600);
        if (g_shm_fd < 0) {
            JYF_ERR("无法打开共享内存 %s: %s", JYFREE_SHM_NAME, strerror(errno));
            return -1;
        }
        if (ftruncate(g_shm_fd, sizeof(JySharedState)) < 0) {
            JYF_ERR("无法设置共享内存大小");
            return -1;
        }
    }
    
    g_shm_ptr = mmap(NULL, sizeof(JySharedState), PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_shm_fd, 0);
    if (g_shm_ptr == MAP_FAILED) {
        g_shm_ptr = NULL;
        JYF_ERR("mmap 共享内存失败");
        return -1;
    }
    
    g_state = (JySharedState *)g_shm_ptr;
    
    // 校验 magic
    if (g_state->magic != JY_SHARED_MAGIC) {
        JYF_ERR("共享内存 magic 不匹配 (期望 0x%08X, 实际 0x%08X)",
                JY_SHARED_MAGIC, g_state->magic);
        return -1;
    }
    
    JYF_LOG("已挂载共享内存, feature_mask=0x%02X", g_state->feature_mask);
    return 0;
}

// ==================== payload 入口 ====================
__attribute__((constructor))
static void payload_init(void) {
    jyf_log_open();
    JYF_LOG("=== libjyfree.so 已注入 pid=%d ===", (int)getpid());
    
    if (attach_shm() < 0) {
        JYF_ERR("共享内存挂载失败, payload 退出");
        return;
    }
    
    // 安装内联钩子
    int inline_hooks = install_inline_hooks();
    
    // 安装 GOT 钩子 (X11)
    int got_hooks = patch_got();
    
    g_state->hooks_installed = (uint32_t)(inline_hooks + got_hooks);
    g_state->payload_ready = 1;
    
    JYF_LOG("钩子安装完成: 内联=%d, GOT=%d, 合计=%u",
            inline_hooks, got_hooks, g_state->hooks_installed);
    
    // 启动协调线程
    g_running = 1;
    if (pthread_create(&g_thread, NULL, payload_thread, NULL) != 0) {
        JYF_ERR("协调线程创建失败");
        g_state->payload_ready = 0;
        return;
    }
    
    JYF_LOG("payload 初始化完成, 进入守护状态");
}

__attribute__((destructor))
static void payload_fini(void) {
    JYF_LOG("payload 卸载中...");
    g_running = 0;
    pthread_join(g_thread, NULL);
    
    inline_unhook_all();
    
    if (g_state) {
        g_state->payload_ready = 0;
        g_state->hooks_installed = 0;
    }
    
    if (g_shm_ptr && g_shm_ptr != MAP_FAILED) {
        munmap(g_shm_ptr, sizeof(JySharedState));
    }
    if (g_shm_fd >= 0) close(g_shm_fd);
    
    JYF_LOG("payload 卸载完成");
}

// 供控制器查询 payload 状态
uint32_t jyfree_payload_version(void) {
    return 0x00020000;
}