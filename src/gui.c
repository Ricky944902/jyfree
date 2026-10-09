#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include "gui.h"
#include "jiyu.h"

// ==================== 前向声明 ====================
static void draw_button(GuiWindow *gui, Button *btn);
static void draw_status(GuiWindow *gui, StatusIndicator *ind);
static int check_button_click(GuiWindow *gui, int x, int y);
static int check_button_hover(GuiWindow *gui, int x, int y);

// ==================== 内部状态 ====================
static int g_font_ascent = 12;
static int g_font_height = 14;
static XFontStruct *g_font = NULL;

// ==================== 字体工具 ====================
static void load_font(Display *display) {
    if (g_font) return;
    g_font = XLoadQueryFont(display, "fixed");
    if (g_font) {
        g_font_ascent = g_font->ascent;
        g_font_height = g_font->ascent + g_font->descent;
    }
}

static void draw_text(Display *display, Window window, GC gc, 
                      int x, int y, const char *text, unsigned long color) {
    if (!g_font) return;
    XSetForeground(display, gc, color);
    XSetFont(display, gc, g_font->fid);
    XDrawString(display, window, gc, x, y, text, strlen(text));
}

static int text_width(const char *text) {
    if (!g_font || !text) return 0;
    return XTextWidth(g_font, text, strlen(text));
}

// ==================== GUI初始化 ====================
int gui_init(GuiWindow *gui, int width, int height, const char *title) {
    JIYU_LOG_INFO("初始化GUI: %dx%d - %s", width, height, title);
    
    memset(gui, 0, sizeof(GuiWindow));
    
    // 多线程安全: 必须在任何 Xlib 调用之前初始化。
    // GUI 线程、监控线程、回调都会访问同一个 Display。
    if (!XInitThreads()) {
        JIYU_LOG_ERROR("XInitThreads 失败, 多线程访问 Xlib 不安全");
        return -1;
    }
    
    gui->display = XOpenDisplay(NULL);
    if (!gui->display) {
        JIYU_LOG_ERROR("无法打开X显示连接");
        return -1;
    }
    
    load_font(gui->display);
    
    int screen = DefaultScreen(gui->display);
    Window root = RootWindow(gui->display, screen);
    
    XSetWindowAttributes attrs;
    attrs.background_pixel = COLOR_BG_DARK;
    attrs.border_pixel = COLOR_ACCENT;
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | 
                      ButtonReleaseMask | PointerMotionMask | StructureNotifyMask;
    
    gui->window = XCreateWindow(gui->display, root, 0, 0, width, height, 2,
                                CopyFromParent, InputOutput, CopyFromParent,
                                CWBackPixel | CWBorderPixel | CWEventMask, &attrs);
    
    gui->width = width;
    gui->height = height;
    
    XStoreName(gui->display, gui->window, title);
    strncpy(gui->title, title, sizeof(gui->title) - 1);
    
    XClassHint *class_hint = XAllocClassHint();
    if (class_hint) {
        class_hint->res_name = "JiYuTrainer";
        class_hint->res_class = "JiYuTrainer";
        XSetClassHint(gui->display, gui->window, class_hint);
        XFree(class_hint);
    }
    
    gui->gc = XCreateGC(gui->display, gui->window, 0, NULL);
    XSetForeground(gui->display, gui->gc, COLOR_WHITE);
    
    gui->button_count = 0;
    gui->indicator_count = 0;
    gui->status_message[0] = '\0';
    
    // 初始化子窗口
    gui_log_window_init(gui);
    gui_settings_init(gui);
    
    XMapWindow(gui->display, gui->window);
    XFlush(gui->display);
    
    JIYU_LOG_INFO("GUI初始化完成");
    return 0;
}

// ==================== GUI清理 ====================
void gui_cleanup(GuiWindow *gui) {
    JIYU_LOG_INFO("清理GUI资源");
    
    gui_log_window_cleanup(gui);
    gui_settings_cleanup(gui);
    
    if (gui->gc) {
        XFreeGC(gui->display, gui->gc);
        gui->gc = NULL;
    }
    if (gui->window) {
        XDestroyWindow(gui->display, gui->window);
        gui->window = 0;
    }
    if (gui->display) {
        XCloseDisplay(gui->display);
        gui->display = NULL;
    }
    if (g_font) {
        XFreeFont(gui->display, g_font);
        g_font = NULL;
    }
}

