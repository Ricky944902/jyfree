// ============================================================
// gui_main.c - jyfree-gui 入口
//
// 极域相关的一切控制都通过共享内存特性位完成, payload 立即响应。
// GUI 只负责显示状态和切换开关。
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include "gui.h"
#include "jiyu.h"

// ==================== 全局状态 ====================
static GuiWindow g_gui;
static volatile int g_running = 1;
static int g_student_pid = -1;

// 按钮索引
static int b_inject = -1;
static int b_lock = -1;
static int b_monitor = -1;
static int b_command = -1;
static int b_input = -1;
static int b_capture = -1;
static int b_policy = -1;
static int b_apps = -1;
static int b_refresh = -1;
static int b_log = -1;

// 状态指示器
static int i_student = -1;
static int i_payload = -1;
static int i_hooks = -1;
static int i_hits = -1;

// ==================== 日志回调 ====================
static void log_to_gui(JiyuDebugLevel level, const char *msg) {
    gui_log_window_add(&g_gui, level, msg);
}

// ==================== 按钮回调 ====================
static void on_inject(void) {
    int pid = jiyu_find_student_process();
    if (pid <= 0) {
        gui_set_status_message(&g_gui, "未找到 Student 进程");
        return;
    }
    
    g_student_pid = pid;
    gui_set_status_message(&g_gui, "正在注入...");
    
    if (jyfree_inject(pid, NULL) == 0) {
        char msg[96];
        snprintf(msg, sizeof(msg), "注入成功, 已装 %u 个钩子",
                 jy_state_hooks_installed());
        gui_set_status_message(&g_gui, msg);
    } else {
        gui_set_status_message(&g_gui, "注入失败, 查 /tmp/jyfree-payload.log");
    }
}

// 特性位切换按钮的通用实现
typedef struct {
    int index;
    uint32_t bit;
} FeatBtn;

static void toggle_feature(uint32_t bit, int status_index, const char *on_msg,
                           const char *off_msg) {
    uint32_t mask = jy_state_get_features();
    bool was_on = (mask & bit) != 0;
    
    if (was_on) {
        mask &= ~bit;
        gui_update_status(&g_gui, status_index, 0);
        gui_set_status_message(&g_gui, off_msg);
    } else {
        mask |= bit;
        gui_update_status(&g_gui, status_index, 1);
        gui_set_status_message(&g_gui, on_msg);
    }
    
    jy_state_set_features(mask);
    jiyu_get_config()->feature_mask = mask;
}

static void on_lock(void)    { toggle_feature(JY_FEAT_LOCK,    -1, "锁屏拦截: 开", "锁屏拦截: 关"); }
static void on_monitor(void) { toggle_feature(JY_FEAT_MONITOR, -1, "监视拦截: 开", "监视拦截: 关"); }
static void on_command(void) { toggle_feature(JY_FEAT_COMMAND, -1, "命令拦截: 开", "命令拦截: 关"); }
static void on_input(void)   { toggle_feature(JY_FEAT_INPUT,   -1, "输入拦截: 开", "输入拦截: 关"); }
static void on_policy(void)  { toggle_feature(JY_FEAT_POLICY,  -1, "策略拦截: 开", "策略拦截: 关"); }
static void on_apps(void)    { toggle_feature(JY_FEAT_APPS,    -1, "应用拦截: 开", "应用拦截: 关"); }

static void on_capture(void) {
    JiyuConfig *cfg = jiyu_get_config();
    
    // 循环: 放行 -> 冻结 -> 全黑
    const char *names[] = { "截屏: 放行", "截屏: 冻结", "截屏: 全黑" };
    
    cfg->capture_mode = (cfg->capture_mode + 1) % 3;
    jy_state_set_capture_mode(cfg->capture_mode);
    
    if (cfg->capture_mode == JY_CAPTURE_PASS) {
        jy_state_set_features(jy_state_get_features() & ~JY_FEAT_CAPTURE);
    } else {
        jy_state_set_features(jy_state_get_features() | JY_FEAT_CAPTURE);
    }
    
    gui_set_status_message(&g_gui, names[cfg->capture_mode]);
}

static void on_refresh(void) {
    int pid = jiyu_find_student_process();
    g_student_pid = pid;
    
    gui_update_status(&g_gui, i_student, pid > 0 ? 1 : 0);
    gui_update_status(&g_gui, i_payload, jy_state_payload_ready() ? 1 : 0);
    
    // 钩子数
    char msg[96];
    snprintf(msg, sizeof(msg), "钩子 %u 个", jy_state_hooks_installed());
    gui_set_status_message(&g_gui, msg);
    
    // 拦截计数
    uint64_t total = 0;
    for (int i = 0; i < JY_CNT_MAX; i++) total += jy_state_get_counter(i);
    snprintf(msg, sizeof(msg), "累计拦截 %llu 次",
             (unsigned long long)total);
    gui_set_status_message(&g_gui, msg);
}

