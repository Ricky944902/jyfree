// ============================================================
// main.c - jyfree CLI 入口 (控制器侧)
//
// 工作流程:
//   1. 初始化配置/日志/共享内存
//   2. 查找 Student 进程
//   3. ptrace 注入 libjyfree.so (payload 自装内联钩子 + GOT 钩子)
//   4. 启动看门狗 (Student 被 systemd 重启后自动重注入)
//   5. 运行时通过 stdin 接受控制命令, 实时改特性位
//
// 特性位改动立即对 payload 生效 (共享内存), 无需重新注入。
// ============================================================

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <errno.h>
#include <pthread.h>
#include <sys/select.h>
#include "jiyu.h"

static volatile sig_atomic_t g_running = 1;
static int g_student_pid = -1;

// ==================== 信号处理 ====================
static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

// ==================== 内置命令 ====================
static const char* feat_name(int bit) {
    switch (bit) {
        case JY_FEAT_LOCK:    return "锁屏";
        case JY_FEAT_MONITOR: return "监视";
        case JY_FEAT_COMMAND: return "命令";
        case JY_FEAT_APPS:    return "应用";
        case JY_FEAT_POLICY:  return "策略";
        case JY_FEAT_INPUT:   return "输入";
        case JY_FEAT_CAPTURE: return "截屏";
        default: return "?";
    }
}

static void cmd_help(int argc, char **argv) {
    (void)argc; (void)argv;
    
    printf("\n可用命令:\n");
    printf("  status              显示注入与拦截状态\n");
    printf("  feat <on|off|list>  特性位管理 (list 查看全部)\n");
    printf("  feat lock on        开启锁屏拦截\n");
    printf("  feat input off      关闭输入拦截\n");
    printf("  capture <pass|freeze|black>  截屏模式\n");
    printf("  inject              手动重新注入\n");
    printf("  uninject           卸载 payload 并还原钩子\n");
    printf("  process             显示极域进程\n");
    printf("  modules            显示 Student 已加载的极域库\n");
    printf("  freeze             冻结 Student (SIGSTOP)\n");
    printf("  thaw               恢复 Student (SIGCONT)\n");
    printf("  counters           显示各特性拦截次数\n");
    printf("  log <file>         查看 payload 日志尾部\n");
    printf("  debug <0-5>        设置调试级别\n");
    printf("  config <show|save|load>  配置管理\n");
    printf("  quit               退出\n");
}

static void cmd_status(int argc, char **argv) {
    (void)argc; (void)argv;
    
    JiyuConfig *cfg = jiyu_get_config();
    
    printf("\n=== jyfree v%s 状态 ===\n", JIYU_VERSION_STRING);
    printf("  Student 进程:   %s", g_student_pid > 0 ? "PID=" : "");
    if (g_student_pid > 0) printf("%d\n", g_student_pid);
    else printf("未运行\n");
    
    if (g_student_pid > 0) {
        JiyuProcessInfo info;
        if (jiyu_get_process_info(g_student_pid, &info) == 0) {
            printf("  进程状态:       %c (%s)\n", info.state,
                   info.state == 'T' ? "已停止, 注入被跳过" :
                   info.state == 'R' ? "运行中" : "其他");
            printf("  启动时间:       %llu\n", (unsigned long long)info.start_time);
            printf("  UID:            %s\n", info.is_root ? "root" : "普通用户");
        }
    }
    
    printf("  payload 就绪:   %s\n", jy_state_payload_ready() ? "是" : "否");
    printf("  已装钩子数:     %u\n", jy_state_hooks_installed());
    printf("  payload 心跳:   %llu\n", (unsigned long long)jy_state_get_heartbeat());
    printf("  特性位:         0x%02X\n", jy_state_get_features());
    
    printf("  当前开启:       ");
    uint32_t mask = jy_state_get_features();
    bool any = false;
    for (int bit = 0; bit <= 6; bit++) {
        if (mask & (1u << bit)) {
            printf("%s ", feat_name(1 << bit));
            any = true;
        }
    }
    if (!any) printf("(无)");
    printf("\n");
    
    printf("  截屏模式:       %s\n",
           cfg->capture_mode == JY_CAPTURE_FREEZE ? "冻结" :
           cfg->capture_mode == JY_CAPTURE_BLACK ? "全黑" :
           cfg->capture_mode == JY_CAPTURE_WHITE ? "全白" : "放行");
    printf("  调试级别:       %d\n", cfg->debug_level);
}