// ==================== 按钮操作 ====================
int gui_add_button(GuiWindow *gui, int x, int y, int width, int height, 
                   const char *text, void (*callback)(void)) {
    if (gui->button_count >= 16) return -1;
    
    Button *btn = &gui->buttons[gui->button_count];
    btn->x = x; btn->y = y;
    btn->width = width; btn->height = height;
    strncpy(btn->text, text, sizeof(btn->text) - 1);
    btn->state = BTN_NORMAL;
    btn->normal_color = COLOR_ACCENT;
    btn->hover_color = COLOR_HIGHLIGHT;
    btn->pressed_color = COLOR_TEXT;
    btn->text_color = COLOR_WHITE;
    btn->enabled = true;
    btn->callback = callback;
    
    gui->button_count++;
    return gui->button_count - 1;
}

int gui_set_button_callback(GuiWindow *gui, int index, void (*callback)(void)) {
    if (index < 0 || index >= gui->button_count) return -1;
    gui->buttons[index].callback = callback;
    return 0;
}

int gui_set_button_text(GuiWindow *gui, int index, const char *text) {
    if (index < 0 || index >= gui->button_count) return -1;
    strncpy(gui->buttons[index].text, text, sizeof(gui->buttons[index].text) - 1);
    return 0;
}

int gui_set_button_enabled(GuiWindow *gui, int index, bool enabled) {
    if (index < 0 || index >= gui->button_count) return -1;
    gui->buttons[index].enabled = enabled;
    gui->buttons[index].normal_color = enabled ? COLOR_ACCENT : 0x404040;
    return 0;
}

// ==================== 状态指示器 ====================
int gui_add_status(GuiWindow *gui, int x, int y, const char *label) {
    if (gui->indicator_count >= 8) return -1;
    
    StatusIndicator *ind = &gui->indicators[gui->indicator_count];
    ind->x = x; ind->y = y;
    strncpy(ind->label, label, sizeof(ind->label) - 1);
    ind->status = 0;
    
    gui->indicator_count++;
    return gui->indicator_count - 1;
}

void gui_update_status(GuiWindow *gui, int index, int status) {
    if (index >= 0 && index < gui->indicator_count) {
        gui->indicators[index].status = status;
        gui_draw(gui);
    }
}

// ==================== 状态消息 ====================
void gui_set_status_message(GuiWindow *gui, const char *message) {
    strncpy(gui->status_message, message, sizeof(gui->status_message) - 1);
    gui_draw(gui);
}

// ==================== 绘制函数 ====================
void gui_draw(GuiWindow *gui) {
    if (!gui->display || !gui->window) return;
    
    XClearWindow(gui->display, gui->window);
    
    // 标题
    draw_text(gui->display, gui->window, gui->gc, 20, 30, gui->title, COLOR_TEXT);
    
    // 分隔线
    XSetForeground(gui->display, gui->gc, COLOR_ACCENT);
    XDrawLine(gui->display, gui->window, gui->gc, 20, 45, gui->width - 20, 45);
    
    // 版本
    char ver[32];
    snprintf(ver, sizeof(ver), "v%s", JIYU_VERSION_STRING);
    draw_text(gui->display, gui->window, gui->gc, gui->width - 60, 30, ver, COLOR_GRAY);
    
    // 按钮
    for (int i = 0; i < gui->button_count; i++) {
        draw_button(gui, &gui->buttons[i]);
    }
    
    // 状态指示器
    for (int i = 0; i < gui->indicator_count; i++) {
        draw_status(gui, &gui->indicators[i]);
    }
    
    // 状态消息
    if (gui->status_message[0]) {
        draw_text(gui->display, gui->window, gui->gc, 20, gui->height - 20,
                  gui->status_message, COLOR_YELLOW);
    }
    
    XFlush(gui->display);
}

