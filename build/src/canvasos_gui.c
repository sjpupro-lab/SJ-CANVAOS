#include "canvasos_gui.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================
 * Buffer Operations
 * ================================================================ */

int gui_buffer_init(GuiBuffer *buf, uint32_t w, uint32_t h) {
    if (!buf || w == 0 || h == 0) return -1;
    buf->pixels = (GuiPixel *)calloc((size_t)w * h, sizeof(GuiPixel));
    if (!buf->pixels) return -1;
    buf->w = w;
    buf->h = h;
    buf->owned = 1;
    return 0;
}

void gui_buffer_free(GuiBuffer *buf) {
    if (!buf) return;
    if (buf->owned && buf->pixels) free(buf->pixels);
    buf->pixels = NULL;
    buf->w = buf->h = 0;
}

void gui_buffer_clear(GuiBuffer *buf, GuiPixel color) {
    uint32_t n;
    if (!buf || !buf->pixels) return;
    n = buf->w * buf->h;
    for (uint32_t i = 0; i < n; ++i)
        buf->pixels[i] = color;
}

void gui_buffer_set_pixel(GuiBuffer *buf, uint32_t x, uint32_t y, GuiPixel px) {
    if (!buf || !buf->pixels) return;
    if (x < buf->w && y < buf->h)
        buf->pixels[y * buf->w + x] = px;
}

GuiPixel gui_buffer_get_pixel(const GuiBuffer *buf, uint32_t x, uint32_t y) {
    GuiPixel zero = {0, 0, 0, 0};
    if (!buf || !buf->pixels) return zero;
    if (x >= buf->w || y >= buf->h) return zero;
    return buf->pixels[y * buf->w + x];
}

void gui_buffer_fill_rect(GuiBuffer *buf, int32_t rx, int32_t ry,
                          uint32_t rw, uint32_t rh, GuiPixel color) {
    int32_t x0, y0, x1, y1;
    if (!buf || !buf->pixels) return;

    x0 = rx < 0 ? 0 : rx;
    y0 = ry < 0 ? 0 : ry;
    x1 = rx + (int32_t)rw;
    y1 = ry + (int32_t)rh;
    if (x1 > (int32_t)buf->w) x1 = (int32_t)buf->w;
    if (y1 > (int32_t)buf->h) y1 = (int32_t)buf->h;

    for (int32_t y = y0; y < y1; ++y)
        for (int32_t x = x0; x < x1; ++x)
            buf->pixels[y * buf->w + x] = color;
}

void gui_buffer_rect_outline(GuiBuffer *buf, int32_t rx, int32_t ry,
                             uint32_t rw, uint32_t rh,
                             GuiPixel color, uint8_t width) {
    if (!buf || !buf->pixels || width == 0) return;
    /* top */
    gui_buffer_fill_rect(buf, rx, ry, rw, width, color);
    /* bottom */
    gui_buffer_fill_rect(buf, rx, ry + (int32_t)rh - width, rw, width, color);
    /* left */
    gui_buffer_fill_rect(buf, rx, ry, width, rh, color);
    /* right */
    gui_buffer_fill_rect(buf, rx + (int32_t)rw - width, ry, width, rh, color);
}

static GuiPixel gui_alpha_blend(GuiPixel dst, GuiPixel src) {
    GuiPixel out;
    uint32_t sa = src.a;
    uint32_t da = 255u - sa;
    out.r = (uint8_t)((src.r * sa + dst.r * da) / 255u);
    out.g = (uint8_t)((src.g * sa + dst.g * da) / 255u);
    out.b = (uint8_t)((src.b * sa + dst.b * da) / 255u);
    out.a = (uint8_t)(sa + (dst.a * da) / 255u);
    return out;
}