static void cmd_feat(int argc, char **argv) {
    if (argc < 2) {
        cmd_help(0, NULL);
        return;
    }
    
    if (strcmp(argv[1], "list") == 0) {
        uint32_t mask = jy_state_get_features();
        printf("\n特性位 (当前 0x%02X):\n", mask);
        for (int bit = 0; bit <= 6; bit++) {
            uint32_t b = 1u << bit;
            printf("  %-8s %s (0x%02X)\n", feat_name(b),
                   (mask & b) ? "[开]" : "[关]", b);
        }
        printf("\n用法: feat <名称> <on|off>\n");
        return;
    }
    
    if (argc < 3) {
        printf("用法: feat <名称> <on|off>\n");
        return;
    }
    
    // 解析特性名
    uint32_t bit = 0;
    struct { const char *name; uint32_t bit; } names[] = {
        { "lock",    JY_FEAT_LOCK },
        { "monitor", JY_FEAT_MONITOR },
        { "command", JY_FEAT_COMMAND },
        { "apps",    JY_FEAT_APPS },
        { "policy",  JY_FEAT_POLICY },
        { "input",   JY_FEAT_INPUT },
        { "capture", JY_FEAT_CAPTURE },
        { "all",     JY_FEAT_ALL },
    };
    
    for (size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++) {
        if (strcmp(argv[1], names[i].name) == 0) {
            bit = names[i].bit;
            break;
        }
    }
    
    if (!bit) {
        printf("未知特性: %s (可用: lock monitor command apps policy input capture all)\n", argv[1]);
        return;
    }
    
    uint32_t mask = jy_state_get_features();
    if (strcmp(argv[2], "on") == 0) {
        mask |= bit;
    } else if (strcmp(argv[2], "off") == 0) {
        mask &= ~bit;
    } else {
        printf("用法: feat %s <on|off>\n", argv[1]);
        return;
    }
    
    jy_state_set_features(mask);
    
    JiyuConfig *cfg = jiyu_get_config();
    cfg->feature_mask = mask;
    
    printf("特性位已更新: 0x%02X\n", mask);
}

static void cmd_capture(int argc, char **argv) {
    if (argc < 2) {
        printf("当前模式: %u\n", jiyu_get_config()->capture_mode);
        printf("用法: capture <pass|freeze|black|white>\n");
        return;
    }
    
    uint32_t mode;
    if (strcmp(argv[1], "pass") == 0)        mode = JY_CAPTURE_PASS;
    else if (strcmp(argv[1], "freeze") == 0) mode = JY_CAPTURE_FREEZE;
    else if (strcmp(argv[1], "black") == 0)  mode = JY_CAPTURE_BLACK;
    else if (strcmp(argv[1], "white") == 0)  mode = JY_CAPTURE_WHITE;
    else {
        printf("用法: capture <pass|freeze|black|white>\n");
        return;
    }
    
    jy_state_set_capture_mode(mode);
    jiyu_get_config()->capture_mode = mode;
    
    // freeze/black 需要开启 CAPTURE 特性
    if (mode != JY_CAPTURE_PASS) {
        jy_state_set_features(jy_state_get_features() | JY_FEAT_CAPTURE);
    }
}

static void cmd_inject(int argc, char **argv) {
    (void)argc; (void)argv;
    
    g_student_pid = jiyu_find_student_process();
    if (g_student_pid <= 0) {
        printf("未找到 Student 进程\n");
        return;
    }
    
    printf("正在注入 pid=%d ...\n", g_student_pid);
    
    if (jyfree_inject(g_student_pid, NULL) == 0) {
        printf("注入成功, 已装 %u 个钩子\n", jy_state_hooks_installed());
    } else {
        printf("注入失败 (检查 /tmp/jyfree-payload.log)\n");
    }
}