// ==================== 事件处理 ====================
int gui_handle_events(GuiWindow *gui) {
    XEvent event;
    int running = 1;
    
    while (XPending(gui->display)) {
        XNextEvent(gui->display, &event);
        
        // 子窗口事件处理
        if (gui->log_window.visible) {
            if (gui_log_window_handle_events(gui, &event)) continue;
        }
        if (gui->settings_dialog.visible) {
            if (gui_settings_handle_events(gui, &event)) continue;
        }
        
        switch (event.type) {
            case Expose:
                if (event.xexpose.count == 0) gui_draw(gui);
                break;
                
            case ButtonPress:
                if (event.xbutton.button == Button1) {
                    int btn = check_button_click(gui, event.xbutton.x, event.xbutton.y);
                    if (btn >= 0 && gui->buttons[btn].enabled) {
                        gui->buttons[btn].state = BTN_PRESSED;
                        gui_draw(gui);
                        if (gui->buttons[btn].callback) {
                            gui->buttons[btn].callback();
                        }
                    }
                }
                break;
                
            case ButtonRelease:
                for (int i = 0; i < gui->button_count; i++) {
                    gui->buttons[i].state = BTN_NORMAL;
                }
                gui_draw(gui);
                break;
                
            case MotionNotify:
                check_button_hover(gui, event.xmotion.x, event.xmotion.y);
                break;
                
            case KeyPress: {
                char buf[32];
                KeySym keysym;
                XLookupString(&event.xkey, buf, sizeof(buf), &keysym, NULL);
                if (keysym == XK_Escape) running = 0;
                break;
            }
                
            case DestroyNotify:
                running = 0;
                break;
        }
    }
    
    return running;
}

// ==================== 内部绘制 ====================
static void draw_button(GuiWindow *gui, Button *btn) {
    unsigned long color = btn->normal_color;
    if (btn->state == BTN_HOVER) color = btn->hover_color;
    else if (btn->state == BTN_PRESSED) color = btn->pressed_color;
    
    XSetForeground(gui->display, gui->gc, color);
    XFillRectangle(gui->display, gui->window, gui->gc, 
                   btn->x, btn->y, btn->width, btn->height);
    
    XSetForeground(gui->display, gui->gc, COLOR_ACCENT);
    XDrawRectangle(gui->display, gui->window, gui->gc,
                   btn->x, btn->y, btn->width, btn->height);
    
    if (g_font) {
        int tw = text_width(btn->text);
        int tx = btn->x + (btn->width - tw) / 2;
        int ty = btn->y + (btn->height + g_font_ascent) / 2;
        draw_text(gui->display, gui->window, gui->gc, tx, ty, 
                  btn->text, btn->text_color);
    }
}

static void draw_status(GuiWindow *gui, StatusIndicator *ind) {
    draw_text(gui->display, gui->window, gui->gc, ind->x, ind->y,
              ind->label, COLOR_WHITE);
    
    unsigned long color;
    const char *stxt;
    switch (ind->status) {
        case 1: color = COLOR_GREEN; stxt = "运行中"; break;
        case 2: color = COLOR_YELLOW; stxt = "警告"; break;
        default: color = COLOR_RED; stxt = "停止";
    }
    
    XSetForeground(gui->display, gui->gc, color);
    XFillArc(gui->display, gui->window, gui->gc,
             ind->x + 120, ind->y - 12, 12, 12, 0, 360 * 64);
    
    draw_text(gui->display, gui->window, gui->gc, ind->x + 140, ind->y, stxt, COLOR_GRAY);
}

static int check_button_click(GuiWindow *gui, int x, int y) {
    for (int i = 0; i < gui->button_count; i++) {
        Button *btn = &gui->buttons[i];
        if (x >= btn->x && x <= btn->x + btn->width &&
            y >= btn->y && y <= btn->y + btn->height) {
            return i;
        }
    }
    return -1;
}

static int check_button_hover(GuiWindow *gui, int x, int y) {
    int changed = 0;
    for (int i = 0; i < gui->button_count; i++) {
        Button *btn = &gui->buttons[i];
        if (x >= btn->x && x <= btn->x + btn->width &&
            y >= btn->y && y <= btn->y + btn->height) {
            if (btn->state != BTN_PRESSED && btn->state != BTN_HOVER) {
                btn->state = BTN_HOVER;
                changed = 1;
            }
        } else {
            if (btn->state == BTN_HOVER) {
                btn->state = BTN_NORMAL;
                changed = 1;
            }
        }
    }
    if (changed) gui_draw(gui);
    return 0;
}

// =====================================================================
// 日志窗口
// =====================================================================
int gui_log_window_init(GuiWindow *gui) {
    LogWindow *lw = &gui->log_window;
    memset(lw, 0, sizeof(LogWindow));
    lw->visible = false;
    lw->entry_count = 0;
    lw->scroll_offset = 0;
    return 0;
}

void gui_log_window_cleanup(GuiWindow *gui) {
    LogWindow *lw = &gui->log_window;
    if (lw->window) {
        XDestroyWindow(gui->display, lw->window);
        lw->window = 0;
    }
    lw->visible = false;
}