void gui_buffer_blit(GuiBuffer *dst, const GuiBuffer *src,
                     int32_t dx, int32_t dy) {
    int32_t sx0, sy0, dx0, dy0, cw, ch;
    if (!dst || !src || !dst->pixels || !src->pixels) return;

    sx0 = 0; sy0 = 0;
    dx0 = dx; dy0 = dy;
    cw = (int32_t)src->w;
    ch = (int32_t)src->h;

    if (dx0 < 0) { sx0 = -dx0; cw += dx0; dx0 = 0; }
    if (dy0 < 0) { sy0 = -dy0; ch += dy0; dy0 = 0; }
    if (dx0 + cw > (int32_t)dst->w) cw = (int32_t)dst->w - dx0;
    if (dy0 + ch > (int32_t)dst->h) ch = (int32_t)dst->h - dy0;
    if (cw <= 0 || ch <= 0) return;

    for (int32_t y = 0; y < ch; ++y) {
        for (int32_t x = 0; x < cw; ++x) {
            GuiPixel sp = src->pixels[(sy0 + y) * src->w + (sx0 + x)];
            if (sp.a == 255) {
                dst->pixels[(dy0 + y) * dst->w + (dx0 + x)] = sp;
            } else if (sp.a > 0) {
                GuiPixel dp = dst->pixels[(dy0 + y) * dst->w + (dx0 + x)];
                dst->pixels[(dy0 + y) * dst->w + (dx0 + x)] = gui_alpha_blend(dp, sp);
            }
        }
    }
}

/* ================================================================
 * BMP Writer (24-bit, top-down via negative height)
 * ================================================================ */

static void bmp_u16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void bmp_u32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}

int gui_bmp_write(const GuiBuffer *buf, const char *path) {
    FILE *fp;
    uint32_t row_bytes, pad, data_size, file_size;
    uint8_t header[54];

    if (!buf || !buf->pixels || !path) return -1;

    row_bytes = buf->w * 3;
    pad = (4 - (row_bytes % 4)) % 4;
    row_bytes += pad;
    data_size = row_bytes * buf->h;
    file_size = 54 + data_size;

    memset(header, 0, 54);
    header[0] = 'B'; header[1] = 'M';
    bmp_u32(header + 2, file_size);
    bmp_u32(header + 10, 54);
    bmp_u32(header + 14, 40);
    bmp_u32(header + 18, buf->w);
    bmp_u32(header + 22, buf->h);  /* positive = bottom-up */
    bmp_u16(header + 26, 1);
    bmp_u16(header + 28, 24);
    bmp_u32(header + 34, data_size);

    fp = fopen(path, "wb");
    if (!fp) return -1;
    if (fwrite(header, 1, 54, fp) != 54) { fclose(fp); return -1; }

    /* BMP stores rows bottom-up */
    for (int32_t y = (int32_t)buf->h - 1; y >= 0; --y) {
        for (uint32_t x = 0; x < buf->w; ++x) {
            GuiPixel px = buf->pixels[y * buf->w + x];
            uint8_t bgr[3] = { px.b, px.g, px.r };
            fwrite(bgr, 1, 3, fp);
        }
        if (pad > 0) {
            uint8_t zeros[3] = {0, 0, 0};
            fwrite(zeros, 1, pad, fp);
        }
    }
    fclose(fp);
    return 0;
}

/* ================================================================
 * 5×7 Bitmap Font (ASCII 32-126)
 * ================================================================ */

