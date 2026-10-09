// ============================================================
// inject.c - 控制器侧注入引擎
//
// 职责:
//   1. PTRACE_ATTACH/DETACH 到 Student (同 UID, 无需 root)
//   2. 远程 mmap 分配内存
//   3. 远程写内存 / 读内存
//   4. 远程调用函数 (在目标 pc 种 brk #0 陷阱, 用 LR 作为返回地址)
//   5. 远程 dlopen 加载 libjyfree.so (payload 自装钩子)
//   6. 看门狗线程: Student 被 systemd 重启后自动重注入
//
// 架构: aarch64 (Student 是 ET_EXEC 非 PIE, nm 地址即运行地址)
// ============================================================

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <link.h>
#include <elf.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/uio.h>
#include <sys/user.h>
#include <sys/stat.h>
#include "jiyu.h"
#include "elf_sym.h"

// ==================== 内部状态 ====================
static int g_attached_pid = -1;
static struct user_regs_struct g_saved_regs;
static bool g_attached = false;

// 看门狗
static volatile int g_watchdog_running = 0;
static pthread_t g_watchdog_tid;
static int g_injected_pid = -1;
static uint64_t g_injected_starttime = 0;
static uintptr_t g_payload_handle = 0;   // payload 的 dlopen 句柄 (uninject 要用)

// ==================== 寄存器访问抽象 ====================
// 目标平台是 aarch64 (Student 所在架构)。控制器的编译架构可能不同,
// 因此用条件编译隔离寄存器布局差异。
//
// 注意: aarch64 的 sys/user.h 里 struct user_regs_struct 定义为
//       unsigned long long regs[31]; unsigned long long sp, pc, pstate;
//       部分 glibc 版本没有命名的 x0..x30 字段, 所以统一用 regs[] 下标,
//       这在任何版本上都成立。
#if defined(__aarch64__)
  #define REG_PC      pc
  #define REG_SP      sp
  #define REG_X(n)    regs[n]
  #define REG_RET     regs[0]
  #define REG_LR      regs[30]
  #define ARCH_NAME   "aarch64"
#elif defined(__x86_64__)
  #define REG_PC      rip
  #define REG_SP      rsp
  #define REG_X(n)    r##n
  #define REG_RET     rax
  #define REG_LR      rip
  #define ARCH_NAME   "x86_64"
#else
#error "需要 aarch64 或 x86_64 架构"
#endif

// ==================== 内部辅助 ====================
static inline uintptr_t pc_of(const struct user_regs_struct *r) {
    return (uintptr_t)r->REG_PC;
}

static int read_remote(int pid, uintptr_t addr, void *buf, size_t len) {
    struct iovec l = { .iov_base = buf, .iov_len = len };
    struct iovec r = { .iov_base = (void *)addr, .iov_len = len };
    ssize_t n = process_vm_readv(pid, &l, 1, &r, 1, 0);
    if (n != (ssize_t)len) return -1;
    return 0;
}

static int write_remote(int pid, uintptr_t addr, const void *buf, size_t len) {
    struct iovec l = { .iov_base = (void *)buf, .iov_len = len };
    struct iovec r = { .iov_base = (void *)addr, .iov_len = len };
    ssize_t n = process_vm_writev(pid, &l, 1, &r, 1, 0);
    if (n != (ssize_t)len) return -1;
    return 0;
}

int inject_read_mem(int pid, uintptr_t addr, void *data, size_t len) {
    if (read_remote(pid, addr, data, len) < 0) {
        JIYU_LOG_ERROR("读远程内存失败: pid=%d addr=0x%lx len=%zu (%s)",
                       pid, (unsigned long)addr, len, strerror(errno));
        return -1;
    }
    return 0;
}

int inject_write_mem(int pid, uintptr_t addr, const void *data, size_t len) {
    if (write_remote(pid, addr, data, len) < 0) {
        JIYU_LOG_ERROR("写远程内存失败: pid=%d addr=0x%lx len=%zu (%s)",
                       pid, (unsigned long)addr, len, strerror(errno));
        return -1;
    }
    return 0;
}

