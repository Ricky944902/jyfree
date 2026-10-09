#ifndef PAYLOAD_STATE_H
#define PAYLOAD_STATE_H

// ============================================================
// 共享定义: 控制器 (jyfree) 与注入 payload (libjyfree.so) 共用
// 任何一方修改都必须同步, 否则结构体布局不一致会导致内存错乱
// ============================================================

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ==================== 共享内存 ====================
// 注意: shm_open() 接收的是"名字"而不是路径, glibc 会自动加 /dev/shm 前缀。
// 传入 "/jyfree.state" 后实际文件位于 /dev/shm/jyfree.state
// (与逆向分析文档记录的路径一致)。
#define JYFREE_SHM_NAME "/jyfree.state"
#define JYFREE_SHM_PATH "/dev/shm/jyfree.state"   // 仅用于日志显示

// ==================== 功能特性位 ====================
#define JY_FEAT_LOCK      (1u << 0)  // 锁屏/黑屏拦截
#define JY_FEAT_MONITOR   (1u << 1)  // 监视/桌面演示拦截
#define JY_FEAT_COMMAND   (1u << 2)  // 远程命令执行拦截
#define JY_FEAT_APPS      (1u << 3)  // 应用策略(杀进程/关窗口)拦截
#define JY_FEAT_POLICY    (1u << 4)  // 策略下发拦截(USB/网页/应用)
#define JY_FEAT_INPUT     (1u << 5)  // 键鼠输入拦截(XTestFake*/XGrab*)
#define JY_FEAT_CAPTURE   (1u << 6)  // 截屏拦截(XGetImage freeze/black)
#define JY_FEAT_ALL       0x7Fu

// 截屏处理模式
typedef enum {
    JY_CAPTURE_PASS = 0,   // 放行原始画面
    JY_CAPTURE_FREEZE,     // 冻结上一帧
    JY_CAPTURE_BLACK,      // 全黑
    JY_CAPTURE_WHITE       // 全白(测试用)
} JyCaptureMode;

// 拦截计数器索引
enum {
    JY_CNT_LOCK = 0,
    JY_CNT_MONITOR,
    JY_CNT_COMMAND,
    JY_CNT_APPS,
    JY_CNT_POLICY,
    JY_CNT_INPUT,
    JY_CNT_CAPTURE,
    JY_CNT_MAX
};

// ==================== 共享状态结构体 ====================
typedef struct {
    // magic: 'JYFR' 用于校验结构体有效性
    uint32_t magic;
    
    // 特性位 (控制器写, payload 读)
    uint32_t feature_mask;
    
    // 截屏模式
    uint32_t capture_mode;
    
    // payload 心跳 (payload 每秒更新, 控制器检测)
    volatile uint64_t heartbeat;
    
    // payload 注入成功标志
    uint32_t payload_ready;
    
    // 已安装的钩子数量
    uint32_t hooks_installed;
    
    // 拦截计数器
    volatile uint64_t counters[JY_CNT_MAX];
    
    // 保留
    uint64_t reserved[8];
} JySharedState;

#define JY_SHARED_MAGIC 0x5246594A  // "JYFR" little-endian

// ==================== 符号地址 (Student v2.7.3715, ET_EXEC 非PIE) ====================
// nm 地址即运行地址, 因为 Student 是 ET_EXEC 非 PIE
// 注意: 换版本必须重新 nm Student 确认

// CStudentMainWork 方法 (mangled name -> 地址)
#define SYM_ShowLockScreen            0x441ed4
#define SYM_ShowBlackScreen           0x43fa4c
#define SYM_StartMonitorPassive       0x44e670
#define SYM_StartRdpMonitorPassive    0x44dbe0
#define SYM_ProcessDeskMonitorCommand 0x44eb44
#define SYM_ExecuteRemoteCmd          0x441a80
#define SYM_ProcessRemoteCommand      0x4438c8
#define SYM_UpdatePoliciesToService   0x44d34c
#define SYM_UpdateUsbPolicyToService  0x44cd28
#define SYM_UpdateWebPolicyToService  0x44ce88

// libGetAppsInfo.so.2 (4 字节 b 跳转桩, 需先跳桩解析)
#define SYM_GetApplicationList        0x82f0
#define SYM_KillProcess               0x82f8
#define SYM_DoAppPolicyControl        0x8340
#define SYM_CloseTopWindow            0x8344
#define SYM_StartGetIcons             0x8348
#define SYM_CloseWndByWindowId        0x856c
#define SYM_CloseApps                 0x85f0

// libcastng.so.0.1 (freerdp 输入回调 = 教师控制最终落点)
#define SYM_tf_peer_keyboard_event        0x22ebf0
#define SYM_tf_peer_unicode_keyboard_event 0x22e940
#define SYM_tf_peer_mouse_event           0x22e750
#define SYM_tf_peer_extended_mouse_event  0x22e560
#define SYM_CastClient_setInputGrab_thunk 0x2083c0

// libDesk.so.2 (截屏)
#define SYM_CDesktopCapture_StartSendThread 0x132f0

// ==================== 库名 ====================
#define JY_LIB_CASTNG   "libcastng.so.0.1"
#define JY_LIB_DESK     "libDesk.so.2"
#define JY_LIB_GETAPPS  "libGetAppsInfo.so.2"
#define JY_LIB_CAST_X11 "libcast-x11.so"
#define JY_INSTALL_DIR  "/opt/mythware/classroom-management/"

// ==================== payload 内部日志 ====================
// payload 运行在 Student 进程内, 不能依赖控制器的 jiyu_debug_*,
// 因此使用独立实现, 避免 dlopen 时符号解析失败

#ifdef JIYU_PAYLOAD_BUILD

void jyf_log_impl(const char *level, const char *fmt, ...);

#define JYF_LOG(...)   jyf_log_impl("INFO", __VA_ARGS__)
#define JYF_ERR(...)   jyf_log_impl("ERR ", __VA_ARGS__)

#else

// 控制器侧: payload 不用这些宏, 仅为编译通过
#define JYF_LOG(...)   ((void)0)
#define JYF_ERR(...)   ((void)0)

#endif

#endif // PAYLOAD_STATE_H