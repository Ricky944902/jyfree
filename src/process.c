#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include "jiyu.h"

// ==================== 内部状态 ====================
static int g_student_pid = -1;
static int g_student_agent_pid = -1;
static int g_student_service_pid = -1;

// ==================== 工具函数 ====================
static bool read_file_to_buf(const char *path, char *buf, size_t buf_size) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    
    ssize_t n = read(fd, buf, buf_size - 1);
    close(fd);
    
    if (n <= 0) return false;
    buf[n] = '\0';
    return true;
}

static char parse_state_from_status(const char *status_buf) {
    const char *state_str = strstr(status_buf, "State:");
    if (!state_str) return 0;
    state_str += 6;
    while (*state_str && isspace(*state_str)) state_str++;
    return *state_str;
}

static uint64_t parse_starttime_from_stat(const char *stat_buf) {
    // stat格式: pid (comm) state ppid ... starttime(22)
    char *copy = strdup(stat_buf);
    if (!copy) return 0;
    
    char *saveptr = NULL;
    char *token = strtok_r(copy, " ", &saveptr);
    for (int i = 0; i < 21 && token; i++) {
        token = strtok_r(NULL, " ", &saveptr);
    }
    
    uint64_t starttime = 0;
    if (token) starttime = strtoull(token, NULL, 10);
    free(copy);
    return starttime;
}

// ==================== 进程扫描 ====================
static int scan_for_process(const char *target_name, bool exact_match) {
    DIR *proc = opendir("/proc");
    if (!proc) return -1;
    
    struct dirent *entry;
    int found_pid = -1;
    
    while ((entry = readdir(proc)) != NULL) {
        if (!isdigit(entry->d_name[0])) continue;
        
        int pid = atoi(entry->d_name);
        if (pid <= 0) continue;
        
        char status_path[256];
        snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
        
        char status_buf[1024];
        if (!read_file_to_buf(status_path, status_buf, sizeof(status_buf))) continue;
        
        // 提取进程名
        const char *name_start = strstr(status_buf, "Name:\t");
        if (!name_start) continue;
        name_start += 6;
        
        const char *name_end = strchr(name_start, '\n');
        if (!name_end) continue;
        
        char proc_name[256];
        size_t len = name_end - name_start;
        if (len >= sizeof(proc_name)) len = sizeof(proc_name) - 1;
        strncpy(proc_name, name_start, len);
        proc_name[len] = '\0';
        
        // 匹配
        bool match = false;
        if (exact_match) {
            match = (strcmp(proc_name, target_name) == 0);
        } else {
            match = (strstr(proc_name, target_name) != NULL);
        }
        
        if (match) {
            found_pid = pid;
            break;
        }
    }
    
    closedir(proc);
    return found_pid;
}

// ==================== 公共接口 ====================

// 查找主程序 Student
int jiyu_find_student_process(void) {
    g_student_pid = scan_for_process(JIYU_PROCESS_STUDENT, true);
    if (g_student_pid > 0) {
        JIYU_LOG_INFO("找到 Student 进程: PID=%d", g_student_pid);
    }
    return g_student_pid;
}

// 查找看门狗 StudentAgent
int jiyu_find_student_agent(void) {
    g_student_agent_pid = scan_for_process(JIYU_PROCESS_STUDENT_AGENT, true);
    if (g_student_agent_pid > 0) {
        JIYU_LOG_INFO("找到 StudentAgent 进程: PID=%d", g_student_agent_pid);
    }
    return g_student_agent_pid;
}

// 查找 root 服务 StudentService
int jiyu_find_student_service(void) {
    g_student_service_pid = scan_for_process(JIYU_PROCESS_SERVICE, true);
    if (g_student_service_pid > 0) {
        JIYU_LOG_INFO("找到 StudentService 进程: PID=%d", g_student_service_pid);
    }
    return g_student_service_pid;
}