void gui_log_window_show(GuiWindow *gui) {
    LogWindow *lw = &gui->log_window;
    if (lw->visible) return;
    
    int screen = DefaultScreen(gui->display);
    Window root = RootWindow(gui->display, screen);
    
    XSetWindowAttributes attrs;
    attrs.background_pixel = COLOR_BG_DARK;
    attrs.border_pixel = COLOR_ACCENT;
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | 
                      ButtonReleaseMask | StructureNotifyMask;
    
    lw->width = 500;
    lw->height = 350;
    lw->window = XCreateWindow(gui->display, root, 100, 100, 
                               lw->width, lw->height, 2,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWBorderPixel | CWEventMask, &attrs);
    
    XStoreName(gui->display, lw->window, "日志查看器");
    lw->gc = XCreateGC(gui->display, lw->window, 0, NULL);
    lw->visible = true;
    
    XMapWindow(gui->display, lw->window);
    XFlush(gui->display);
}

void gui_log_window_hide(GuiWindow *gui) {
    LogWindow *lw = &gui->log_window;
    if (!lw->visible) return;
    
    if (lw->gc) XFreeGC(gui->display, lw->gc);
    if (lw->window) XDestroyWindow(gui->display, lw->window);
    lw->window = 0;
    lw->gc = 0;
    lw->visible = false;
}

void gui_log_window_add(GuiWindow *gui, int level, const char *message) {
    LogWindow *lw = &gui->log_window;
    if (lw->entry_count >= 100) {
        // 移动条目
        memmove(lw->entries, lw->entries + 1, sizeof(LogEntry) * 99);
        lw->entry_count = 99;
    }
    
    LogEntry *entry = &lw->entries[lw->entry_count];
    strncpy(entry->message, message, sizeof(entry->message) - 1);
    entry->level = level;
    
    switch (level) {
        case JIYU_DEBUG_ERROR: entry->color = COLOR_RED; break;
        case JIYU_DEBUG_WARN:  entry->color = COLOR_YELLOW; break;
        case JIYU_DEBUG_INFO:  entry->color = COLOR_GREEN; break;
        default:               entry->color = COLOR_WHITE;
    }
    
    lw->entry_count++;
    
    // 自动滚动到底部
    int max_visible = (lw->height - 40) / g_font_height;
    if (lw->entry_count > max_visible) {
        lw->scroll_offset = lw->entry_count - max_visible;
    }
    
    if (lw->visible) gui_log_window_draw(gui);
}

void gui_log_window_draw(GuiWindow *gui) {
    LogWindow *lw = &gui->log_window;
    if (!lw->visible || !lw->window) return;
    
    XClearWindow(gui->display, lw->window);
    
    // 标题
    draw_text(gui->display, lw->window, lw->gc, 10, 20, "日志输出", COLOR_TEXT);
    XSetForeground(gui->display, lw->gc, COLOR_ACCENT);
    XDrawLine(gui->display, lw->window, lw->gc, 10, 30, lw->width - 10, 30);
    
    // 日志条目
    load_font(gui->display);
    int max_visible = (lw->height - 40) / g_font_height;
    int start = lw->scroll_offset;
    int end = start + max_visible;
    if (end > lw->entry_count) end = lw->entry_count;
    
    for (int i = start; i < end; i++) {
        LogEntry *entry = &lw->entries[i];
        int y = 30 + (i - start + 1) * g_font_height;
        draw_text(gui->display, lw->window, lw->gc, 10, y, entry->message, entry->color);
    }
    
    // 滚动条提示
    if (lw->entry_count > max_visible) {
        char scroll_info[32];
        snprintf(scroll_info, sizeof(scroll_info), "[%d/%d]", start + 1, lw->entry_count);
        draw_text(gui->display, lw->window, lw->gc, lw->width - 60, 20, scroll_info, COLOR_GRAY);
    }
    
    XFlush(gui->display);
}