// ==================== attach / detach ====================
int inject_attach(int pid) {
    if (pid <= 0) return -1;
    if (g_attached && g_attached_pid == pid) return 0;
    if (g_attached && g_attached_pid != pid) inject_detach(g_attached_pid);
    
    if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
        if (errno == EPERM) {
            JIYU_LOG_ERROR("PTRACE_ATTACH 被拒绝 (pid=%d): 需要同 UID 运行", pid);
        } else {
            JIYU_LOG_ERROR("PTRACE_ATTACH 失败 (pid=%d): %s", pid, strerror(errno));
        }
        return -1;
    }
    
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        JIYU_LOG_ERROR("waitpid 失败: %s", strerror(errno));
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return -1;
    }
    if (!WIFSTOPPED(status)) {
        JIYU_LOG_ERROR("目标进程未进入停止态: pid=%d", pid);
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return -1;
    }
    
    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &g_saved_regs) < 0) {
        JIYU_LOG_ERROR("GETREGSET 失败: %s", strerror(errno));
        ptrace(PTRACE_DETACH, pid, NULL, NULL);
        return -1;
    }
    
    g_attached = true;
    g_attached_pid = pid;
    JIYU_LOG_INFO("已附加 Student: pid=%d (原 pc=0x%lx)",
                  pid, (unsigned long)pc_of(&g_saved_regs));
    return 0;
}

int inject_detach(int pid) {
    if (!g_attached || g_attached_pid != pid) return 0;
    
    // 还原寄存器, 确保目标从原位置继续
    ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &g_saved_regs);
    
    if (ptrace(PTRACE_DETACH, pid, NULL, NULL) < 0) {
        JIYU_LOG_WARN("PTRACE_DETACH 异常: %s", strerror(errno));
    }
    
    g_attached = false;
    g_attached_pid = -1;
    JIYU_LOG_INFO("已分离 Student: pid=%d", pid);
    return 0;
}

// ==================== 远程函数调用 ====================
// ==================== 远程栈 (scratch stack) ====================
// 不能复用被注入线程自己的栈:
//   1. 目标可能正好在栈浅处 (信号处理、线程刚启动)
//   2. 被调函数 (如 dlopen) 栈需求远超几字节, 会冲掉我们放在栈上的 brk 陷阱
//   3. 会污染目标线程的栈帧
// 改为在目标进程里 mmap 一块独立区域当栈, 每次 attach 分配一次并复用。
#define SCRATCH_STACK_SIZE (2UL * 1024 * 1024)   // 2MB, 对 dlopen 绰绰有余

static uintptr_t g_scratch = 0;      // 栈底 (低地址)
static size_t    g_scratch_size = 0;
static int       g_scratch_pid = -1;

// 在目标进程分配一块内存 (不依赖 inject_call)
static int remote_mmap_raw(int pid, size_t size, uintptr_t *out) {
    uintptr_t fn = inject_resolve_remote(pid, "libc.so", "mmap");
    if (!fn) return -1;
    
    // mmap 自身几乎不用栈, 可以直接借用目标当前 SP 下方少量空间
    struct user_regs_struct regs;
    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &regs) < 0) return -1;
    
    uintptr_t args[6] = { 0, size, PROT_READ | PROT_WRITE,
                          MAP_PRIVATE | MAP_ANONYMOUS, (uintptr_t)-1, 0 };
    struct user_regs_struct r = regs;
    uintptr_t sp;
    
#if defined(__aarch64__)
    sp = regs.REG_SP - 128;
    sp &= ~0xFul;
    
    uint32_t brk0 = 0xd4200000;
    if (write_remote(pid, sp, &brk0, 4) < 0) return -1;
    
    for (int i = 0; i < 6; i++) r.regs[i] = args[i];
    r.REG_PC = fn;
    r.REG_SP = sp;
    r.REG_LR = sp;
    