// 综合查找所有极域进程
int jiyu_find_process(void) {
    // 优先返回 Student 主进程
    int pid = jiyu_find_student_process();
    if (pid > 0) return pid;
    
    // 兼容旧版本进程名
    const char *legacy_names[] = {
        "studentmain", "mythware-student", "StudentMain.exe", "StudentMain"
    };
    
    for (size_t i = 0; i < sizeof(legacy_names)/sizeof(legacy_names[0]); i++) {
        pid = scan_for_process(legacy_names[i], true);
        if (pid > 0) return pid;
    }
    
    return -1;
}

bool jiyu_is_process_running(int pid) {
    if (pid <= 0) return false;
    return kill(pid, 0) == 0;
}

int jiyu_kill_process(int pid) {
    if (pid <= 0) return -1;
    
    JIYU_LOG_INFO("正在结束进程: PID=%d", pid);
    
    // 先尝试优雅终止
    if (kill(pid, SIGTERM) == 0) {
        for (int i = 0; i < 10; i++) {
            usleep(100000);
            if (!jiyu_is_process_running(pid)) {
                JIYU_LOG_INFO("进程已正常退出: PID=%d", pid);
                return 0;
            }
        }
    }
    
    // 强制杀死
    if (kill(pid, SIGKILL) == 0) {
        JIYU_LOG_WARN("强制杀死进程: PID=%d", pid);
        return 0;
    }
    
    JIYU_LOG_ERROR("无法杀死进程: PID=%d (%m)", pid);
    return -1;
}

int jiyu_freeze_process(int pid) {
    if (pid <= 0) return -1;
    JIYU_LOG_INFO("冻结进程 (SIGSTOP): PID=%d", pid);
    return kill(pid, SIGSTOP);
}

int jiyu_thaw_process(int pid) {
    if (pid <= 0) return -1;
    JIYU_LOG_INFO("恢复进程 (SIGCONT): PID=%d", pid);
    return kill(pid, SIGCONT);
}

int jiyu_get_process_info(int pid, JiyuProcessInfo *info) {
    if (pid <= 0 || !info) return -1;
    
    memset(info, 0, sizeof(JiyuProcessInfo));
    info->pid = pid;
    
    // 判断是否 root: /proc/<pid>/status 的 Uid 行第一个是有效 UID
    char status_path[256];
    snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
    char status_buf[1024];
    if (read_file_to_buf(status_path, status_buf, sizeof(status_buf))) {
        const char *uid_line = strstr(status_buf, "Uid:");
        if (uid_line) {
            uid_line += 4;
            unsigned int ruid = 0, euid = 0;
            if (sscanf(uid_line, "%u %u", &ruid, &euid) >= 1) {
                info->is_root = (euid == 0 || ruid == 0);
            }
        }
    }
    
    // 读取 exe 路径
    char exe_path[512];
    snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", pid);
    ssize_t len = readlink(exe_path, info->exe_path, sizeof(info->exe_path) - 1);
    if (len > 0) info->exe_path[len] = '\0';
    
    // 读取 cmdline
    char cmdline_path[256];
    snprintf(cmdline_path, sizeof(cmdline_path), "/proc/%d/cmdline", pid);
    char cmdline_buf[1024];
    if (read_file_to_buf(cmdline_path, cmdline_buf, sizeof(cmdline_buf))) {
        for (size_t i = 0; i < strlen(cmdline_buf); i++) {
            if (cmdline_buf[i] == '\0') cmdline_buf[i] = ' ';
        }
        strncpy(info->cmdline, cmdline_buf, sizeof(info->cmdline) - 1);
    }
    
    // 从 status 取进程名与状态
    if (read_file_to_buf(status_path, status_buf, sizeof(status_buf))) {
        info->state = parse_state_from_status(status_buf);
        
        const char *name_start = strstr(status_buf, "Name:\t");
        if (name_start) {
            name_start += 6;
            const char *name_end = strchr(name_start, '\n');
            if (name_end) {
                size_t nlen = name_end - name_start;
                if (nlen >= sizeof(info->name)) nlen = sizeof(info->name) - 1;
                strncpy(info->name, name_start, nlen);
                info->name[nlen] = '\0';
            }
        }
    }
    
    // 读取 stat 获取启动时间
    char stat_path[256];
    snprintf(stat_path, sizeof(stat_path), "/proc/%d/stat", pid);
    char stat_buf[2048];
    if (read_file_to_buf(stat_path, stat_buf, sizeof(stat_buf))) {
        info->start_time = parse_starttime_from_stat(stat_buf);
    }
    
    return 0;
}