static void on_log(void) {
    if (g_gui.log_window.visible) {
        gui_log_window_hide(&g_gui);
    } else {
        gui_log_window_show(&g_gui);
        gui_log_window_draw(&g_gui);
    }
}

// ==================== 线程 ====================
static void *gui_thread(void *arg) {
    (void)arg;
    while (g_running) {
        if (!gui_handle_events(&g_gui)) g_running = 0;
        usleep(10000);
    }
    return NULL;
}

static void *monitor_thread(void *arg) {
    (void)arg;
    int last_pid = -2;
    
    while (g_running) {
        int pid = jiyu_find_student_process();
        
        if (pid != last_pid) {
            last_pid = pid;
            gui_update_status(&g_gui, i_student, pid > 0 ? 1 : 0);
            
            if (pid > 0) {
                char msg[96];
                snprintf(msg, sizeof(msg), "检测到 Student (PID %d)", pid);
                gui_set_status_message(&g_gui, msg);
            } else {
                gui_set_status_message(&g_gui, "未检测到 Student");
            }
        }
        
        gui_update_status(&g_gui, i_payload, jy_state_payload_ready() ? 1 : 0);
        
        sleep(1);
    }
    return NULL;
}

// ==================== 信号 ====================
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// ==================== 主函数 ====================
int main(int argc, char *argv[]) {
    jiyu_init_config();
    
    bool auto_inject = false;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-i") == 0 || strcmp(argv[i], "--inject") == 0) {
            auto_inject = true;
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            jiyu_debug_set_level((JiyuDebugLevel)atoi(argv[++i]));
        } else if (strcmp(argv[i], "-h") == 0) {
            printf("jyfree-gui v%s\n\n", JIYU_VERSION_STRING);
            printf("用法: %s [选项]\n\n", argv[0]);
            printf("  -i, --inject    启动后立即注入 Student\n");
            printf("  -d, --debug N   调试级别 (0-5)\n");
            printf("  -h, --help      显示帮助\n");
            return 0;
        }
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    jiyu_log_open(NULL);
    jiyu_debug_set_callback(log_to_gui);
    
    if (jy_state_create() < 0) {
        fprintf(stderr, "共享内存初始化失败\n");
        return 1;
    }
    
    if (gui_init(&g_gui, 460, 330, "jyfree - 极域绕过 (UOS)") < 0) {
        fprintf(stderr, "GUI 初始化失败 (检查 DISPLAY)\n");
        return 1;
    }
    
    // 第一行: 注入 + 刷新 + 日志
    b_inject  = gui_add_button(&g_gui,  15, 58,  90, 30, "注入",   on_inject);
    b_refresh = gui_add_button(&g_gui, 112, 58,  90, 30, "刷新",   on_refresh);
    b_log     = gui_add_button(&g_gui, 209, 58,  70, 30, "日志",   on_log);
    b_capture = gui_add_button(&g_gui, 286, 58,  90, 30, "截屏:冻结", on_capture);
    
    // 第二行: 特性位开关 (两列)
    b_lock    = gui_add_button(&g_gui,  15, 100, 105, 30, "锁屏拦截", on_lock);
    b_monitor = gui_add_button(&g_gui, 130, 100, 105, 30, "监视拦截", on_monitor);
    b_command = gui_add_button(&g_gui, 245, 100, 105, 30, "命令拦截", on_command);
    
    // 第三行
    b_input   = gui_add_button(&g_gui,  15, 140, 105, 30, "输入拦截", on_input);
    b_policy  = gui_add_button(&g_gui, 130, 140, 105, 30, "策略拦截", on_policy);
    b_apps    = gui_add_button(&g_gui, 245, 140, 105, 30, "应用拦截", on_apps);
    
    // 状态指示器
    i_student = gui_add_status(&g_gui,  20, 200, "Student");
    i_payload = gui_add_status(&g_gui, 235, 200, "Payload");
    i_hooks   = gui_add_status(&g_gui,  20, 225, "已装钩子");
    i_hits    = gui_add_status(&g_gui, 235, 225, "拦截次数");
    
    gui_draw(&g_gui);
    
    pthread_t gui_tid, mon_tid;
    if (pthread_create(&gui_tid, NULL, gui_thread, NULL) != 0) {
        fprintf(stderr, "GUI 线程创建失败\n");
        return 1;
    }
    if (pthread_create(&mon_tid, NULL, monitor_thread, NULL) != 0) {
        fprintf(stderr, "监控线程创建失败\n");
        return 1;
    }
    
    // 看门狗
    jyfree_start_watchdog();
    
    gui_set_status_message(&g_gui, "就绪 - 点击注入开始");
    
    if (auto_inject) {
        on_inject();
    }
    
    while (g_running) {
        usleep(100000);
    }
    
    pthread_join(gui_tid, NULL);
    pthread_join(mon_tid, NULL);
    
    jyfree_stop_watchdog();
    g_student_pid = jiyu_find_student_process();
    jyfree_uninject(g_student_pid);
    
    jy_state_close();
    jiyu_log_close();
    gui_cleanup(&g_gui);
    
    return 0;
}