#elif defined(__x86_64__)
    unsigned long long *argp[6] = {
        (unsigned long long *)&r.rdi, (unsigned long long *)&r.rsi,
        (unsigned long long *)&r.rdx, (unsigned long long *)&r.rcx,
        (unsigned long long *)&r.r8,  (unsigned long long *)&r.r9
    };
    for (int i = 0; i < 6; i++) *argp[i] = (unsigned long long)args[i];
    
    sp = r.rsp - 64;
    r.rsp = sp;
    r.rip = fn;
#else
    return -1;
#endif
    
    if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &r) < 0) return -1;
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) return -1;
    
    int status;
    if (waitpid(pid, &status, 0) < 0) return -1;
    if (!WIFSTOPPED(status) || WSTOPSIG(status) != SIGTRAP) return -1;
    
    struct user_regs_struct after;
    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &after) < 0) return -1;
    
    uintptr_t ret = (uintptr_t)after.REG_RET;
    if (ret == (uintptr_t)-1 || ret == 0) return -1;
    
    *out = ret;
    return 0;
}

// 确保目标进程有可用的远程栈
static int ensure_scratch(int pid) {
    if (g_scratch && g_scratch_pid == pid) return 0;
    
    // 目标变了, 旧的远程内存放弃回收 (跨进程无法安全 munmap)
    g_scratch = 0;
    g_scratch_size = 0;
    g_scratch_pid = -1;
    
    uintptr_t mem = 0;
    if (remote_mmap_raw(pid, SCRATCH_STACK_SIZE, &mem) < 0) {
        JIYU_LOG_ERROR("无法分配远程栈");
        return -1;
    }
    
    g_scratch = mem;
    g_scratch_size = SCRATCH_STACK_SIZE;
    g_scratch_pid = pid;
    
    JIYU_LOG_VERBOSE("远程栈已分配: 0x%lx (%lu KB)",
                     (unsigned long)mem, SCRATCH_STACK_SIZE / 1024);
    return 0;
}