void jiyu_list_processes(void) {
    JIYU_LOG_INFO("=== 极域相关进程 ===");
    
    JiyuProcessInfo info;
    
    // Student
    if (g_student_pid > 0 && jiyu_is_process_running(g_student_pid)) {
        jiyu_get_process_info(g_student_pid, &info);
        JIYU_LOG_INFO("  Student:       PID=%d, 状态=%c, 启动=%llu", 
                     info.pid, info.state, info.start_time);
    } else {
        JIYU_LOG_INFO("  Student:       未运行");
    }
    
    // StudentAgent
    if (g_student_agent_pid > 0 && jiyu_is_process_running(g_student_agent_pid)) {
        jiyu_get_process_info(g_student_agent_pid, &info);
        JIYU_LOG_INFO("  StudentAgent:  PID=%d, 状态=%c, 启动=%llu", 
                     info.pid, info.state, info.start_time);
    } else {
        JIYU_LOG_INFO("  StudentAgent:  未运行");
    }
    
    // StudentService
    if (g_student_service_pid > 0 && jiyu_is_process_running(g_student_service_pid)) {
        jiyu_get_process_info(g_student_service_pid, &info);
        JIYU_LOG_INFO("  StudentService: PID=%d, 状态=%c (root)", 
                     info.pid, info.state);
    } else {
        JIYU_LOG_INFO("  StudentService: 未运行");
    }
    
    // Launcher
    int launcher_pid = scan_for_process(JIYU_PROCESS_LAUNCHER, true);
    if (launcher_pid > 0 && jiyu_is_process_running(launcher_pid)) {
        jiyu_get_process_info(launcher_pid, &info);
        JIYU_LOG_INFO("  Launcher(setuid): PID=%d, 状态=%c", 
                     info.pid, info.state);
    }
}

// ==================== systemd user 集成 ====================
static int run_systemctl_user(const char *action, char *output, size_t out_size) {
    char cmd[512];
    snprintf(cmd, sizeof(cmd), "systemctl --user %s com.mythware.mcm-student.service 2>&1", action);
    
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    
    if (output && out_size > 0) {
        size_t n = fread(output, 1, out_size - 1, fp);
        output[n] = '\0';
    }
    
    return pclose(fp);
}

int jiyu_systemd_restart_student(void) {
    JIYU_LOG_INFO("systemd 重启 Student 服务");
    return run_systemctl_user("restart", NULL, 0);
}

int jiyu_systemd_stop_student(void) {
    JIYU_LOG_INFO("systemd 停止 Student 服务");
    return run_systemctl_user("stop", NULL, 0);
}

int jiyu_systemd_get_status(char *buf, size_t len) {
    return run_systemctl_user("status", buf, len);
}

// ==================== 查找安装目录 ====================
const char* jiyu_get_install_dir(void) {
    static char path[512];
    
    // 1. 通过 Student 进程 exe 路径
    if (g_student_pid > 0) {
        char exe_path[512];
        snprintf(exe_path, sizeof(exe_path), "/proc/%d/exe", g_student_pid);
        ssize_t n = readlink(exe_path, path, sizeof(path) - 1);
        if (n > 0) {
            path[n] = '\0';
            char *last_slash = strrchr(path, '/');
            if (last_slash) *last_slash = '\0';
            return path;
        }
    }
    
    // 2. 默认路径
    return JY_INSTALL_DIR;
}