int gui_log_window_handle_events(GuiWindow *gui, XEvent *event) {
    LogWindow *lw = &gui->log_window;
    if (!lw->visible || event->xany.window != lw->window) return 0;
    
    switch (event->type) {
        case Expose:
            gui_log_window_draw(gui);
            return 1;
            
        case ButtonPress:
            if (event->xbutton.button == Button4) { // 滚轮上
                if (lw->scroll_offset > 0) lw->scroll_offset--;
                gui_log_window_draw(gui);
            } else if (event->xbutton.button == Button5) { // 滚轮下
                int max_visible = (lw->height - 40) / g_font_height;
                if (lw->scroll_offset < lw->entry_count - max_visible) {
                    lw->scroll_offset++;
                }
                gui_log_window_draw(gui);
            }
            return 1;
            
        case KeyPress: {
            char buf[32];
            KeySym keysym;
            XLookupString(&event->xkey, buf, sizeof(buf), &keysym, NULL);
            if (keysym == XK_Escape || keysym == XK_q) {
                gui_log_window_hide(gui);
                gui_draw(gui);
            }
            return 1;
        }
        
        case DestroyNotify:
            gui_log_window_hide(gui);
            gui_draw(gui);
            return 1;
    }
    
    return 0;
}

// =====================================================================
// 设置对话框
// =====================================================================
int gui_settings_init(GuiWindow *gui) {
    SettingsDialog *sd = &gui->settings_dialog;
    memset(sd, 0, sizeof(SettingsDialog));
    sd->visible = false;
    
    // 从配置加载默认值
    JiyuConfig *cfg = jiyu_get_config();
    sd->debug_level = cfg->debug_level;
    sd->capture_mode = (int)cfg->capture_mode;
    
    uint32_t mask = cfg->feature_mask;
    sd->feature_lock    = (mask & JY_FEAT_LOCK)    != 0;
    sd->feature_monitor = (mask & JY_FEAT_MONITOR) != 0;
    sd->feature_command = (mask & JY_FEAT_COMMAND) != 0;
    sd->feature_input   = (mask & JY_FEAT_INPUT)   != 0;
    sd->feature_policy  = (mask & JY_FEAT_POLICY)  != 0;
    sd->feature_apps    = (mask & JY_FEAT_APPS)    != 0;
    
    return 0;
}

void gui_settings_cleanup(GuiWindow *gui) {
    SettingsDialog *sd = &gui->settings_dialog;
    if (sd->window) {
        XDestroyWindow(gui->display, sd->window);
        sd->window = 0;
    }
    sd->visible = false;
}

void gui_settings_show(GuiWindow *gui) {
    SettingsDialog *sd = &gui->settings_dialog;
    if (sd->visible) return;
    
    int screen = DefaultScreen(gui->display);
    Window root = RootWindow(gui->display, screen);
    
    XSetWindowAttributes attrs;
    attrs.background_pixel = COLOR_BG_DARK;
    attrs.border_pixel = COLOR_ACCENT;
    attrs.event_mask = ExposureMask | KeyPressMask | ButtonPressMask | 
                      ButtonReleaseMask | StructureNotifyMask;
    
    sd->width = 380;
    sd->height = 320;
    sd->window = XCreateWindow(gui->display, root, 150, 150,
                               sd->width, sd->height, 2,
                               CopyFromParent, InputOutput, CopyFromParent,
                               CWBackPixel | CWBorderPixel | CWEventMask, &attrs);
    
    XStoreName(gui->display, sd->window, "设置");
    sd->gc = XCreateGC(gui->display, sd->window, 0, NULL);
    sd->visible = true;
    
    // 同步当前特性位 (从共享内存读取, 反映实时状态)
    uint32_t mask = jy_state_get_features();
    sd->debug_level    = jiyu_debug_get_level();
    sd->feature_lock    = (mask & JY_FEAT_LOCK)    != 0;
    sd->feature_monitor = (mask & JY_FEAT_MONITOR) != 0;
    sd->feature_command = (mask & JY_FEAT_COMMAND) != 0;
    sd->feature_input   = (mask & JY_FEAT_INPUT)   != 0;
    sd->feature_policy  = (mask & JY_FEAT_POLICY)  != 0;
    sd->feature_apps    = (mask & JY_FEAT_APPS)    != 0;
    sd->capture_mode    = (int)jy_state_get_capture_mode();
    
    XMapWindow(gui->display, sd->window);
    XFlush(gui->display);
}

void gui_settings_hide(GuiWindow *gui) {
    SettingsDialog *sd = &gui->settings_dialog;
    if (!sd->visible) return;
    
    if (sd->gc) XFreeGC(gui->display, sd->gc);
    if (sd->window) XDestroyWindow(gui->display, sd->window);
    sd->window = 0;
    sd->gc = 0;
    sd->visible = false;
}