int inject_call(int pid, uintptr_t func, int argc, uintptr_t *args, uintptr_t *ret) {
    if (!g_attached || g_attached_pid != pid) {
        JIYU_LOG_ERROR("inject_call: 未附加到 pid=%d", pid);
        return -1;
    }
    
    if (ensure_scratch(pid) < 0) return -1;
    
    struct user_regs_struct saved;
    memcpy(&saved, &g_saved_regs, sizeof(saved));
    
    struct user_regs_struct regs = saved;
    
#if defined(__aarch64__)
    // aarch64 调用约定: x0-x7 传参, x30(LR) 为返回地址
    // 用独立远程栈, 栈顶放 brk #0 作为返回陷阱
    uintptr_t sp = g_scratch + g_scratch_size;
    sp &= ~0xFul;                    // 16 字节对齐 (aarch64 BTI 要求)
    sp -= 16;                         // 留出返回地址位置
    
    uint32_t brk0 = 0xd4200000;       // brk #0
    if (write_remote(pid, sp, &brk0, 4) < 0) {
        JIYU_LOG_ERROR("inject_call: 写 brk 指令失败");
        return -1;
    }
    
    uintptr_t trap_addr = sp;
    
    for (int i = 0; i < 8; i++) {
        regs.regs[i] = (i < argc) ? args[i] : 0;
    }
    
    regs.REG_PC = func;
    regs.REG_SP = sp;
    regs.REG_LR = trap_addr;
    
#elif defined(__x86_64__)
    // x86_64 调用约定: rdi rsi rdx rcx r8 r9, 其余走栈
    uintptr_t sp = g_scratch + g_scratch_size;
    sp &= ~0xFull;
    
    // 栈上参数 (第7个起), 从高地址往低放
    if (argc > 6) {
        sp -= (argc - 6) * 8;
        if (write_remote(pid, sp, &args[6], (argc - 6) * 8) < 0) {
            JIYU_LOG_ERROR("inject_call: 写栈参数失败");
            return -1;
        }
    }
    
    // 返回地址压栈 (指向 int3)
    uintptr_t trap_addr = sp - 16;
    uint8_t int3 = 0xCC;
    if (write_remote(pid, trap_addr, &int3, 1) < 0) {
        JIYU_LOG_ERROR("inject_call: 写 int3 失败");
        return -1;
    }
    uintptr_t ret_addr = trap_addr;
    if (write_remote(pid, trap_addr + 8, &ret_addr, 8) < 0) {
        JIYU_LOG_ERROR("inject_call: 写返回地址失败");
        return -1;
    }
    
    // 参数寄存器
    unsigned long long *argp[6] = {
        (unsigned long long *)&regs.rdi, (unsigned long long *)&regs.rsi,
        (unsigned long long *)&regs.rdx, (unsigned long long *)&regs.rcx,
        (unsigned long long *)&regs.r8,  (unsigned long long *)&regs.r9
    };
    for (int i = 0; i < 6; i++) {
        *argp[i] = (unsigned long long)((i < argc) ? args[i] : 0);
    }
    
    regs.rsp = trap_addr + 8;          // 栈顶 = 返回地址之后
    regs.rip = func;
#endif
    
    if (ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &regs) < 0) {
        JIYU_LOG_ERROR("inject_call: SETREGSET 失败: %s", strerror(errno));
        return -1;
    }
    
    if (ptrace(PTRACE_CONT, pid, NULL, NULL) < 0) {
        JIYU_LOG_ERROR("inject_call: PTRACE_CONT 失败: %s", strerror(errno));
        return -1;
    }
    
    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        JIYU_LOG_ERROR("inject_call: waitpid 失败: %s", strerror(errno));
        return -1;
    }
    
    if (WIFSIGNALED(status)) {
        JIYU_LOG_ERROR("inject_call: 目标被信号 %d 杀死 (远程调用破坏了栈?)",
                       WTERMSIG(status));
        return -1;
    }
    
    if (!WIFSTOPPED(status)) {
        JIYU_LOG_ERROR("inject_call: 非预期停止 status=0x%x", status);
        return -1;
    }
    
    int stopsig = WSTOPSIG(status);
    if (stopsig != SIGTRAP) {
        JIYU_LOG_WARN("inject_call: 停止信号为 %d (非 SIGTRAP)", stopsig);
        // 把信号交还目标自行处理
        ptrace(PTRACE_SETREGSET, pid, NT_PRSTATUS, &saved);
        ptrace(PTRACE_CONT, pid, NULL, (void *)(long)stopsig);
        waitpid(pid, &status, 0);
        return -1;
    }
    
    struct user_regs_struct after;
    if (ptrace(PTRACE_GETREGSET, pid, NT_PRSTATUS, &after) < 0) {
        return -1;
    }
    
    if (ret) {
        *ret = (uintptr_t)after.REG_RET;
    }
    
    JIYU_LOG_TRACE("inject_call 完成: func=0x%lx -> ret=0x%lx",
                   (unsigned long)func, (unsigned long)after.REG_RET);
    
    // 还原到调用前的状态 (目标从原位置继续)
    memcpy(&g_saved_regs, &saved, sizeof(saved));
    return 0;
}

// ==================== 远程符号解析 ====================
// 取目标进程中某库的加载基址
static uintptr_t remote_module_base(int pid, const char *needle) {
    // 目标进程的 maps
    char path[64];
    snprintf(path, sizeof(path), "/proc/%d/maps", pid);
    FILE *fp = fopen(path, "r");
    if (!fp) return 0;
    
    char line[1024];
    uintptr_t lo = 0;
    int found = 0;
    
    while (fgets(line, sizeof(line), fp)) {
        if (!strstr(line, needle)) continue;
        
        uintptr_t a, b;
        if (sscanf(line, "%lx-%lx", &a, &b) != 2) continue;
        (void)b;
        
        // 取该库最小的映射地址作为基址
        if (!found || a < lo) lo = a;
        found = 1;
    }
    fclose(fp);
    
    return found ? lo : 0;
}