static const uint8_t gui_font_5x7[95][7] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /*   */
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, /* ! */
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, /* " */
    {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, /* # */
    {0x0E,0x15,0x0E,0x14,0x0E,0x00,0x00}, /* $ */
    {0x13,0x08,0x04,0x02,0x19,0x00,0x00}, /* % */
    {0x06,0x09,0x06,0x15,0x0E,0x00,0x00}, /* & */
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, /* ' */
    {0x08,0x04,0x04,0x04,0x08,0x00,0x00}, /* ( */
    {0x02,0x04,0x04,0x04,0x02,0x00,0x00}, /* ) */
    {0x04,0x15,0x0E,0x15,0x04,0x00,0x00}, /* * */
    {0x00,0x04,0x0E,0x04,0x00,0x00,0x00}, /* + */
    {0x00,0x00,0x00,0x04,0x04,0x02,0x00}, /* , */
    {0x00,0x00,0x0E,0x00,0x00,0x00,0x00}, /* - */
    {0x00,0x00,0x00,0x00,0x04,0x00,0x00}, /* . */
    {0x10,0x08,0x04,0x02,0x01,0x00,0x00}, /* / */
    {0x0E,0x11,0x11,0x11,0x0E,0x00,0x00}, /* 0 */
    {0x04,0x06,0x04,0x04,0x0E,0x00,0x00}, /* 1 */
    {0x0E,0x11,0x0C,0x02,0x1F,0x00,0x00}, /* 2 */
    {0x0E,0x10,0x0C,0x10,0x0E,0x00,0x00}, /* 3 */
    {0x08,0x0C,0x0A,0x1F,0x08,0x00,0x00}, /* 4 */
    {0x1F,0x01,0x0F,0x10,0x0F,0x00,0x00}, /* 5 */
    {0x0E,0x01,0x0F,0x11,0x0E,0x00,0x00}, /* 6 */
    {0x1F,0x10,0x08,0x04,0x04,0x00,0x00}, /* 7 */
    {0x0E,0x11,0x0E,0x11,0x0E,0x00,0x00}, /* 8 */
    {0x0E,0x11,0x1E,0x10,0x0E,0x00,0x00}, /* 9 */
    {0x00,0x04,0x00,0x04,0x00,0x00,0x00}, /* : */
    {0x00,0x04,0x00,0x04,0x02,0x00,0x00}, /* ; */
    {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, /* < */
    {0x00,0x0E,0x00,0x0E,0x00,0x00,0x00}, /* = */
    {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, /* > */
    {0x0E,0x11,0x08,0x00,0x04,0x00,0x00}, /* ? */
    {0x0E,0x19,0x15,0x01,0x0E,0x00,0x00}, /* @ */
    {0x0E,0x11,0x1F,0x11,0x11,0x00,0x00}, /* A */
    {0x0F,0x11,0x0F,0x11,0x0F,0x00,0x00}, /* B */
    {0x0E,0x01,0x01,0x01,0x0E,0x00,0x00}, /* C */
    {0x07,0x09,0x11,0x09,0x07,0x00,0x00}, /* D */
    {0x1F,0x01,0x0F,0x01,0x1F,0x00,0x00}, /* E */
    {0x1F,0x01,0x0F,0x01,0x01,0x00,0x00}, /* F */
    {0x0E,0x01,0x19,0x11,0x0E,0x00,0x00}, /* G */
    {0x11,0x11,0x1F,0x11,0x11,0x00,0x00}, /* H */
    {0x0E,0x04,0x04,0x04,0x0E,0x00,0x00}, /* I */
    {0x1C,0x10,0x10,0x11,0x0E,0x00,0x00}, /* J */
    {0x11,0x09,0x07,0x09,0x11,0x00,0x00}, /* K */
    {0x01,0x01,0x01,0x01,0x1F,0x00,0x00}, /* L */
    {0x11,0x1B,0x15,0x11,0x11,0x00,0x00}, /* M */
    {0x11,0x13,0x15,0x19,0x11,0x00,0x00}, /* N */
    {0x0E,0x11,0x11,0x11,0x0E,0x00,0x00}, /* O */
    {0x0F,0x11,0x0F,0x01,0x01,0x00,0x00}, /* P */
    {0x0E,0x11,0x11,0x09,0x16,0x00,0x00}, /* Q */
    {0x0F,0x11,0x0F,0x09,0x11,0x00,0x00}, /* R */
    {0x0E,0x01,0x0E,0x10,0x0E,0x00,0x00}, /* S */
    {0x1F,0x04,0x04,0x04,0x04,0x00,0x00}, /* T */
    {0x11,0x11,0x11,0x11,0x0E,0x00,0x00}, /* U */
    {0x11,0x11,0x11,0x0A,0x04,0x00,0x00}, /* V */
    {0x11,0x11,0x15,0x1B,0x11,0x00,0x00}, /* W */
    {0x11,0x0A,0x04,0x0A,0x11,0x00,0x00}, /* X */
    {0x11,0x0A,0x04,0x04,0x04,0x00,0x00}, /* Y */
    {0x1F,0x08,0x04,0x02,0x1F,0x00,0x00}, /* Z */
    {0x0E,0x02,0x02,0x02,0x0E,0x00,0x00}, /* [ */
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, /* \ */
    {0x0E,0x08,0x08,0x08,0x0E,0x00,0x00}, /* ] */
    {0x04,0x0A,0x00,0x00,0x00,0x00,0x00}, /* ^ */
    {0x00,0x00,0x00,0x00,0x1F,0x00,0x00}, /* _ */
    {0x02,0x04,0x00,0x00,0x00,0x00,0x00}, /* ` */
    {0x00,0x0E,0x12,0x12,0x1C,0x00,0x00}, /* a */
    {0x01,0x0F,0x11,0x11,0x0F,0x00,0x00}, /* b */
    {0x00,0x0E,0x01,0x01,0x0E,0x00,0x00}, /* c */
    {0x10,0x1E,0x11,0x11,0x1E,0x00,0x00}, /* d */
    {0x00,0x0E,0x1F,0x01,0x0E,0x00,0x00}, /* e */
    {0x0C,0x02,0x07,0x02,0x02,0x00,0x00}, /* f */
    {0x00,0x1E,0x11,0x1E,0x10,0x0E,0x00}, /* g */
    {0x01,0x0F,0x11,0x11,0x11,0x00,0x00}, /* h */
    {0x04,0x00,0x04,0x04,0x04,0x00,0x00}, /* i */
    {0x08,0x00,0x08,0x08,0x09,0x06,0x00}, /* j */
    {0x01,0x09,0x05,0x07,0x09,0x00,0x00}, /* k */
    {0x06,0x04,0x04,0x04,0x0E,0x00,0x00}, /* l */
    {0x00,0x0B,0x15,0x15,0x11,0x00,0x00}, /* m */
    {0x00,0x0F,0x11,0x11,0x11,0x00,0x00}, /* n */
    {0x00,0x0E,0x11,0x11,0x0E,0x00,0x00}, /* o */
    {0x00,0x0F,0x11,0x0F,0x01,0x01,0x00}, /* p */
    {0x00,0x1E,0x11,0x1E,0x10,0x10,0x00}, /* q */
    {0x00,0x0D,0x13,0x01,0x01,0x00,0x00}, /* r */
    {0x00,0x0E,0x02,0x0C,0x0E,0x00,0x00}, /* s */
    {0x02,0x07,0x02,0x02,0x0C,0x00,0x00}, /* t */
    {0x00,0x11,0x11,0x11,0x1E,0x00,0x00}, /* u */
    {0x00,0x11,0x11,0x0A,0x04,0x00,0x00}, /* v */
    {0x00,0x11,0x15,0x15,0x0A,0x00,0x00}, /* w */
    {0x00,0x11,0x0A,0x0A,0x11,0x00,0x00}, /* x */
    {0x00,0x11,0x0A,0x04,0x02,0x01,0x00}, /* y */
    {0x00,0x1F,0x08,0x04,0x1F,0x00,0x00}, /* z */
    {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, /* { */
    {0x04,0x04,0x04,0x04,0x04,0x00,0x00}, /* | */
    {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, /* } */
    {0x00,0x05,0x0A,0x00,0x00,0x00,0x00}, /* ~ */
};

void gui_font_draw_char(GuiBuffer *buf, uint32_t x, uint32_t y,
                        char c, GuiPixel color, uint32_t scale) {
    int idx;
    if (!buf || !buf->pixels || scale == 0) return;
    if (c < 32 || c > 126) return;
    idx = c - 32;
    for (uint32_t row = 0; row < 7; ++row) {
        uint8_t bits = gui_font_5x7[idx][row];
        for (uint32_t col = 0; col < 5; ++col) {
            if (bits & (1u << col)) {
                for (uint32_t sy = 0; sy < scale; ++sy)
                    for (uint32_t sx = 0; sx < scale; ++sx)
                        gui_buffer_set_pixel(buf,
                            x + col * scale + sx,
                            y + row * scale + sy, color);
            }
        }
    }
}

void gui_font_draw_string(GuiBuffer *buf, uint32_t x, uint32_t y,
                          const char *str, GuiPixel color, uint32_t scale) {
    if (!str) return;
    uint32_t cx = x;
    while (*str) {
        gui_font_draw_char(buf, cx, y, *str, color, scale);
        cx += 6 * scale; /* 5 + 1 spacing */
        ++str;
    }
}

uint32_t gui_font_string_width(const char *str, uint32_t scale) {
    if (!str || scale == 0) return 0;
    uint32_t len = (uint32_t)strlen(str);
    if (len == 0) return 0;
    return len * 6 * scale - scale; /* last char has no trailing space */
}

/* ================================================================
 * System Canvas (Inner Layer)
 * ================================================================ */

void gui_sys_init(GuiSystemCanvas *sys, uint32_t w, uint32_t h) {
    if (!sys) return;
    memset(sys, 0, sizeof(*sys));
    sys->canvas_w = w;
    sys->canvas_h = h;
    sys->next_id = 1;
}

GuiElement *gui_sys_add(GuiSystemCanvas *sys, GuiElemType type,
                        GuiRect rect, uint32_t flags) {
    GuiElement *e;
    if (!sys || sys->count >= GUI_MAX_ELEMENTS) return NULL;
    e = &sys->elements[sys->count];
    memset(e, 0, sizeof(*e));
    e->id = sys->next_id++;
    e->type = type;
    e->rect = rect;
    e->flags = flags;
    e->z_order = sys->count;
    e->bg_color = (GuiPixel){200, 200, 200, 255};
    e->fg_color = (GuiPixel){0, 0, 0, 255};
    e->border_color = (GuiPixel){80, 80, 80, 255};
    e->border_width = 1;
    sys->count++;
    return e;
}

GuiElement *gui_sys_find(GuiSystemCanvas *sys, uint32_t id) {
    if (!sys || id == 0) return NULL;
    for (uint32_t i = 0; i < sys->count; ++i)
        if (sys->elements[i].id == id) return &sys->elements[i];
    return NULL;
}

int gui_sys_remove(GuiSystemCanvas *sys, uint32_t id) {
    if (!sys || id == 0) return -1;
    for (uint32_t i = 0; i < sys->count; ++i) {
        if (sys->elements[i].id == id) {
            if (i < sys->count - 1)
                sys->elements[i] = sys->elements[sys->count - 1];
            sys->count--;
            return 0;
        }
    }
    return -1;
}

void gui_sys_move(GuiElement *elem, int32_t x, int32_t y) {
    if (!elem) return;
    elem->rect.x = x;
    elem->rect.y = y;
}

void gui_sys_resize(GuiElement *elem, uint32_t w, uint32_t h) {
    if (!elem) return;
    elem->rect.w = w;
    elem->rect.h = h;
}

void gui_sys_set_z(GuiElement *elem, uint32_t z) {
    if (!elem) return;
    elem->z_order = z;
}

/* z-order sorted index array for rendering */
static void gui_sys_sort_indices(const GuiSystemCanvas *sys,
                                 uint32_t *idx, uint32_t n) {
    /* simple insertion sort — n <= 128 */
    for (uint32_t i = 1; i < n; ++i) {
        uint32_t key = idx[i];
        uint32_t kz = sys->elements[key].z_order;
        int32_t j = (int32_t)i - 1;
        while (j >= 0 && sys->elements[idx[j]].z_order > kz) {
            idx[j + 1] = idx[j];
            --j;
        }
        idx[j + 1] = key;
    }
}

static void gui_render_element(const GuiElement *e, GuiBuffer *target) {
    if (!(e->flags & GUI_ELEM_VISIBLE)) return;

    /* background fill */
    if (e->bg_color.a > 0) {
        gui_buffer_fill_rect(target, e->rect.x, e->rect.y,
                             e->rect.w, e->rect.h, e->bg_color);
    }

    /* border */
    if (e->border_width > 0 && e->border_color.a > 0) {
        gui_buffer_rect_outline(target, e->rect.x, e->rect.y,
                                e->rect.w, e->rect.h,
                                e->border_color, e->border_width);
    }

    /* image blit */
    if (e->type == GUI_ELEM_IMAGE && e->image && e->image->pixels) {
        gui_buffer_blit(target, e->image, e->rect.x, e->rect.y);
    }

    /* label text */
    if (e->label[0] != '\0') {
        uint32_t tx = (uint32_t)e->rect.x + 4;
        uint32_t ty = (uint32_t)e->rect.y + (e->rect.h / 2) - 3;
        gui_font_draw_string(target, tx, ty, e->label, e->fg_color, 1);
    }

    /* custom draw */
    if (e->custom_draw) {
        /* cast away const for callback — callback receives mutable elem */
        e->custom_draw((GuiElement *)e, target);
    }
}

void gui_sys_render(const GuiSystemCanvas *sys, GuiBuffer *target) {
    uint32_t idx[GUI_MAX_ELEMENTS];
    if (!sys || !target) return;
    for (uint32_t i = 0; i < sys->count; ++i) idx[i] = i;
    gui_sys_sort_indices(sys, idx, sys->count);
    for (uint32_t i = 0; i < sys->count; ++i)
        gui_render_element(&sys->elements[idx[i]], target);
}

GuiElement *gui_sys_hit_test(GuiSystemCanvas *sys, int32_t x, int32_t y) {
    GuiElement *hit = NULL;
    uint32_t best_z = 0;
    if (!sys) return NULL;
    for (uint32_t i = 0; i < sys->count; ++i) {
        GuiElement *e = &sys->elements[i];
        if (!(e->flags & GUI_ELEM_VISIBLE)) continue;
        if (x >= e->rect.x && x < e->rect.x + (int32_t)e->rect.w &&
            y >= e->rect.y && y < e->rect.y + (int32_t)e->rect.h) {
            if (!hit || e->z_order >= best_z) {
                hit = e;
                best_z = e->z_order;
            }
        }
    }
    return hit;
}

/* ================================================================
 * Home Screen (Outer Layer)
 * ================================================================ */

void gui_home_init(GuiHomeScreen *home, uint32_t screen_w, uint32_t screen_h) {
    if (!home) return;
    memset(home, 0, sizeof(*home));
    home->screen_w = screen_w;
    home->screen_h = screen_h;
    home->icon_size = 48;
    home->margin = 16;
    home->statusbar_h = 24;
    home->statusbar_color = (GuiPixel){40, 40, 50, 255};
    home->bg_color = (GuiPixel){30, 30, 45, 255};

    /* compute grid */
    if (home->icon_size + home->margin > 0) {
        uint32_t cell = home->icon_size + home->margin;
        home->grid_cols = (screen_w - home->margin) / cell;
        home->grid_rows = (screen_h - home->statusbar_h - home->margin) / cell;
        if (home->grid_cols == 0) home->grid_cols = 1;
        if (home->grid_rows == 0) home->grid_rows = 1;
    }
}

int gui_home_add_icon(GuiHomeScreen *home, const char *name,
                      GuiPixel color, uint32_t elem_id) {
    GuiHomeIcon *ic;
    if (!home || home->icon_count >= GUI_HOME_MAX_ICONS) return -1;
    ic = &home->icons[home->icon_count];
    memset(ic, 0, sizeof(*ic));
    if (name) {
        strncpy(ic->name, name, GUI_ICON_LABEL_MAX - 1);
        ic->name[GUI_ICON_LABEL_MAX - 1] = '\0';
    }
    ic->icon_color = color;
    ic->elem_id = elem_id;
    home->icon_count++;
    gui_home_layout(home);
    return 0;
}

void gui_home_layout(GuiHomeScreen *home) {
    if (!home) return;
    uint32_t cell = home->icon_size + home->margin;
    uint32_t start_x = home->margin;
    uint32_t start_y = home->statusbar_h + home->margin;

    for (uint32_t i = 0; i < home->icon_count; ++i) {
        uint32_t col = i % home->grid_cols;
        uint32_t row = i / home->grid_cols;
        home->icons[i].slot.x = (int32_t)(start_x + col * cell);
        home->icons[i].slot.y = (int32_t)(start_y + row * cell);
        home->icons[i].slot.w = home->icon_size;
        home->icons[i].slot.h = home->icon_size;
    }
}

void gui_home_render(const GuiHomeScreen *home,
                     const GuiSystemCanvas *sys, GuiBuffer *target) {
    (void)sys;
    if (!home || !target) return;

    /* background */
    gui_buffer_clear(target, home->bg_color);

    /* status bar */
    gui_buffer_fill_rect(target, 0, 0,
                         home->screen_w, home->statusbar_h,
                         home->statusbar_color);
    /* status bar title */
    {
        GuiPixel white = {255, 255, 255, 255};
        gui_font_draw_string(target, 8, 8, "CanvasOS", white, 1);

        /* right-aligned indicator */
        gui_font_draw_string(target,
            home->screen_w - gui_font_string_width("12:00", 1) - 8,
            8, "12:00", white, 1);
    }

    /* icons */
    for (uint32_t i = 0; i < home->icon_count; ++i) {
        const GuiHomeIcon *ic = &home->icons[i];
        GuiPixel white = {255, 255, 255, 255};

        /* icon background (rounded corners simulated with filled rect) */
        gui_buffer_fill_rect(target,
                             ic->slot.x + 4, ic->slot.y + 4,
                             ic->slot.w - 8, ic->slot.h - 8,
                             ic->icon_color);

        /* icon border highlight */
        gui_buffer_rect_outline(target,
                                ic->slot.x + 4, ic->slot.y + 4,
                                ic->slot.w - 8, ic->slot.h - 8,
                                (GuiPixel){255, 255, 255, 60}, 1);

        /* icon initial letter */
        if (ic->name[0]) {
            uint32_t cx = (uint32_t)ic->slot.x + ic->slot.w / 2 - 5;
            uint32_t cy = (uint32_t)ic->slot.y + ic->slot.h / 2 - 7;
            gui_font_draw_char(target, cx, cy, ic->name[0], white, 2);
        }

        /* label below icon */
        if (ic->name[0]) {
            uint32_t lw = gui_font_string_width(ic->name, 1);
            uint32_t lx = (uint32_t)ic->slot.x + ic->slot.w / 2;
            if (lx > lw / 2) lx -= lw / 2; else lx = 0;
            gui_font_draw_string(target, lx,
                                 (uint32_t)ic->slot.y + ic->slot.h + 2,
                                 ic->name, white, 1);
        }
    }
}

int gui_home_hit_icon(const GuiHomeScreen *home, int32_t x, int32_t y) {
    if (!home) return -1;
    for (uint32_t i = 0; i < home->icon_count; ++i) {
        const GuiHomeIcon *ic = &home->icons[i];
        if (x >= ic->slot.x && x < ic->slot.x + (int32_t)ic->slot.w &&
            y >= ic->slot.y && y < ic->slot.y + (int32_t)ic->slot.h)
            return (int)i;
    }
    return -1;
}

/* ================================================================
 * Event Dispatch
 * ================================================================ */

int gui_event_dispatch(GuiContext *ctx, const GuiEvent *ev) {
    if (!ctx || !ev) return -1;

    if (ev->type == GUI_EVT_TOUCH_DOWN) {
        /* check home icons first */
        int icon = gui_home_hit_icon(&ctx->home, ev->x, ev->y);
        if (icon >= 0) {
            ctx->dirty = 1;
            return icon + 1000; /* icon hit code */
        }
        /* then check system canvas elements */
        GuiElement *hit = gui_sys_hit_test(&ctx->sys, ev->x, ev->y);
        if (hit) {
            ctx->sys.focused_id = hit->id;
            if (hit->flags & GUI_ELEM_DRAGGABLE) {
                ctx->sys.dragging_id = hit->id;
                ctx->sys.drag_ox = ev->x - hit->rect.x;
                ctx->sys.drag_oy = ev->y - hit->rect.y;
            }
            if (hit->on_event) hit->on_event(hit, ev);
            ctx->dirty = 1;
            return (int)hit->id;
        }
    } else if (ev->type == GUI_EVT_TOUCH_MOVE) {
        if (ctx->sys.dragging_id) {
            GuiElement *e = gui_sys_find(&ctx->sys, ctx->sys.dragging_id);
            if (e) {
                e->rect.x = ev->x - ctx->sys.drag_ox;
                e->rect.y = ev->y - ctx->sys.drag_oy;
                ctx->dirty = 1;
            }
        }
    } else if (ev->type == GUI_EVT_TOUCH_UP) {
        if (ctx->sys.dragging_id) {
            GuiElement *e = gui_sys_find(&ctx->sys, ctx->sys.dragging_id);
            if (e && e->on_event) e->on_event(e, ev);
            ctx->sys.dragging_id = 0;
            ctx->dirty = 1;
        }
    }
    return 0;
}

/* ================================================================
 * Top-Level Context
 * ================================================================ */

void gui_init(GuiContext *ctx, uint32_t screen_w, uint32_t screen_h) {
    if (!ctx) return;
    memset(ctx, 0, sizeof(*ctx));
    gui_sys_init(&ctx->sys, screen_w, screen_h);
    gui_home_init(&ctx->home, screen_w, screen_h);
    gui_buffer_init(&ctx->framebuffer, screen_w, screen_h);
    ctx->dirty = 1;
}

void gui_free(GuiContext *ctx) {
    if (!ctx) return;
    gui_buffer_free(&ctx->framebuffer);
}

void gui_render(GuiContext *ctx) {
    if (!ctx) return;
    /* home screen draws background + status bar + icons */
    gui_home_render(&ctx->home, &ctx->sys, &ctx->framebuffer);
    /* system canvas elements drawn on top */
    gui_sys_render(&ctx->sys, &ctx->framebuffer);
    ctx->frame_count++;
    ctx->dirty = 0;
}

int gui_save_bmp(const GuiContext *ctx, const char *path) {
    if (!ctx) return -1;
    return gui_bmp_write(&ctx->framebuffer, path);
}