static void settings_draw_checkbox(GuiWindow *gui, SettingsDialog *sd,
                                   int y, const char *label, bool value) {
    XSetForeground(gui->display, sd->gc, COLOR_BG_LIGHT);
    XFillRectangle(gui->display, sd->window, sd->gc, 20, y - 12, 14, 14);
    XSetForeground(gui->display, sd->gc, value ? COLOR_GREEN : COLOR_ACCENT);
    XDrawRectangle(gui->display, sd->window, sd->gc, 20, y - 12, 14, 14);
    
    if (value) {
        draw_text(gui->display, sd->window, sd->gc, 22, y, "X", COLOR_GREEN);
    }
    
    draw_text(gui->display, sd->window, sd->gc, 42, y, label, COLOR_WHITE);
}

void gui_settings_draw(GuiWindow *gui) {
    SettingsDialog *sd = &gui->settings_dialog;
    if (!sd->visible || !sd->window) return;
    
    XClearWindow(gui->display, sd->window);
    load_font(gui->display);
    
    // 标题
    draw_text(gui->display, sd->window, sd->gc, 20, 25, "设置", COLOR_TEXT);
    XSetForeground(gui->display, sd->gc, COLOR_ACCENT);
    XDrawLine(gui->display, sd->window, sd->gc, 20, 35, sd->width - 20, 35);
    
    // 调试级别
    draw_text(gui->display, sd->window, sd->gc, 20, 60, "调试级别:", COLOR_WHITE);
    const char *levels[] = {"关闭", "错误", "警告", "信息", "详细", "跟踪"};
    for (int i = 0; i < 6; i++) {
        int bx = 120 + i * 40;
        unsigned long bg = (sd->debug_level == i) ? COLOR_HIGHLIGHT : COLOR_BG_LIGHT;
        XSetForeground(gui->display, sd->gc, bg);
        XFillRectangle(gui->display, sd->window, sd->gc, bx, 48, 35, 18);
        XSetForeground(gui->display, sd->gc, COLOR_ACCENT);
        XDrawRectangle(gui->display, sd->window, sd->gc, bx, 48, 35, 18);
        draw_text(gui->display, sd->window, sd->gc, bx + 5, 61, levels[i], COLOR_WHITE);
    }
    
    // 特性位复选框 (两列)
    settings_draw_checkbox(gui, sd, 90,  "锁屏拦截", sd->feature_lock);
    settings_draw_checkbox(gui, sd, 120, "监视拦截", sd->feature_monitor);
    settings_draw_checkbox(gui, sd, 150, "命令拦截", sd->feature_command);
    settings_draw_checkbox(gui, sd, 180, "输入拦截", sd->feature_input);
    settings_draw_checkbox(gui, sd, 210, "策略拦截", sd->feature_policy);
    settings_draw_checkbox(gui, sd, 240, "应用拦截", sd->feature_apps);
    
    // 截屏模式
    draw_text(gui->display, sd->window, sd->gc, 20, 268, "截屏:", COLOR_WHITE);
    const char *cmodes[] = { "放行", "冻结", "全黑" };
    for (int i = 0; i < 3; i++) {
        int bx = 80 + i * 55;
        unsigned long bg = (sd->capture_mode == i) ? COLOR_HIGHLIGHT : COLOR_BG_LIGHT;
        XSetForeground(gui->display, sd->gc, bg);
        XFillRectangle(gui->display, sd->window, sd->gc, bx, 256, 50, 18);
        XSetForeground(gui->display, sd->gc, COLOR_ACCENT);
        XDrawRectangle(gui->display, sd->window, sd->gc, bx, 256, 50, 18);
        draw_text(gui->display, sd->window, sd->gc, bx + 12, 269, cmodes[i], COLOR_WHITE);
    }
    
    // 按钮
    Button ok_btn = {sd->width - 180, sd->height - 40, 70, 28, "确定", 
                     BTN_NORMAL, COLOR_GREEN, 0x00cc00, 0x009900, COLOR_WHITE, true, NULL};
    Button cancel_btn = {sd->width - 90, sd->height - 40, 70, 28, "取消",
                         BTN_NORMAL, COLOR_RED, 0xcc0000, 0x990000, COLOR_WHITE, true, NULL};
    
    draw_button(gui, &ok_btn);
    draw_button(gui, &cancel_btn);
    
    XFlush(gui->display);
}

