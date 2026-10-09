#ifndef JIYU_H
#define JIYU_H

#include <stdint.h>
#include <stdbool.h>

// 共享定义 (特性位/符号地址/共享状态结构体)
#include "payload_state.h"

// ==================== 版本信息 ====================
#define JIYU_VERSION_MAJOR 2
#define JIYU_VERSION_MINOR 0
#define JIYU_VERSION_PATCH 0
#define JIYU_VERSION_STRING "2.0.0"

// ==================== 极域进程名 (UOS aarch64 实测) ====================
#define JIYU_PROCESS_STUDENT       "Student"
#define JIYU_PROCESS_STUDENT_AGENT "StudentAgent"
#define JIYU_PROCESS_SERVICE       "StudentService"
#define JIYU_PROCESS_LAUNCHER      "Launcher"

#define JIYU_SYSTEMD_UNIT "com.mythware.mcm-student.service"

// ==================== 实测网络端口 ====================
// Student 监听
#define JIYU_PORT_UDP_1       4788
#define JIYU_PORT_UDP_2       5512
#define JIYU_PORT_UDP_3       5662
#define JIYU_PORT_UDP_4       5665
#define JIYU_PORT_UDP_5       5666
#define JIYU_PORT_TCP         4806

// cast NG (freerdp) 组播
#define JIYU_MCAST_ADDR       "225.2.2.11"
#define JIYU_MCAST_CTRL_PORT  5542
#define JIYU_MCAST_MEDIA_PORT 5547

// Windows 版端口 (仅作参考, Linux 版不使用)
#define JIYU_PORT_DISCOVERY   7000
#define JIYU_PORT_DATA        7001
#define JIYU_PORT_AUDIO       7002
#define JIYU_PORT_CONTROL     7003
#define JIYU_PORT_LOG         7004

// ==================== 心跳 ====================
#define JIYU_HEARTBEAT_INTERVAL   3000
#define JIYU_HEARTBEAT_BUF_SIZE   1024

// ==================== 输入设备 ====================
#define INPUT_DEVICE_PATH "/dev/input/event"

// ==================== 调试级别 ====================
typedef enum {
    JIYU_DEBUG_NONE = 0,
    JIYU_DEBUG_ERROR,
    JIYU_DEBUG_WARN,
    JIYU_DEBUG_INFO,
    JIYU_DEBUG_VERBOSE,
    JIYU_DEBUG_TRACE
} JiyuDebugLevel;

typedef enum {
    JIYU_MODE_NORMAL = 0,
    JIYU_MODE_DEBUG,
    JIYU_MODE_VERBOSE,
    JIYU_MODE_TEST
} JiyuRunMode;

// ==================== 全局配置 ====================
typedef struct {
    JiyuDebugLevel debug_level;
    JiyuRunMode    run_mode;
    bool           enable_gui;
    bool           enable_log;
    char           log_file[256];
    int            discovery_port;
    bool           auto_reinject;    // Student 崩溃重启后自动重注入
    uint32_t       feature_mask;     // 启用的特性位
    uint32_t       capture_mode;     // 截屏处理模式
} JiyuConfig;

// ==================== 进程信息 ====================
typedef struct {
    int      pid;
    char     name[64];
    char     exe_path[512];
    char     cmdline[1024];
    char     state;              // R/S/D/T/Z
    uint64_t start_time;
    bool     is_root;
} JiyuProcessInfo;

// ==================== 全局配置 ====================
JiyuConfig* jiyu_get_config(void);
void jiyu_init_config(void);
int jiyu_config_load(const char *path);
int jiyu_config_save(const char *path);

// ==================== 日志 ====================
int  jiyu_log_open(const char *path);
void jiyu_log_close(void);

// ==================== 调试接口 ====================
void jiyu_debug_set_level(JiyuDebugLevel level);
JiyuDebugLevel jiyu_debug_get_level(void);
void jiyu_debug_print(JiyuDebugLevel level, const char *fmt, ...);
void jiyu_debug_hexdump(JiyuDebugLevel level, const char *label,
                        const void *data, size_t len);
