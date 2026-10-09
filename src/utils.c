#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <pthread.h>
#include <sys/stat.h>
#include <errno.h>
#include "jiyu.h"

// ==================== 全局配置（内部） ====================
static JiyuConfig g_config;
static FILE *g_log_file = NULL;
static void (*g_log_callback)(JiyuDebugLevel, const char *) = NULL;

// ==================== 全局配置函数 ====================
JiyuConfig* jiyu_get_config(void) {
    return &g_config;
}

void jiyu_init_config(void) {
    memset(&g_config, 0, sizeof(g_config));
    g_config.debug_level = JIYU_DEBUG_INFO;
    g_config.run_mode = JIYU_MODE_NORMAL;
    g_config.enable_gui = false;
    g_config.enable_log = true;
    g_config.discovery_port = JIYU_PORT_UDP_1;
    g_config.auto_reinject = true;
    g_config.feature_mask = JY_FEAT_ALL;
    g_config.capture_mode = JY_CAPTURE_FREEZE;
}

// ==================== 日志文件操作 ====================
static void ensure_config_dir(void) {
    const char *home = getenv("HOME");
    if (!home || !*home || home[1] == ':') {
        home = "/tmp";
    }
    
    char path[512];
    snprintf(path, sizeof(path), "%s/.config/jiyu-trainer", home);
    mkdir(path, 0755);
}

static const char* get_config_path(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home || !*home || home[1] == ':') home = "/tmp";
    snprintf(path, sizeof(path), "%s/.config/jiyu-trainer/config.json", home);
    return path;
}

static const char* get_default_log_path(void) {
    static char path[512];
    const char *home = getenv("HOME");
    if (!home || !*home || home[1] == ':') home = "/tmp";
    snprintf(path, sizeof(path), "%s/.config/jiyu-trainer/jiyu-trainer.log", home);
    return path;
}

// ==================== 配置文件读写 ====================
// 简易JSON解析（无外部依赖）
static char* json_get_value(const char *json, const char *key, char *value, size_t max_len) {
    char search[256];
    snprintf(search, sizeof(search), "\"%s\"", key);
    
    const char *pos = strstr(json, search);
    if (!pos) return NULL;
    
    pos += strlen(search);
    while (*pos == ' ' || *pos == ':') pos++;
    
    if (*pos == '"') {
        pos++;
        const char *end = strchr(pos, '"');
        if (!end) return NULL;
        size_t len = end - pos;
        if (len >= max_len) len = max_len - 1;
        strncpy(value, pos, len);
        value[len] = '\0';
    } else {
        char *end = NULL;
        long num = strtol(pos, &end, 10);
        if (end == pos) return NULL;
        snprintf(value, max_len, "%ld", num);
    }
    
    return value;
}

int jiyu_config_load(const char *path) {
    const char *config_path = path ? path : get_config_path();
    
    FILE *f = fopen(config_path, "r");
    if (!f) {
        JIYU_LOG_VERBOSE("配置文件不存在: %s，使用默认配置", config_path);
        return -1;
    }
    
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    if (size <= 0 || size > 1024 * 1024) {
        fclose(f);
        return -1;
    }
    
    char *json = (char *)malloc(size + 1);
    if (!json) {
        fclose(f);
        return -1;
    }
    
    size_t read_len = fread(json, 1, size, f);
    json[read_len] = '\0';
    fclose(f);
    
    char val[256];
    
    if (json_get_value(json, "debug_level", val, sizeof(val)))
        g_config.debug_level = (JiyuDebugLevel)atoi(val);
    if (json_get_value(json, "run_mode", val, sizeof(val)))
        g_config.run_mode = (JiyuRunMode)atoi(val);
    if (json_get_value(json, "enable_gui", val, sizeof(val)))
        g_config.enable_gui = (strcmp(val, "true") == 0);
    if (json_get_value(json, "enable_log", val, sizeof(val)))
        g_config.enable_log = (strcmp(val, "true") == 0);
    if (json_get_value(json, "discovery_port", val, sizeof(val)))
        g_config.discovery_port = atoi(val);
    if (json_get_value(json, "auto_reinject", val, sizeof(val)))
        g_config.auto_reinject = (strcmp(val, "true") == 0);
    if (json_get_value(json, "feature_mask", val, sizeof(val)))
        g_config.feature_mask = (uint32_t)strtoul(val, NULL, 0);
    if (json_get_value(json, "capture_mode", val, sizeof(val)))
        g_config.capture_mode = (uint32_t)atoi(val);
    if (json_get_value(json, "log_file", val, sizeof(val)))
        snprintf(g_config.log_file, sizeof(g_config.log_file), "%s", val);
    
    free(json);
    
    JIYU_LOG_INFO("已加载配置文件: %s", config_path);
    return 0;
}