// 解析目标进程中某库某符号的运行地址
//
// 不用 dlopen/dlsym 的原因:
//   1. 命令行版默认静态链接 (STATIC=1), 静态二进制里 dlopen 不可用
//   2. 交叉编译时控制器所在机器未必有目标架构的库文件
//   3. 直接解析 ELF 得到的偏移就是真实加载偏移, 更准确
uintptr_t inject_resolve_remote(int pid, const char *lib, const char *sym) {
    // 目标进程里该库的加载基址
    uintptr_t rbase = remote_module_base(pid, lib);
    if (!rbase) {
        JIYU_LOG_ERROR("目标进程未加载 %s", lib);
        return 0;
    }
    
    // 从磁盘上的 ELF 文件取符号偏移
    static const char *multiarch[] = {
        "/lib/aarch64-linux-gnu", "/usr/lib/aarch64-linux-gnu",
        "/lib/x86_64-linux-gnu",  "/usr/lib/x86_64-linux-gnu",
        "/usr/lib64", "/lib64"
    };
    
    char paths[16][512];
    int np = elf_candidate_paths(lib, jiyu_get_install_dir(),
                                 multiarch,
                                 (int)(sizeof(multiarch)/sizeof(multiarch[0])),
                                 paths, 16);
    
    char found_path[512] = {0};
    int off = elf_find_symbol((const char *const *)paths, np, sym,
                              found_path, sizeof(found_path));
    
    if (off <= 0) {
        JIYU_LOG_ERROR("本地 ELF 中未找到符号 %s::%s", lib, sym);
        JIYU_LOG_ERROR("  尝试过的路径:");
        for (int i = 0; i < np && i < 5; i++) {
            JIYU_LOG_ERROR("    %s", paths[i]);
        }
        return 0;
    }
    
    uintptr_t remote = rbase + (uintptr_t)off;
    
    JIYU_LOG_VERBOSE("符号解析 %s::%s: %s offset=0x%x -> remote=0x%lx",
                     lib, sym, found_path, off, (unsigned long)remote);
    return remote;
}

// GOT 解析由 payload 内部 dl_iterate_phdr 完成, 控制器不需要
uintptr_t inject_find_got(int pid, const char *lib, const char *sym) {
    (void)pid; (void)lib; (void)sym;
    return 0;
}

// ==================== 远程 dlopen ====================
int inject_dlopen(int pid, const char *so_path, uintptr_t *handle) {
    // 解析目标进程的 dlopen
    uintptr_t fn = inject_resolve_remote(pid, "libdl.so", "dlopen");
    if (!fn) {
        // 某些系统 dlopen 在 libc 里
        fn = inject_resolve_remote(pid, "libc.so", "dlopen");
    }
    if (!fn) {
        JIYU_LOG_ERROR("无法解析目标进程 dlopen");
        return -1;
    }
    
    // 在目标进程分配内存存放路径
    size_t plen = strlen(so_path) + 1;
    uintptr_t mfn = inject_resolve_remote(pid, "libc.so", "mmap");
    if (!mfn) {
        JIYU_LOG_ERROR("无法解析目标进程 mmap");
        return -1;
    }
    
    uintptr_t margs[6] = {
        0,                                    // addr
        ((plen + 0xFFF) & ~0xFFFUL),          // len (页对齐)
        PROT_READ | PROT_WRITE,               // prot
        MAP_PRIVATE | MAP_ANONYMOUS,          // flags
        (uintptr_t)-1,                         // fd
        0                                      // offset
    };
    uintptr_t mem = 0;
    if (inject_call(pid, mfn, 6, margs, &mem) < 0) {
        JIYU_LOG_ERROR("远程 mmap 失败");
        return -1;
    }
    if (mem == (uintptr_t)-1 || !mem) {
        JIYU_LOG_ERROR("远程 mmap 返回异常: 0x%lx", (unsigned long)mem);
        return -1;
    }
    
    if (inject_write_mem(pid, mem, so_path, plen) < 0) {
        JIYU_LOG_ERROR("写入 SO 路径失败");
        return -1;
    }
    
    uintptr_t dargs[2] = { mem, RTLD_NOW };
    uintptr_t h = 0;
    if (inject_call(pid, fn, 2, dargs, &h) < 0) {
        JIYU_LOG_ERROR("远程 dlopen 调用失败");
        return -1;
    }
    
    if (handle) *handle = h;
    JIYU_LOG_INFO("远程 dlopen 成功: %s -> handle=0x%lx",
                  so_path, (unsigned long)h);
    return 0;
}

