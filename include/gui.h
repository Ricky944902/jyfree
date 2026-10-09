#ifndef GUI_H
#define GUI_H

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdbool.h>

// 颜色定义
#define COLOR_BG_DARK     0x1a1a2e
#define COLOR_BG_LIGHT    0x16213e
#define COLOR_ACCENT      0x0f3460
#define COLOR_HIGHLIGHT   0x533483
#define COLOR_TEXT        0xe94560
#define COLOR_WHITE       0xffffff
#define COLOR_GREEN       0x00ff00
#define COLOR_RED         0xff0000
#define COLOR_YELLOW      0xffff00
#define COLOR_GRAY        0x808080

// 按钮状态
typedef enum {
    BTN_NORMAL = 0,
    BTN_HOVER,
    BTN_PRESSED
} ButtonState;

// 按钮结构
typedef struct {
    int x, y, width, height;
    char text[64];
    ButtonState state;
    unsigned long normal_color;
    unsigned long hover_color;
    unsigned long pressed_color;
    unsigned long text_color;
    bool enabled;
    void (*callback)(void);
} Button;

// 状态指示器
typedef struct {
    int x, y;
    char label[64];
    int status;
} StatusIndicator;

// 日志条目
typedef struct {
    char message[256];
    int level;
    unsigned long color;
} LogEntry;

// 日志窗口
typedef struct {
    Display *display;
    Window window;
    GC gc;
    int width, height;
    bool visible;
    
    LogEntry entries[100];
    int entry_count;
    int scroll_offset;
} LogWindow;

// 设置对话框
typedef struct {
    Display *display;
    Window window;
    GC gc;
    int width, height;
    bool visible;
    
    // 设置项 (与共享内存特性位对应)
    int  debug_level;
    bool feature_lock;
    bool feature_monitor;
    bool feature_command;
    bool feature_input;
    bool feature_policy;
    bool feature_apps;
    int  capture_mode;    // JY_CAPTURE_*
    
    // 编辑状态
    int editing_field;
    char input_buffer[64];
} SettingsDialog;

// GUI窗口结构
typedef struct {
    Display *display;
    Window window;
    GC gc;
    int width, height;
    
    Button buttons[16];
    int button_count;
    
    StatusIndicator indicators[8];
    int indicator_count;
    
    char title[128];
    char status_message[256];
    
    LogWindow log_window;
    SettingsDialog settings_dialog;
} GuiWindow;

// ==================== 基础GUI函数 ====================
int gui_init(GuiWindow *gui, int width, int height, const char *title);
void gui_cleanup(GuiWindow *gui);
int gui_add_button(GuiWindow *gui, int x, int y, int width, int height, 
                   const char *text, void (*callback)(void));
int gui_add_status(GuiWindow *gui, int x, int y, const char *label);
void gui_update_status(GuiWindow *gui, int index, int status);
void gui_set_status_message(GuiWindow *gui, const char *message);
void gui_draw(GuiWindow *gui);
int gui_handle_events(GuiWindow *gui);

// ==================== 按钮操作 ====================
int gui_set_button_callback(GuiWindow *gui, int index, void (*callback)(void));
int gui_set_button_text(GuiWindow *gui, int index, const char *text);
int gui_set_button_enabled(GuiWindow *gui, int index, bool enabled);

// ==================== 日志窗口 ====================
int gui_log_window_init(GuiWindow *gui);
void gui_log_window_cleanup(GuiWindow *gui);
void gui_log_window_show(GuiWindow *gui);
void gui_log_window_hide(GuiWindow *gui);
void gui_log_window_add(GuiWindow *gui, int level, const char *message);
void gui_log_window_draw(GuiWindow *gui);
int gui_log_window_handle_events(GuiWindow *gui, XEvent *event);

// ==================== 设置对话框 ====================
int gui_settings_init(GuiWindow *gui);
void gui_settings_cleanup(GuiWindow *gui);
void gui_settings_show(GuiWindow *gui);
void gui_settings_hide(GuiWindow *gui);
void gui_settings_draw(GuiWindow *gui);
int gui_settings_handle_events(GuiWindow *gui, XEvent *event);
void gui_settings_apply(GuiWindow *gui);

#endif // GUI_H