int jiyu_config_save(const char *path) {
    const char *config_path = path ? path : get_config_path();
    
    ensure_config_dir();
    
    FILE *f = fopen(config_path, "w");
    if (!f) {
        JIYU_LOG_ERROR("无法写入配置文件: %s (%m)", config_path);
        return -1;
    }
    
    fprintf(f, "{\n");
    fprintf(f, "  \"debug_level\": %d,\n", g_config.debug_level);
    fprintf(f, "  \"run_mode\": %d,\n", g_config.run_mode);
    fprintf(f, "  \"enable_gui\": %s,\n", g_config.enable_gui ? "true" : "false");
    fprintf(f, "  \"enable_log\": %s,\n", g_config.enable_log ? "true" : "false");
    fprintf(f, "  \"discovery_port\": %d,\n", g_config.discovery_port);
    fprintf(f, "  \"auto_reinject\": %s,\n", g_config.auto_reinject ? "true" : "false");
    fprintf(f, "  \"feature_mask\": %u,\n", g_config.feature_mask);
    fprintf(f, "  \"capture_mode\": %u,\n", g_config.capture_mode);
    fprintf(f, "  \"log_file\": \"%s\"\n", g_config.log_file);
    fprintf(f, "}\n");
    
    fclose(f);
    
    JIYU_LOG_INFO("已保存配置文件: %s", config_path);
    return 0;
}

// ==================== 日志文件输出 ====================
static int g_log_file_size = 0;
#define MAX_LOG_FILE_SIZE (10 * 1024 * 1024)  // 10MB

int jiyu_log_open(const char *path) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
    
    const char *log_path = path;
    if (!log_path || !*log_path) {
        if (g_config.log_file[0]) {
            log_path = g_config.log_file;
        } else {
            log_path = get_default_log_path();
        }
    }
    
    ensure_config_dir();
    
    g_log_file = fopen(log_path, "a");
    if (!g_log_file) {
        JIYU_LOG_ERROR("无法打开日志文件: %s (%m)", log_path);
        return -1;
    }
    
    g_log_file_size = ftell(g_log_file);
    JIYU_LOG_INFO("日志文件: %s (当前%d字节)", log_path, g_log_file_size);
    return 0;
}

void jiyu_log_close(void) {
    if (g_log_file) {
        fclose(g_log_file);
        g_log_file = NULL;
    }
}

static void jiyu_log_rotate(void) {
    if (!g_log_file || g_log_file_size < MAX_LOG_FILE_SIZE) return;
    
    fclose(g_log_file);
    g_log_file = NULL;
    
    const char *log_path = g_config.log_file[0] ? g_config.log_file : get_default_log_path();
    
    char old_path[768];
    snprintf(old_path, sizeof(old_path), "%s.1", log_path);
    rename(log_path, old_path);
    
    g_log_file = fopen(log_path, "a");
    g_log_file_size = 0;
}

static void jiyu_log_write(const char *msg) {
    if (!g_log_file) return;
    
    fputs(msg, g_log_file);
    fputc('\n', g_log_file);
    fflush(g_log_file);
    
    g_log_file_size += strlen(msg) + 1;
    jiyu_log_rotate();
}

// ==================== 调试接口实现 ====================
void jiyu_debug_set_level(JiyuDebugLevel level) {
    g_config.debug_level = level;
}

JiyuDebugLevel jiyu_debug_get_level(void) {
    return g_config.debug_level;
}

void jiyu_debug_set_callback(void (*callback)(JiyuDebugLevel, const char *)) {
    g_log_callback = callback;
}

void jiyu_debug_print(JiyuDebugLevel level, const char *fmt, ...) {
    if (level > g_config.debug_level) return;
    
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm *tm_info = localtime(&tv.tv_sec);
    
    const char *level_str;
    switch (level) {
        case JIYU_DEBUG_ERROR:   level_str = "ERROR"; break;
        case JIYU_DEBUG_WARN:    level_str = "WARN "; break;
        case JIYU_DEBUG_INFO:    level_str = "INFO "; break;
        case JIYU_DEBUG_VERBOSE: level_str = "VERB "; break;
        case JIYU_DEBUG_TRACE:   level_str = "TRACE"; break;
        default:                 level_str = "?????"; break;
    }
    
    char msg[2048];
    int prefix_len = snprintf(msg, sizeof(msg), "[%02d:%02d:%02d.%03ld] [%s] ",
            tm_info->tm_hour, tm_info->tm_min, tm_info->tm_sec,
            tv.tv_usec / 1000, level_str);
    
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg + prefix_len, sizeof(msg) - prefix_len, fmt, args);
    va_end(args);
    
    fprintf(stderr, "%s\n", msg);
    fflush(stderr);
    
    jiyu_log_write(msg);
    
    // 调用日志回调
    if (g_log_callback) {
        g_log_callback(level, msg);
    }
}

