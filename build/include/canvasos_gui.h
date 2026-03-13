#ifndef CANVASOS_GUI_H
#define CANVASOS_GUI_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Pixel & Buffer ─────────────────────────────────────────── */

typedef struct { uint8_t r, g, b, a; } GuiPixel;

typedef struct {
    GuiPixel *pixels;
    uint32_t  w, h;
    uint8_t   owned;
} GuiBuffer;

int      gui_buffer_init(GuiBuffer *buf, uint32_t w, uint32_t h);
void     gui_buffer_free(GuiBuffer *buf);
void     gui_buffer_clear(GuiBuffer *buf, GuiPixel color);
void     gui_buffer_set_pixel(GuiBuffer *buf, uint32_t x, uint32_t y, GuiPixel px);
GuiPixel gui_buffer_get_pixel(const GuiBuffer *buf, uint32_t x, uint32_t y);
void     gui_buffer_fill_rect(GuiBuffer *buf, int32_t x, int32_t y,
                              uint32_t w, uint32_t h, GuiPixel color);
void     gui_buffer_rect_outline(GuiBuffer *buf, int32_t x, int32_t y,
                                 uint32_t w, uint32_t h,
                                 GuiPixel color, uint8_t width);
void     gui_buffer_blit(GuiBuffer *dst, const GuiBuffer *src,
                         int32_t dx, int32_t dy);

/* ── BMP Output ─────────────────────────────────────────────── */

int gui_bmp_write(const GuiBuffer *buf, const char *path);

/* ── Bitmap Font (5×7) ──────────────────────────────────────── */

void     gui_font_draw_char(GuiBuffer *buf, uint32_t x, uint32_t y,
                            char c, GuiPixel color, uint32_t scale);
void     gui_font_draw_string(GuiBuffer *buf, uint32_t x, uint32_t y,
                              const char *str, GuiPixel color, uint32_t scale);
uint32_t gui_font_string_width(const char *str, uint32_t scale);

/* ── Rect ───────────────────────────────────────────────────── */

typedef struct {
    int32_t  x, y;
    uint32_t w, h;
} GuiRect;

/* ── Element ────────────────────────────────────────────────── */

#define GUI_ELEM_VISIBLE    (1u << 0)
#define GUI_ELEM_DRAGGABLE  (1u << 1)
#define GUI_ELEM_RESIZABLE  (1u << 2)
#define GUI_ELEM_FOCUSABLE  (1u << 3)

typedef enum {
    GUI_ELEM_PANEL = 0,
    GUI_ELEM_LABEL,
    GUI_ELEM_IMAGE,
    GUI_ELEM_BUTTON,
    GUI_ELEM_CUSTOM
} GuiElemType;

typedef struct GuiEvent GuiEvent;
typedef struct GuiElement GuiElement;

typedef void (*GuiDrawFn)(GuiElement *elem, GuiBuffer *target);
typedef void (*GuiEventFn)(GuiElement *elem, const GuiEvent *ev);

#define GUI_MAX_CHILDREN 32
#define GUI_LABEL_MAX    64

struct GuiElement {
    uint32_t      id;
    GuiElemType   type;
    GuiRect       rect;
    uint32_t      flags;
    uint32_t      z_order;

    GuiPixel      bg_color;
    GuiPixel      fg_color;
    GuiPixel      border_color;
    uint8_t       border_width;
    char          label[GUI_LABEL_MAX];

    GuiBuffer    *image;

    GuiDrawFn     custom_draw;
    GuiEventFn    on_event;
    void         *user_data;

    GuiElement   *parent;
    GuiElement   *children[GUI_MAX_CHILDREN];
    uint32_t      child_count;
};

/* ── System Canvas (inner layer) ────────────────────────────── */

#define GUI_MAX_ELEMENTS 128

typedef struct {
    GuiElement elements[GUI_MAX_ELEMENTS];
    uint32_t   count;
    uint32_t   next_id;
    uint32_t   focused_id;
    uint32_t   dragging_id;
    int32_t    drag_ox, drag_oy;
    uint32_t   canvas_w, canvas_h;
} GuiSystemCanvas;

void        gui_sys_init(GuiSystemCanvas *sys, uint32_t w, uint32_t h);
GuiElement *gui_sys_add(GuiSystemCanvas *sys, GuiElemType type,
                        GuiRect rect, uint32_t flags);
GuiElement *gui_sys_find(GuiSystemCanvas *sys, uint32_t id);
int         gui_sys_remove(GuiSystemCanvas *sys, uint32_t id);
void        gui_sys_move(GuiElement *elem, int32_t x, int32_t y);
void        gui_sys_resize(GuiElement *elem, uint32_t w, uint32_t h);
void        gui_sys_set_z(GuiElement *elem, uint32_t z);
void        gui_sys_render(const GuiSystemCanvas *sys, GuiBuffer *target);
GuiElement *gui_sys_hit_test(GuiSystemCanvas *sys, int32_t x, int32_t y);

/* ── Home Screen (outer layer) ──────────────────────────────── */

#define GUI_HOME_MAX_ICONS  32
#define GUI_ICON_LABEL_MAX  24

typedef struct {
    uint32_t elem_id;
    char     name[GUI_ICON_LABEL_MAX];
    GuiRect  slot;
    GuiPixel icon_color;
} GuiHomeIcon;

typedef struct {
    GuiHomeIcon icons[GUI_HOME_MAX_ICONS];
    uint32_t    icon_count;
    uint32_t    screen_w, screen_h;
    uint32_t    grid_cols, grid_rows;
    uint32_t    icon_size;
    uint32_t    margin;
    GuiPixel    statusbar_color;
    uint32_t    statusbar_h;
    GuiPixel    bg_color;
} GuiHomeScreen;

void gui_home_init(GuiHomeScreen *home, uint32_t screen_w, uint32_t screen_h);
int  gui_home_add_icon(GuiHomeScreen *home, const char *name,
                       GuiPixel color, uint32_t elem_id);
void gui_home_layout(GuiHomeScreen *home);
void gui_home_render(const GuiHomeScreen *home,
                     const GuiSystemCanvas *sys, GuiBuffer *target);
int  gui_home_hit_icon(const GuiHomeScreen *home, int32_t x, int32_t y);

/* ── Event System ───────────────────────────────────────────── */

typedef enum {
    GUI_EVT_NONE = 0,
    GUI_EVT_TOUCH_DOWN,
    GUI_EVT_TOUCH_UP,
    GUI_EVT_TOUCH_MOVE,
    GUI_EVT_KEY_DOWN,
    GUI_EVT_KEY_UP
} GuiEventType;

struct GuiEvent {
    GuiEventType type;
    int32_t      x, y;
    uint32_t     button;
    uint32_t     keycode;
    uint32_t     modifiers;
};

/* ── Top-Level Context ──────────────────────────────────────── */

typedef struct {
    GuiSystemCanvas sys;
    GuiHomeScreen   home;
    GuiBuffer       framebuffer;
    uint32_t        frame_count;
    uint8_t         dirty;
} GuiContext;

void gui_init(GuiContext *ctx, uint32_t screen_w, uint32_t screen_h);
void gui_free(GuiContext *ctx);
void gui_render(GuiContext *ctx);
int  gui_save_bmp(const GuiContext *ctx, const char *path);
int  gui_event_dispatch(GuiContext *ctx, const GuiEvent *ev);

#ifdef __cplusplus
}
#endif

#endif