void jiyu_debug_set_callback(void (*cb)(JiyuDebugLevel, const char *));

#define JIYU_DEBUG(level, ...) \
    do { if (jiyu_debug_get_level() >= level) jiyu_debug_print(level, __VA_ARGS__); } while(0)

#define JIYU_LOG_ERROR(...)   JIYU_DEBUG(JIYU_DEBUG_ERROR, __VA_ARGS__)
#define JIYU_LOG_WARN(...)    JIYU_DEBUG(JIYU_DEBUG_WARN, __VA_ARGS__)
#define JIYU_LOG_INFO(...)    JIYU_DEBUG(JIYU_DEBUG_INFO, __VA_ARGS__)
#define JIYU_LOG_VERBOSE(...) JIYU_DEBUG(JIYU_DEBUG_VERBOSE, __VA_ARGS__)
#define JIYU_LOG_TRACE(...)   JIYU_DEBUG(JIYU_DEBUG_TRACE, __VA_ARGS__)

// ==================== 调试命令 ====================
typedef struct {
    const char *name;
    const char *description;
    int (*handler)(int argc, char **argv);
} JiyuDebugCommand;

int  jiyu_debug_register_command(const JiyuDebugCommand *cmd);
int  jiyu_debug_execute_command(const char *cmd_line);
void jiyu_debug_list_commands(void);

// ==================== 进程 ====================
int  jiyu_find_process(void);
int  jiyu_find_student_process(void);
int  jiyu_find_student_agent(void);
int  jiyu_find_student_service(void);
bool jiyu_is_process_running(int pid);
int  jiyu_kill_process(int pid);
int  jiyu_freeze_process(int pid);
int  jiyu_thaw_process(int pid);
int  jiyu_get_process_info(int pid, JiyuProcessInfo *info);
void jiyu_list_processes(void);
const char* jiyu_get_install_dir(void);

// systemd user 集成 (RefuseManualStop=yes, 只能发信号)
int  jiyu_systemd_restart_student(void);
int  jiyu_systemd_stop_student(void);
int  jiyu_systemd_get_status(char *buf, size_t len);

// ==================== 共享内存状态 ====================
int  jy_state_create(void);
int  jy_state_open(void);
void jy_state_close(void);
JySharedState* jy_state(void);

// 特性位控制
void jy_state_set_features(uint32_t mask);
uint32_t jy_state_get_features(void);
void jy_state_set_capture_mode(uint32_t mode);
uint32_t jy_state_get_capture_mode(void);
uint64_t jy_state_get_counter(int idx);
uint64_t jy_state_get_heartbeat(void);
bool jy_state_payload_ready(void);
uint32_t jy_state_hooks_installed(void);

// ==================== 注入引擎 (控制器侧) ====================
int inject_attach(int pid);
int inject_detach(int pid);
int inject_call(int pid, uintptr_t func, int argc, uintptr_t *args, uintptr_t *ret);
int inject_dlopen(int pid, const char *so_path, uintptr_t *handle);
int inject_write_mem(int pid, uintptr_t addr, const void *data, size_t len);
int inject_read_mem(int pid, uintptr_t addr, void *data, size_t len);
uintptr_t inject_resolve_remote(int pid, const char *lib, const char *sym);
uintptr_t inject_find_got(int pid, const char *lib, const char *sym);

// ==================== 注入主流程 ====================
int  jyfree_inject(int pid, const char *payload_path);
int  jyfree_uninject(int pid);
int  jyfree_start_watchdog(void);
void jyfree_stop_watchdog(void);
bool jyfree_is_injected(int pid);
int  jyfree_injected_pid(void);

// ==================== 工具函数 ====================
uint32_t jiyu_get_timestamp_ms(void);
void     jiyu_sleep_ms(uint32_t ms);
int  jiyu_socket_create_udp(void);
int  jiyu_socket_create_tcp(void);
int  jiyu_socket_close(int fd);

#endif // JIYU_H