static void cmd_uninject(int argc, char **argv) {
    (void)argc; (void)argv;
    
    if (g_student_pid <= 0) g_student_pid = jiyu_find_student_process();
    
    jyfree_uninject(g_student_pid);
    printf("已卸载 payload\n");
}

static void cmd_process(int argc, char **argv) {
    (void)argc; (void)argv;
    
    int student = jiyu_find_student_process();
    int agent = jiyu_find_student_agent();
    int service = jiyu_find_student_service();
    
    printf("\n极域进程:\n");
    printf("  Student        PID=%d\n", student);
    printf("  StudentAgent   PID=%d  (systemd user 单元主进程, 看门狗)\n", agent);
    printf("  StudentService PID=%d  (root, 无输入能力)\n", service);
    
    if (student > 0) {
        JiyuProcessInfo info;
        if (jiyu_get_process_info(student, &info) == 0) {
            printf("\nStudent 详情:\n");
            printf("  exe:     %s\n", info.exe_path);
            printf("  cmdline: %s\n", info.cmdline);
            printf("  state:   %c\n", info.state);
        }
    }
}

static void cmd_modules(int argc, char **argv) {
    (void)argc; (void)argv;
    
    if (g_student_pid <= 0) g_student_pid = jiyu_find_student_process();
    if (g_student_pid <= 0) {
        printf("未找到 Student\n");
        return;
    }
    
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", g_student_pid);
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("无法读取 maps\n");
        return;
    }
    
    const char *targets[] = {
        JY_LIB_CASTNG, JY_LIB_DESK, JY_LIB_GETAPPS, JY_LIB_CAST_X11, "libjyfree.so"
    };
    
    printf("\n极域库加载状态:\n");
    
    // 收集 maps 内容
    char *maps = NULL;
    size_t maps_len = 0;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (sz > 0 && sz < 8*1024*1024) {
        maps = malloc(sz + 1);
        maps_len = fread(maps, 1, sz, fp);
        maps[maps_len] = '\0';
    }
    fclose(fp);
    
    if (!maps) {
        printf("maps 过大或读取失败\n");
        return;
    }
    
    for (size_t i = 0; i < sizeof(targets)/sizeof(targets[0]); i++) {
        if (strstr(maps, targets[i])) {
            printf("  [已加载] %s\n", targets[i]);
        } else {
            printf("  [未加载] %s\n", targets[i]);
        }
    }
    
    free(maps);
    
    printf("\n拦截计数:\n");
    printf("  锁屏   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_LOCK));
    printf("  监视   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_MONITOR));
    printf("  命令   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_COMMAND));
    printf("  应用   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_APPS));
    printf("  策略   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_POLICY));
    printf("  输入   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_INPUT));
    printf("  截屏   %llu\n", (unsigned long long)jy_state_get_counter(JY_CNT_CAPTURE));
}

static void cmd_counters(int argc, char **argv) {
    (void)argc; (void)argv;
    
    printf("\n拦截计数 (证明钩子生效):\n");
    static const char *names[] = { "锁屏", "监视", "命令", "应用", "策略", "输入", "截屏" };
    for (int i = 0; i < JY_CNT_MAX; i++) {
        uint64_t c = jy_state_get_counter(i);
        printf("  %-6s %llu %s\n", names[i], (unsigned long long)c,
               c > 0 ? "<- 已拦截" : "");
    }
}

static void cmd_freeze(int argc, char **argv) {
    (void)argc; (void)argv;
    if (g_student_pid <= 0) g_student_pid = jiyu_find_student_process();
    if (g_student_pid <= 0) { printf("未找到 Student\n"); return; }
    jiyu_freeze_process(g_student_pid);
}

static void cmd_thaw(int argc, char **argv) {
    (void)argc; (void)argv;
    if (g_student_pid <= 0) g_student_pid = jiyu_find_student_process();
    if (g_student_pid <= 0) { printf("未找到 Student\n"); return; }
    jiyu_thaw_process(g_student_pid);
}

static void cmd_log(int argc, char **argv) {
    (void)argc;
    
    const char *path = (argc > 1) ? argv[1] : "/tmp/jyfree-payload.log";
    
    FILE *fp = fopen(path, "r");
    if (!fp) {
        printf("无法打开 %s\n", path);
        return;
    }
    
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    long start = sz > 8192 ? sz - 8192 : 0;
    fseek(fp, start, SEEK_SET);
    
    char buf[8192];
    size_t n = fread(buf, 1, sizeof(buf), fp);
    fclose(fp);
    
    printf("--- %s (尾部) ---\n", path);
    fwrite(buf, 1, n, stdout);
}

static void cmd_debug(int argc, char **argv) {
    if (argc < 2) {
        printf("当前级别: %d\n", jiyu_get_config()->debug_level);
        return;
    }
    int level = atoi(argv[1]);
    if (level < 0 || level > 5) {
        printf("级别范围 0-5\n");
        return;
    }
    jiyu_debug_set_level((JiyuDebugLevel)level);
    printf("调试级别: %d\n", level);
}

static void cmd_config(int argc, char **argv) {
    if (argc < 2) {
        printf("用法: config <show|save|load>\n");
        return;
    }
    
    JiyuConfig *cfg = jiyu_get_config();
    
    if (strcmp(argv[1], "show") == 0) {
        printf("\n当前配置:\n");
        printf("  debug_level:   %d\n", cfg->debug_level);
        printf("  feature_mask:  0x%02X\n", cfg->feature_mask);
        printf("  capture_mode:  %u\n", cfg->capture_mode);
        printf("  auto_reinject: %s\n", cfg->auto_reinject ? "true" : "false");
        printf("  log_file:      %s\n", cfg->log_file[0] ? cfg->log_file : "(默认)");
    } else if (strcmp(argv[1], "save") == 0) {
        jiyu_config_save(argc > 2 ? argv[2] : NULL);
        printf("配置已保存\n");
    } else if (strcmp(argv[1], "load") == 0) {
        jiyu_config_load(argc > 2 ? argv[2] : NULL);
        printf("配置已加载\n");
    } else {
        printf("用法: config <show|save|load>\n");
    }
}

static void cmd_quit(int argc, char **argv) {
    (void)argc; (void)argv;
    g_running = 0;
}

// 命令表
typedef struct {
    const char *name;
    void (*handler)(int argc, char **argv);
} CliCommand;

static const CliCommand g_commands[] = {
    { "help",     cmd_help },
    { "status",   cmd_status },
    { "feat",     cmd_feat },
    { "capture",  cmd_capture },
    { "inject",   cmd_inject },
    { "uninject", cmd_uninject },
    { "process",  cmd_process },
    { "modules",  cmd_modules },
    { "counters", cmd_counters },
    { "freeze",   cmd_freeze },
    { "thaw",     cmd_thaw },
    { "log",      cmd_log },
    { "debug",    cmd_debug },
    { "config",   cmd_config },
    { "quit",     cmd_quit },
};

#define CMD_COUNT (sizeof(g_commands)/sizeof(g_commands[0]))

// 执行命令行
static void exec_command(char *line) {
    // 切分参数
    char *argv[16];
    int argc = 0;
    char *saveptr = NULL;
    
    for (char *tok = strtok_r(line, " \t", &saveptr);
         tok && argc < 16;
         tok = strtok_r(NULL, " \t", &saveptr)) {
        argv[argc++] = tok;
    }
    
    if (argc == 0) return;
    
    for (size_t i = 0; i < CMD_COUNT; i++) {
        if (strcmp(g_commands[i].name, argv[0]) == 0) {
            g_commands[i].handler(argc, argv);
            return;
        }
    }
    
    printf("未知命令: %s (输入 help 查看)\n", argv[0]);
}

// ==================== 使用说明 ====================
static void print_usage(const char *prog) {
    printf("jyfree - 极域电子教室绕过工具 (统信UOS版) v%s\n\n", JIYU_VERSION_STRING);
    printf("用法: %s [选项]\n\n", prog);
    
    printf("选项:\n");
    printf("  -i, --inject      注入 Student 并进入交互模式 (默认)\n");
    printf("  -p, --process     显示极域进程后退出\n");
    printf("  -m, --modules     显示已加载的极域库后退出\n");
    printf("  -s, --status      显示状态后退出\n");
    printf("  -c, --config FILE 加载配置文件\n");
    printf("  -d, --debug N     调试级别 (0-5)\n");
    printf("  -n, --no-watchdog 不启动看门狗\n");
    printf("  -h, --help        显示帮助\n\n");
    
    printf("目标环境 (实测):\n");
    printf("  架构      aarch64 (UOS Desktop 20 E)\n");
    printf("  进程      Student / StudentAgent / StudentService\n");
    printf("  安装路径  %s\n", JY_INSTALL_DIR);
    printf("  需要权限  与 Student 同 UID (无需 root)\n\n");
    
    printf("极域库:\n");
    printf("  %s   freerdp 引擎 (192MB, 广播/远程控制)\n", JY_LIB_CASTNG);
    printf("  %s      截屏采集 (XGetImage)\n", JY_LIB_DESK);
    printf("  %s  应用策略 (KillProcess/关窗口)\n", JY_LIB_GETAPPS);
    printf("  %s     X11 后端 (运行期 dlopen)\n\n", JY_LIB_CAST_X11);
    
    printf("监听端口:\n");
    printf("  UDP %d %d %d %d %d   TCP %d\n",
           JIYU_PORT_UDP_1, JIYU_PORT_UDP_2, JIYU_PORT_UDP_3,
           JIYU_PORT_UDP_4, JIYU_PORT_UDP_5, JIYU_PORT_TCP);
    printf("  组播 %s:%d (控制) / %d (媒体)\n\n",
           JIYU_MCAST_ADDR, JIYU_MCAST_CTRL_PORT, JIYU_MCAST_MEDIA_PORT);
    
    printf("交互命令 (注入后输入):\n");
    printf("  help              命令列表\n");
    printf("  status            注入状态\n");
    printf("  feat list         特性位列表\n");
    printf("  feat <名> on/off   开关特性\n");
    printf("  capture <模式>    pass/freeze/black/white\n");
    printf("  counters          拦截计数\n");
    printf("  modules           已加载库\n");
    printf("  quit              退出\n");
}

// ==================== 主函数 ====================
int main(int argc, char *argv[]) {
    bool do_inject = true;
    bool do_watchdog = true;
    const char *config_file = NULL;
    int mode = 0;  // 0=交互 1=process 2=modules 3=status
    
    jiyu_init_config();
    
    static struct option long_opts[] = {
        { "inject",     no_argument,       NULL, 'i' },
        { "process",    no_argument,       NULL, 'p' },
        { "modules",    no_argument,       NULL, 'm' },
        { "status",     no_argument,       NULL, 's' },
        { "config",     required_argument, NULL, 'c' },
        { "debug",      required_argument, NULL, 'd' },
        { "no-watchdog",no_argument,       NULL, 'n' },
        { "help",       no_argument,       NULL, 'h' },
        { NULL, 0, NULL, 0 }
    };
    
    int opt;
    while ((opt = getopt_long(argc, argv, "ipmsc:d:nh", long_opts, NULL)) != -1) {
        switch (opt) {
            case 'i': do_inject = true; mode = 0; break;
            case 'p': mode = 1; do_inject = false; break;
            case 'm': mode = 2; do_inject = false; break;
            case 's': mode = 3; do_inject = false; break;
            case 'c': config_file = optarg; break;
            case 'd': jiyu_debug_set_level((JiyuDebugLevel)atoi(optarg)); break;
            case 'n': do_watchdog = false; break;
            case 'h': print_usage(argv[0]); return 0;
            default:  print_usage(argv[0]); return 1;
        }
    }
    
    if (config_file) {
        jiyu_config_load(config_file);
    }
    
    jiyu_log_open(NULL);
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGPIPE, SIG_IGN);
    
    JIYU_LOG_INFO("jyfree v%s (架构: %s)", JIYU_VERSION_STRING,
#if defined(__aarch64__)
                  "aarch64"
#else
                  "非 aarch64 (符号地址可能不匹配!)"
#endif
    );
    
    // 共享内存
    if (jy_state_create() < 0) {
        JIYU_LOG_ERROR("共享内存初始化失败");
        return 1;
    }
    
    // 非交互模式
    if (mode != 0) {
        if (mode == 1) cmd_process(0, NULL);
        else if (mode == 2) cmd_modules(0, NULL);
        else if (mode == 3) cmd_status(0, NULL);
        jy_state_close();
        return 0;
    }
    
    // 查找 Student
    g_student_pid = jiyu_find_student_process();
    
    if (g_student_pid <= 0) {
        JIYU_LOG_WARN("未找到 Student 进程");
        JIYU_LOG_INFO("请在极域启动后运行, 或以 systemd 单元方式常驻");
        
        if (do_watchdog) {
            JIYU_LOG_INFO("看门狗已启动, 等待极域启动后自动注入");
            jyfree_start_watchdog();
            
            // 交互循环 (等待极域出现)
            printf("输入 help 查看命令, quit 退出\n");
            while (g_running) {
                fd_set fds;
                struct timeval tv = { 1, 0 };
                FD_ZERO(&fds);
                FD_SET(STDIN_FILENO, &fds);
                
                if (select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv) > 0) {
                    char line[256];
                    if (fgets(line, sizeof(line), stdin)) {
                        line[strcspn(line, "\n")] = '\0';
                        exec_command(line);
                    } else {
                        break;
                    }
                }
                
                if (jyfree_is_injected(jyfree_injected_pid())) {
                    g_student_pid = jyfree_injected_pid();
                } else {
                    g_student_pid = jiyu_find_student_process();
                }
            }
            jyfree_stop_watchdog();
        }
        jy_state_close();
        return 0;
    }
    
    JIYU_LOG_INFO("找到 Student: PID=%d", g_student_pid);
    
    // 注入
    if (do_inject) {
        if (jyfree_inject(g_student_pid, NULL) == 0) {
            printf("\n");
            printf("========================================\n");
            printf("  注入成功, 已安装 %u 个钩子\n", jy_state_hooks_installed());
            printf("========================================\n");
            printf("输入 help 查看命令\n\n");
        } else {
            printf("\n注入失败。排查步骤:\n");
            printf("  1. 确认同 UID:  Student uid=%d, 本工具 uid=%d\n",
                   (int)0, (int)getuid());
            printf("  2. 查看 payload 日志: cat /tmp/jyfree-payload.log\n");
            printf("  3. 若进程状态为 T, 先执行 thaw\n\n");
        }
    }
    
    if (do_watchdog) {
        jyfree_start_watchdog();
    }
    
    // 交互主循环
    printf("输入 help 查看命令, quit 退出\n");
    
    while (g_running) {
        fd_set fds;
        struct timeval tv = { 1, 0 };
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);
        
        int ret = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);
        
        if (ret > 0 && FD_ISSET(STDIN_FILENO, &fds)) {
            char line[256];
            if (fgets(line, sizeof(line), stdin)) {
                line[strcspn(line, "\n")] = '\0';
                exec_command(line);
            } else {
                // EOF (管道输入结束)
                break;
            }
        }
        
        // 同步 Student pid
        int current = jiyu_find_student_process();
        if (current != g_student_pid) {
            g_student_pid = current;
        }
    }
    
    // 清理
    JIYU_LOG_INFO("正在清理...");
    jyfree_stop_watchdog();
    
    jyfree_uninject(g_student_pid);
    
    jy_state_close();
    jiyu_log_close();
    
    printf("已退出\n");
    return 0;
}