// ==================== 共享内存 ====================
static JySharedState *g_state = NULL;
static void *g_shm_map = NULL;
static int g_shm_fd = -1;

int jy_state_create(void) {
    if (g_state) return 0;
    
    shm_unlink(JYFREE_SHM_NAME);
    
    g_shm_fd = shm_open(JYFREE_SHM_NAME, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (g_shm_fd < 0) {
        JIYU_LOG_ERROR("创建共享内存失败: %s", strerror(errno));
        return -1;
    }
    
    if (ftruncate(g_shm_fd, sizeof(JySharedState)) < 0) {
        JIYU_LOG_ERROR("设置共享内存大小失败: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }
    
    g_shm_map = mmap(NULL, sizeof(JySharedState), PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_shm_fd, 0);
    if (g_shm_map == MAP_FAILED) {
        JIYU_LOG_ERROR("映射共享内存失败: %s", strerror(errno));
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }
    
    g_state = (JySharedState *)g_shm_map;
    memset(g_state, 0, sizeof(*g_state));
    g_state->magic = JY_SHARED_MAGIC;
    
    JiyuConfig *cfg = jiyu_get_config();
    g_state->feature_mask = cfg->feature_mask;
    g_state->capture_mode = cfg->capture_mode;
    
    JIYU_LOG_INFO("共享内存已创建: %s (特性位 0x%02X)",
                  JYFREE_SHM_PATH, g_state->feature_mask);
    return 0;
}

int jy_state_open(void) {
    if (g_state) return 0;
    
    g_shm_fd = shm_open(JYFREE_SHM_NAME, O_RDWR, 0600);
    if (g_shm_fd < 0) return jy_state_create();
    
    g_shm_map = mmap(NULL, sizeof(JySharedState), PROT_READ | PROT_WRITE,
                     MAP_SHARED, g_shm_fd, 0);
    if (g_shm_map == MAP_FAILED) {
        close(g_shm_fd);
        g_shm_fd = -1;
        return -1;
    }
    
    g_state = (JySharedState *)g_shm_map;
    
    if (g_state->magic != JY_SHARED_MAGIC) {
        JIYU_LOG_ERROR("共享内存校验失败");
        munmap(g_shm_map, sizeof(JySharedState));
        g_shm_map = NULL;
        g_state = NULL;
        return -1;
    }
    return 0;
}

void jy_state_close(void) {
    if (g_shm_map && g_shm_map != MAP_FAILED) {
        munmap(g_shm_map, sizeof(JySharedState));
    }
    if (g_shm_fd >= 0) close(g_shm_fd);
    g_shm_map = NULL;
    g_shm_fd = -1;
    g_state = NULL;
}

JySharedState* jy_state(void) { return g_state; }

void jy_state_set_features(uint32_t mask) {
    if (!g_state) return;
    g_state->feature_mask = mask;
    JIYU_LOG_INFO("特性位更新: 0x%02X", mask);
}

uint32_t jy_state_get_features(void) {
    return g_state ? g_state->feature_mask : 0;
}

void jy_state_set_capture_mode(uint32_t mode) {
    if (!g_state) return;
    g_state->capture_mode = mode;
    JIYU_LOG_INFO("截屏模式: %u", mode);
}

uint32_t jy_state_get_capture_mode(void) {
    return g_state ? g_state->capture_mode : JY_CAPTURE_PASS;
}

uint64_t jy_state_get_counter(int idx) {
    if (!g_state || idx < 0 || idx >= JY_CNT_MAX) return 0;
    return g_state->counters[idx];
}

// 心跳由 payload 在自己进程内直接写入, 控制器只读
uint64_t jy_state_get_heartbeat(void) {
    return g_state ? g_state->heartbeat : 0;
}

bool jy_state_payload_ready(void) {
    return g_state && g_state->payload_ready;
}

uint32_t jy_state_hooks_installed(void) {
    return g_state ? g_state->hooks_installed : 0;
}

// ==================== payload 路径定位 ====================
static const char* payload_path(void) {
    static char path[640];
    
    char exe[512];
    ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
    if (n > 0) {
        exe[n] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            *slash = '\0';
            snprintf(path, sizeof(path), "%s/libjyfree.so", exe);
            if (access(path, R_OK) == 0) return path;
        }
    }
    
    // 备用路径
    const char *candidates[] = {
        "/usr/local/lib/libjyfree.so",
        "/usr/lib/libjyfree.so",
        "./libjyfree.so"
    };
    for (size_t i = 0; i < sizeof(candidates)/sizeof(candidates[0]); i++) {
        if (access(candidates[i], R_OK) == 0) {
            snprintf(path, sizeof(path), "%s", candidates[i]);
            return path;
        }
    }
    
    snprintf(path, sizeof(path), "%s", "/usr/local/lib/libjyfree.so");
    return path;
}

// ==================== 注入主流程 ====================
int jyfree_inject(int pid, const char *so_path) {
    if (pid <= 0) {
        JIYU_LOG_ERROR("注入失败: 无效 pid");
        return -1;
    }
    
    // 检查进程状态: 非运行态 (T=stopped) 不注入
    JiyuProcessInfo info;
    if (jiyu_get_process_info(pid, &info) == 0) {
        if (info.state == 'T' || info.state == 'Z') {
            JIYU_LOG_WARN("目标进程处于 '%c' 状态, 跳过注入 (需先 SIGCONT)", info.state);
            return -1;
        }
    }
    
    const char *so = so_path ? so_path : payload_path();
    
    if (access(so, R_OK) != 0) {
        JIYU_LOG_ERROR("payload 不存在或不可读: %s", so);
        return -1;
    }
    
    // 共享内存必须先就绪
    if (jy_state_open() < 0 && jy_state_create() < 0) {
        JIYU_LOG_ERROR("共享内存初始化失败, 中止注入");
        return -1;
    }
    
    // 重置状态
    g_state->payload_ready = 0;
    g_state->hooks_installed = 0;
    g_state->heartbeat = 0;
    
    if (inject_attach(pid) < 0) return -1;
    
    uintptr_t handle = 0;
    int ret = inject_dlopen(pid, so, &handle);
    
    inject_detach(pid);
    
    if (ret < 0) return -1;
    
    // 保存句柄, uninject 时要用它 dlclose 才能触发 destructor
    g_payload_handle = handle;
    
    // 等待 payload 构造函数完成
    for (int i = 0; i < 50; i++) {
        if (g_state->payload_ready) break;
        usleep(100000);  // 最多等 5 秒
    }
    
    if (!g_state->payload_ready) {
        JIYU_LOG_ERROR("payload 未在超时内就绪 (检查 %s)", "/tmp/jyfree-payload.log");
        return -1;
    }
    
    g_injected_pid = pid;
    jiyu_get_process_info(pid, &info);
    g_injected_starttime = info.start_time;
    
    JIYU_LOG_INFO("注入成功: pid=%d, 已安装 %u 个钩子",
                  pid, g_state->hooks_installed);
    return 0;
}

int jyfree_uninject(int pid) {
    if (pid <= 0 || g_injected_pid != pid) return 0;
    
    if (!g_payload_handle) {
        JIYU_LOG_WARN("没有 payload 句柄, 无法卸载");
        g_injected_pid = -1;
        g_injected_starttime = 0;
        return -1;
    }
    
    // payload 实现了 destructor, dlclose 会触发它还原所有内联钩子
    if (inject_attach(pid) == 0) {
        uintptr_t fn = inject_resolve_remote(pid, "libdl.so", "dlclose");
        if (!fn) {
            JIYU_LOG_ERROR("无法解析目标进程 dlclose");
        } else {
            uintptr_t args[1] = { g_payload_handle };
            uintptr_t ret = 0;
            if (inject_call(pid, fn, 1, args, &ret) == 0) {
                JIYU_LOG_INFO("dlclose 返回 %d (0=成功)", (int)ret);
                if (ret != 0) {
                    JIYU_LOG_WARN("dlclose 未完全卸载 (可能仍有引用), 钩子可能残留");
                }
            } else {
                JIYU_LOG_ERROR("远程 dlclose 调用失败");
            }
        }
        inject_detach(pid);
    } else {
        JIYU_LOG_ERROR("附加失败, 无法卸载 payload");
    }
    
    g_payload_handle = 0;
    g_injected_pid = -1;
    g_injected_starttime = 0;
    return 0;
}

bool jyfree_is_injected(int pid) {
    if (g_injected_pid != pid) return false;
    if (!jiyu_is_process_running(pid)) return false;
    
    // 校验是不是同一个进程实例 (防 pid 复用)
    JiyuProcessInfo info;
    if (jiyu_get_process_info(pid, &info) == 0) {
        if (info.start_time != g_injected_starttime) {
            JIYU_LOG_INFO("检测到进程已重启 (pid 复用), 注入失效");
            g_injected_pid = -1;
            g_injected_starttime = 0;
            g_payload_handle = 0;
            return false;
        }
    }
    
    return true;
}

int jyfree_injected_pid(void) { return g_injected_pid; }

// ==================== 看门狗 ====================
static void *watchdog_thread(void *arg) {
    (void)arg;
    
    uint32_t last_hb = 0;
    int stale_count = 0;
    
    while (g_watchdog_running) {
        sleep(1);
        
        int pid = jiyu_find_student_process();
        
        if (pid <= 0) {
            // Student 未运行 (进程已消失, payload 内存随之释放)
            if (g_injected_pid > 0) {
                JIYU_LOG_INFO("Student 已退出, 清除注入记录");
                g_injected_pid = -1;
                g_injected_starttime = 0;
                g_payload_handle = 0;
            }
            continue;
        }
        
        if (pid != g_injected_pid) {
            // 新实例 (systemd 重启过), 重新注入
            JIYU_LOG_INFO("检测到 Student 新实例 (pid=%d), 重新注入", pid);
            g_injected_pid = -1;
            g_payload_handle = 0;
            if (jyfree_inject(pid, NULL) == 0) {
                last_hb = 0;
                stale_count = 0;
            }
            continue;
        }
        
        // 校验实例是否被替换
        if (!jyfree_is_injected(pid)) {
            JIYU_LOG_WARN("注入失效, 尝试重新注入 pid=%d", pid);
            jyfree_inject(pid, NULL);
            continue;
        }
        
        // payload 心跳检测 (5 秒无心跳视为 payload 线程已死)
        uint32_t hb = (uint32_t)(g_state ? g_state->heartbeat : 0);
        if (hb != last_hb) {
            last_hb = hb;
            stale_count = 0;
        } else {
            stale_count++;
            if (stale_count == 5) {
                JIYU_LOG_WARN("payload 心跳停滞 5 秒, 重新注入");
                jyfree_inject(pid, NULL);
                stale_count = 0;
            }
        }
    }
    
    return NULL;
}

int jyfree_start_watchdog(void) {
    if (g_watchdog_running) return 0;
    g_watchdog_running = 1;
    
    if (pthread_create(&g_watchdog_tid, NULL, watchdog_thread, NULL) != 0) {
        JIYU_LOG_ERROR("看门狗线程创建失败");
        g_watchdog_running = 0;
        return -1;
    }
    
    JIYU_LOG_INFO("看门狗已启动 (自动重注入)");
    return 0;
}

void jyfree_stop_watchdog(void) {
    if (!g_watchdog_running) return;
    g_watchdog_running = 0;
    pthread_join(g_watchdog_tid, NULL);
    JIYU_LOG_INFO("看门狗已停止");
}