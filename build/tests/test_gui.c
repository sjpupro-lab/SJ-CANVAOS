#include "canvasos_gui.h"

#include <stdio.h>
#include <string.h>

static int P = 0, F = 0;
#define T(n)     printf("  %-54s ", n)
#define PASS()   do { printf("PASS\n"); P++; } while(0)
#define FAIL(m)  do { printf("FAIL: %s\n", m); F++; return; } while(0)
#define CHK(c,m) do { if(!(c)) FAIL(m); } while(0)

/* ── G1: buffer init/clear ─────────────────────────────────── */
static void test_buffer_init(void) {
    GuiBuffer buf;
    GuiPixel red = {255, 0, 0, 255};
    T("G1  buffer init + clear");
    CHK(gui_buffer_init(&buf, 16, 16) == 0, "init failed");
    CHK(buf.w == 16 && buf.h == 16, "wrong dims");
    gui_buffer_clear(&buf, red);
    GuiPixel p = gui_buffer_get_pixel(&buf, 0, 0);
    CHK(p.r == 255 && p.g == 0 && p.b == 0, "clear mismatch");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G2: pixel set/get ─────────────────────────────────────── */
static void test_pixel_setget(void) {
    GuiBuffer buf;
    GuiPixel green = {0, 200, 0, 255};
    T("G2  pixel set/get roundtrip");
    gui_buffer_init(&buf, 8, 8);
    gui_buffer_set_pixel(&buf, 3, 5, green);
    GuiPixel p = gui_buffer_get_pixel(&buf, 3, 5);
    CHK(p.g == 200, "pixel mismatch");
    GuiPixel oob = gui_buffer_get_pixel(&buf, 99, 99);
    CHK(oob.a == 0, "oob should be zero");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G3: fill_rect ─────────────────────────────────────────── */
static void test_fill_rect(void) {
    GuiBuffer buf;
    GuiPixel bg = {0, 0, 0, 255};
    GuiPixel blue = {0, 0, 255, 255};
    T("G3  fill_rect region");
    gui_buffer_init(&buf, 32, 32);
    gui_buffer_clear(&buf, bg);
    gui_buffer_fill_rect(&buf, 4, 4, 8, 8, blue);
    GuiPixel inside = gui_buffer_get_pixel(&buf, 6, 6);
    GuiPixel outside = gui_buffer_get_pixel(&buf, 0, 0);
    CHK(inside.b == 255, "inside should be blue");
    CHK(outside.b == 0, "outside should be black");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G4: rect_outline ──────────────────────────────────────── */
static void test_rect_outline(void) {
    GuiBuffer buf;
    GuiPixel bg = {0, 0, 0, 255};
    GuiPixel white = {255, 255, 255, 255};
    T("G4  rect_outline border");
    gui_buffer_init(&buf, 32, 32);
    gui_buffer_clear(&buf, bg);
    gui_buffer_rect_outline(&buf, 4, 4, 16, 16, white, 2);
    GuiPixel border = gui_buffer_get_pixel(&buf, 4, 4);
    GuiPixel center = gui_buffer_get_pixel(&buf, 12, 12);
    CHK(border.r == 255, "border should be white");
    CHK(center.r == 0, "center should be untouched");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G5: blit with alpha ───────────────────────────────────── */
static void test_blit_alpha(void) {
    GuiBuffer dst, src;
    GuiPixel bg = {100, 0, 0, 255};
    GuiPixel semi = {0, 0, 200, 128};
    T("G5  blit alpha composite");
    gui_buffer_init(&dst, 16, 16);
    gui_buffer_init(&src, 4, 4);
    gui_buffer_clear(&dst, bg);
    gui_buffer_clear(&src, semi);
    gui_buffer_blit(&dst, &src, 2, 2);
    GuiPixel p = gui_buffer_get_pixel(&dst, 3, 3);
    CHK(p.r < 100, "red should be reduced by blend");
    CHK(p.b > 0, "blue should appear from src");
    gui_buffer_free(&dst);
    gui_buffer_free(&src);
    PASS();
}

/* ── G6: font render ──────────────────────────────────────── */
static void test_font(void) {
    GuiBuffer buf;
    GuiPixel bg = {0, 0, 0, 255};
    GuiPixel white = {255, 255, 255, 255};
    int found = 0;
    T("G6  font draw char 'A'");
    gui_buffer_init(&buf, 32, 32);
    gui_buffer_clear(&buf, bg);
    gui_font_draw_char(&buf, 4, 4, 'A', white, 2);
    for (uint32_t y = 4; y < 20; ++y)
        for (uint32_t x = 4; x < 16; ++x)
            if (gui_buffer_get_pixel(&buf, x, y).r == 255) found++;
    CHK(found > 10, "font should produce visible pixels");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G7: sys_init + add ────────────────────────────────────── */
static void test_sys_add(void) {
    GuiSystemCanvas sys;
    GuiRect r = {10, 10, 50, 30};
    T("G7  sys_init + add element");
    gui_sys_init(&sys, 320, 240);
    GuiElement *e = gui_sys_add(&sys, GUI_ELEM_PANEL, r, GUI_ELEM_VISIBLE);
    CHK(e != NULL, "add returned NULL");
    CHK(e->id == 1, "first id should be 1");
    CHK(sys.count == 1, "count should be 1");
    PASS();
}

/* ── G8: sys_render ────────────────────────────────────────── */
static void test_sys_render(void) {
    GuiSystemCanvas sys;
    GuiBuffer buf;
    GuiRect r = {5, 5, 20, 20};
    GuiPixel red = {255, 0, 0, 255};
    T("G8  sys_render colored panel");
    gui_sys_init(&sys, 64, 64);
    gui_buffer_init(&buf, 64, 64);
    gui_buffer_clear(&buf, (GuiPixel){0, 0, 0, 255});
    GuiElement *e = gui_sys_add(&sys, GUI_ELEM_PANEL, r, GUI_ELEM_VISIBLE);
    e->bg_color = red;
    gui_sys_render(&sys, &buf);
    GuiPixel p = gui_buffer_get_pixel(&buf, 10, 10);
    CHK(p.r == 255, "panel pixel should be red");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G9: hit_test ──────────────────────────────────────────── */
static void test_hit_test(void) {
    GuiSystemCanvas sys;
    GuiRect r = {10, 10, 40, 40};
    T("G9  sys_hit_test");
    gui_sys_init(&sys, 320, 240);
    gui_sys_add(&sys, GUI_ELEM_PANEL, r, GUI_ELEM_VISIBLE);
    GuiElement *hit = gui_sys_hit_test(&sys, 20, 20);
    CHK(hit != NULL, "should hit element");
    GuiElement *miss = gui_sys_hit_test(&sys, 0, 0);
    CHK(miss == NULL, "should miss outside");
    PASS();
}

/* ── G10: move + resize ────────────────────────────────────── */
static void test_move_resize(void) {
    GuiSystemCanvas sys;
    GuiRect r = {0, 0, 20, 20};
    T("G10 sys_move + resize");
    gui_sys_init(&sys, 320, 240);
    GuiElement *e = gui_sys_add(&sys, GUI_ELEM_PANEL, r, GUI_ELEM_VISIBLE);
    gui_sys_move(e, 50, 60);
    CHK(e->rect.x == 50 && e->rect.y == 60, "move failed");
    gui_sys_resize(e, 100, 80);
    CHK(e->rect.w == 100 && e->rect.h == 80, "resize failed");
    PASS();
}

/* ── G11: z_order ──────────────────────────────────────────── */
static void test_z_order(void) {
    GuiSystemCanvas sys;
    GuiBuffer buf;
    GuiPixel blue = {0, 0, 255, 255};
    GuiPixel red = {255, 0, 0, 255};
    GuiRect r1 = {0, 0, 30, 30};
    GuiRect r2 = {10, 10, 30, 30};
    T("G11 z_order overlap");
    gui_sys_init(&sys, 64, 64);
    gui_buffer_init(&buf, 64, 64);
    gui_buffer_clear(&buf, (GuiPixel){0, 0, 0, 255});
    GuiElement *e1 = gui_sys_add(&sys, GUI_ELEM_PANEL, r1, GUI_ELEM_VISIBLE);
    GuiElement *e2 = gui_sys_add(&sys, GUI_ELEM_PANEL, r2, GUI_ELEM_VISIBLE);
    e1->bg_color = blue;  e1->border_width = 0;
    e2->bg_color = red;   e2->border_width = 0;
    gui_sys_set_z(e1, 0);
    gui_sys_set_z(e2, 10);
    gui_sys_render(&sys, &buf);
    GuiPixel overlap = gui_buffer_get_pixel(&buf, 15, 15);
    CHK(overlap.r == 255 && overlap.b == 0, "higher z should win");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G12: home_init + add_icon ─────────────────────────────── */
static void test_home_add(void) {
    GuiHomeScreen home;
    T("G12 home_init + add_icon");
    gui_home_init(&home, 320, 480);
    CHK(gui_home_add_icon(&home, "Files", (GuiPixel){60, 120, 200, 255}, 0) == 0, "add icon");
    CHK(gui_home_add_icon(&home, "Term", (GuiPixel){200, 60, 60, 255}, 0) == 0, "add icon2");
    CHK(home.icon_count == 2, "count should be 2");
    CHK(home.icons[0].slot.w == home.icon_size, "slot should be laid out");
    PASS();
}

/* ── G13: home_render ──────────────────────────────────────── */
static void test_home_render(void) {
    GuiHomeScreen home;
    GuiBuffer buf;
    T("G13 home_render statusbar + icons");
    gui_home_init(&home, 320, 480);
    gui_buffer_init(&buf, 320, 480);
    gui_home_add_icon(&home, "App1", (GuiPixel){100, 150, 200, 255}, 0);
    gui_home_render(&home, NULL, &buf);
    /* statusbar pixel */
    GuiPixel sb = gui_buffer_get_pixel(&buf, 4, 4);
    CHK(sb.r == home.statusbar_color.r, "statusbar color");
    /* icon area should not be bg color */
    GuiPixel ic = gui_buffer_get_pixel(&buf,
        (uint32_t)home.icons[0].slot.x + home.icon_size / 2,
        (uint32_t)home.icons[0].slot.y + home.icon_size / 2);
    CHK(ic.r != home.bg_color.r || ic.g != home.bg_color.g, "icon should be drawn");
    gui_buffer_free(&buf);
    PASS();
}

/* ── G14: event touch callback ─────────────────────────────── */
static int g14_called = 0;
static void g14_cb(GuiElement *elem, const GuiEvent *ev) {
    (void)elem; (void)ev;
    g14_called = 1;
}
static void test_event_touch(void) {
    GuiContext ctx;
    GuiRect r = {10, 10, 40, 40};
    GuiEvent ev = {GUI_EVT_TOUCH_DOWN, 20, 20, 0, 0, 0};
    T("G14 event dispatch touch callback");
    gui_init(&ctx, 320, 240);
    GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_BUTTON, r,
                                GUI_ELEM_VISIBLE | GUI_ELEM_FOCUSABLE);
    e->on_event = g14_cb;
    gui_event_dispatch(&ctx, &ev);
    CHK(g14_called == 1, "callback not invoked");
    CHK(ctx.sys.focused_id == e->id, "focus not set");
    gui_free(&ctx);
    PASS();
}

/* ── G15: event drag ───────────────────────────────────────── */
static void test_event_drag(void) {
    GuiContext ctx;
    GuiRect r = {10, 10, 40, 40};
    T("G15 event dispatch drag");
    gui_init(&ctx, 320, 240);
    GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_PANEL, r,
                                GUI_ELEM_VISIBLE | GUI_ELEM_DRAGGABLE);
    GuiEvent down = {GUI_EVT_TOUCH_DOWN, 20, 20, 0, 0, 0};
    gui_event_dispatch(&ctx, &down);
    CHK(ctx.sys.dragging_id == e->id, "drag not started");
    GuiEvent move = {GUI_EVT_TOUCH_MOVE, 50, 60, 0, 0, 0};
    gui_event_dispatch(&ctx, &move);
    CHK(e->rect.x == 40 && e->rect.y == 50, "drag position wrong");
    GuiEvent up = {GUI_EVT_TOUCH_UP, 50, 60, 0, 0, 0};
    gui_event_dispatch(&ctx, &up);
    CHK(ctx.sys.dragging_id == 0, "drag not ended");
    gui_free(&ctx);
    PASS();
}

/* ── G16: full pipeline BMP ────────────────────────────────── */
static void test_full_pipeline(void) {
    GuiContext ctx;
    GuiRect panel_r = {20, 40, 120, 80};
    GuiRect btn_r = {40, 60, 80, 30};
    T("G16 full pipeline → BMP output");
    gui_init(&ctx, 320, 480);

    /* home icons */
    gui_home_add_icon(&ctx.home, "Files",
                      (GuiPixel){60, 120, 220, 255}, 0);
    gui_home_add_icon(&ctx.home, "Terminal",
                      (GuiPixel){50, 50, 50, 255}, 0);
    gui_home_add_icon(&ctx.home, "Canvas",
                      (GuiPixel){180, 80, 40, 255}, 0);
    gui_home_add_icon(&ctx.home, "Stream",
                      (GuiPixel){40, 160, 80, 255}, 0);
    gui_home_add_icon(&ctx.home, "Settings",
                      (GuiPixel){130, 130, 130, 255}, 0);
    gui_home_add_icon(&ctx.home, "VM",
                      (GuiPixel){160, 60, 160, 255}, 0);

    /* system canvas elements */
    GuiElement *panel = gui_sys_add(&ctx.sys, GUI_ELEM_PANEL, panel_r,
                                    GUI_ELEM_VISIBLE | GUI_ELEM_DRAGGABLE);
    panel->bg_color = (GuiPixel){50, 80, 140, 200};
    panel->border_color = (GuiPixel){100, 150, 220, 255};
    panel->border_width = 2;
    strncpy(panel->label, "SJ Panel", GUI_LABEL_MAX);

    GuiElement *btn = gui_sys_add(&ctx.sys, GUI_ELEM_BUTTON, btn_r,
                                  GUI_ELEM_VISIBLE | GUI_ELEM_FOCUSABLE);
    btn->bg_color = (GuiPixel){220, 60, 60, 255};
    btn->fg_color = (GuiPixel){255, 255, 255, 255};
    btn->border_width = 1;
    strncpy(btn->label, "Run", GUI_LABEL_MAX);
    gui_sys_set_z(btn, 10);

    gui_render(&ctx);
    int rc = gui_save_bmp(&ctx, "test_gui_output.bmp");
    CHK(rc == 0, "bmp write failed");
    gui_free(&ctx);
    PASS();
}

/* ── G17: BMP header validation ────────────────────────────── */
static void test_bmp_header(void) {
    FILE *fp;
    uint8_t hdr[54];
    uint32_t w, h;
    T("G17 bmp header validation");
    fp = fopen("test_gui_output.bmp", "rb");
    CHK(fp != NULL, "bmp file not found");
    CHK(fread(hdr, 1, 54, fp) == 54, "header short read");
    fclose(fp);
    CHK(hdr[0] == 'B' && hdr[1] == 'M', "bad magic");
    w = (uint32_t)hdr[18] | ((uint32_t)hdr[19]<<8) |
        ((uint32_t)hdr[20]<<16) | ((uint32_t)hdr[21]<<24);
    h = (uint32_t)hdr[22] | ((uint32_t)hdr[23]<<8) |
        ((uint32_t)hdr[24]<<16) | ((uint32_t)hdr[25]<<24);
    CHK(w == 320, "width mismatch");
    CHK(h == 480, "height mismatch");
    remove("test_gui_output.bmp");
    PASS();
}

int main(void) {
    printf("=== CanvasOS GUI System Tests ===\n");
    test_buffer_init();
    test_pixel_setget();
    test_fill_rect();
    test_rect_outline();
    test_blit_alpha();
    test_font();
    test_sys_add();
    test_sys_render();
    test_hit_test();
    test_move_resize();
    test_z_order();
    test_home_add();
    test_home_render();
    test_event_touch();
    test_event_drag();
    test_full_pipeline();
    test_bmp_header();
    printf("─────────────────────────────────────────\n");
    printf("Total: %d PASS  %d FAIL\n", P, F);
    return F > 0 ? 1 : 0;
}