int gui_settings_handle_events(GuiWindow *gui, XEvent *event) {
    SettingsDialog *sd = &gui->settings_dialog;
    if (!sd->visible || event->xany.window != sd->window) return 0;
    
    switch (event->type) {
        case Expose:
            gui_settings_draw(gui);
            return 1;
            
        case ButtonPress: {
            int x = event->xbutton.x;
            int y = event->xbutton.y;
            
            // 调试级别按钮
            if (y >= 48 && y <= 66) {
                for (int i = 0; i < 6; i++) {
                    int bx = 120 + i * 40;
                    if (x >= bx && x <= bx + 35) {
                        sd->debug_level = i;
                        gui_settings_draw(gui);
                        return 1;
                    }
                }
            }
            
            // 特性位复选框 (左列 x=20..36, 右列 x=190..206)
            struct { int y1, y2; bool *flag; } boxes[] = {
                { 78,  95, &sd->feature_lock },
                { 108, 125, &sd->feature_monitor },
                { 138, 155, &sd->feature_command },
                { 168, 185, &sd->feature_input },
                { 198, 215, &sd->feature_policy },
                { 228, 245, &sd->feature_apps },
            };
            for (size_t i = 0; i < sizeof(boxes)/sizeof(boxes[0]); i++) {
                if (y < boxes[i].y1 || y > boxes[i].y2) continue;
                bool right_col = (x >= 185 && x <= 360);
                bool left_col = (x >= 20 && x <= 180);
                if (!right_col && !left_col) return 1;
                // 右列跳过 (本版本只画了左列)
                if (!left_col) return 1;
                *boxes[i].flag = !*boxes[i].flag;
                gui_settings_draw(gui);
                return 1;
            }
            
            // 截屏模式
            if (y >= 254 && y <= 274) {
                for (int i = 0; i < 3; i++) {
                    int bx = 80 + i * 55;
                    if (x >= bx && x <= bx + 50) {
                        sd->capture_mode = i;
                        gui_settings_draw(gui);
                        return 1;
                    }
                }
            }
            
            // 确定按钮
            if (x >= sd->width - 180 && x <= sd->width - 110 &&
                y >= sd->height - 40 && y <= sd->height - 12) {
                gui_settings_apply(gui);
                gui_settings_hide(gui);
                gui_draw(gui);
                return 1;
            }
            
            // 取消按钮
            if (x >= sd->width - 90 && x <= sd->width - 20 &&
                y >= sd->height - 40 && y <= sd->height - 12) {
                gui_settings_hide(gui);
                gui_draw(gui);
                return 1;
            }
            return 1;
        }
        
        case KeyPress: {
            char buf[32];
            KeySym keysym;
            XLookupString(&event->xkey, buf, sizeof(buf), &keysym, NULL);
            if (keysym == XK_Escape) {
                gui_settings_hide(gui);
                gui_draw(gui);
            } else if (keysym == XK_Return) {
                gui_settings_apply(gui);
                gui_settings_hide(gui);
                gui_draw(gui);
            }
            return 1;
        }
        
        case DestroyNotify:
            gui_settings_hide(gui);
            gui_draw(gui);
            return 1;
    }
    
    return 0;
}

void gui_settings_apply(GuiWindow *gui) {
    SettingsDialog *sd = &gui->settings_dialog;
    JiyuConfig *cfg = jiyu_get_config();
    
    cfg->debug_level = (JiyuDebugLevel)sd->debug_level;
    
    // 把对话框里的复选框状态写回共享内存特性位
    uint32_t mask = 0;
    if (sd->feature_lock)    mask |= JY_FEAT_LOCK;
    if (sd->feature_monitor) mask |= JY_FEAT_MONITOR;
    if (sd->feature_command) mask |= JY_FEAT_COMMAND;
    if (sd->feature_input)   mask |= JY_FEAT_INPUT;
    if (sd->feature_policy)  mask |= JY_FEAT_POLICY;
    if (sd->feature_apps)    mask |= JY_FEAT_APPS;
    if (sd->capture_mode != JY_CAPTURE_PASS) mask |= JY_FEAT_CAPTURE;
    
    jy_state_set_features(mask);
    jy_state_set_capture_mode((uint32_t)sd->capture_mode);
    
    cfg->feature_mask = mask;
    cfg->capture_mode = (uint32_t)sd->capture_mode;
    
    jiyu_debug_set_level(cfg->debug_level);
    jiyu_config_save(NULL);
    
    JIYU_LOG_INFO("设置已应用并保存");
}