void jiyu_debug_hexdump(JiyuDebugLevel level, const char *label, 
                        const void *data, size_t len) {
    if (level > g_config.debug_level) return;
    
    const uint8_t *bytes = (const uint8_t *)data;
    
    jiyu_debug_print(level, "%s (%zu bytes):", label, len);
    
    for (size_t i = 0; i < len; i += 16) {
        char hex_str[64] = {0};
        char ascii_str[20] = {0};
        
        for (size_t j = 0; j < 16 && (i + j) < len; j++) {
            size_t pos = j * 3;
            if (pos < sizeof(hex_str) - 3) {
                snprintf(hex_str + pos, sizeof(hex_str) - pos, "%02X ", bytes[i + j]);
            }
            
            if (bytes[i + j] >= 32 && bytes[i + j] < 127) {
                ascii_str[j] = bytes[i + j];
            } else {
                ascii_str[j] = '.';
            }
        }
        
        jiyu_debug_print(level, "  %04zX: %-48s |%s|", i, hex_str, ascii_str);
    }
}

// ==================== 调试命令系统 ====================
#define MAX_DEBUG_COMMANDS 32
static JiyuDebugCommand g_debug_commands[MAX_DEBUG_COMMANDS];
static int g_debug_command_count = 0;

int jiyu_debug_register_command(const JiyuDebugCommand *cmd) {
    if (g_debug_command_count >= MAX_DEBUG_COMMANDS) {
        JIYU_LOG_WARN("调试命令数量已达上限: %d", MAX_DEBUG_COMMANDS);
        return -1;
    }
    
    g_debug_commands[g_debug_command_count] = *cmd;
    g_debug_command_count++;
    return 0;
}

int jiyu_debug_execute_command(const char *cmd_line) {
    if (!cmd_line || !*cmd_line) return -1;
    
    char cmd_name[64] = {0};
    const char *space = strchr(cmd_line, ' ');
    if (space) {
        size_t len = space - cmd_line;
        if (len >= sizeof(cmd_name)) len = sizeof(cmd_name) - 1;
        strncpy(cmd_name, cmd_line, len);
    } else {
        strncpy(cmd_name, cmd_line, sizeof(cmd_name) - 1);
    }
    
    for (int i = 0; i < g_debug_command_count; i++) {
        if (strcmp(g_debug_commands[i].name, cmd_name) == 0) {
            char *argv[16] = {0};
            int argc = 0;
            
            char buf[1024];
            strncpy(buf, cmd_line, sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            
            char *token = strtok(buf, " ");
            while (token && argc < 16) {
                argv[argc++] = token;
                token = strtok(NULL, " ");
            }
            
            return g_debug_commands[i].handler(argc, argv);
        }
    }
    
    JIYU_LOG_ERROR("未知命令: %s (输入help查看可用命令)", cmd_name);
    return -1;
}

void jiyu_debug_list_commands(void) {
    JIYU_LOG_INFO("可用调试命令:");
    for (int i = 0; i < g_debug_command_count; i++) {
        JIYU_LOG_INFO("  %-16s - %s", 
                     g_debug_commands[i].name,
                     g_debug_commands[i].description);
    }
}

// ==================== 兼容旧接口 ====================
void log_info(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    JIYU_LOG_INFO("%s", msg);
}

void log_error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    JIYU_LOG_ERROR("%s", msg);
}

void log_debug(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    char msg[1024];
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    JIYU_LOG_VERBOSE("%s", msg);
}

// ==================== 时间相关函数 ====================
uint32_t jiyu_get_timestamp_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}

void jiyu_sleep_ms(uint32_t ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000;
    nanosleep(&ts, NULL);
}

// ==================== 网络相关函数 ====================
int jiyu_socket_create_udp(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        JIYU_LOG_ERROR("创建UDP套接字失败: %s", strerror(errno));
        return -1;
    }
    JIYU_LOG_VERBOSE("创建UDP套接字: fd=%d", sock);
    return sock;
}

int jiyu_socket_create_tcp(void) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        JIYU_LOG_ERROR("创建TCP套接字失败: %s", strerror(errno));
        return -1;
    }
    JIYU_LOG_VERBOSE("创建TCP套接字: fd=%d", sock);
    return sock;
}

int jiyu_socket_close(int fd) {
    if (fd < 0) return -1;
    JIYU_LOG_VERBOSE("关闭套接字: fd=%d", fd);
    return close(fd);
}
