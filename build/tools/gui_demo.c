/* gui_demo.c — Generate CanvasOS GUI demo BMP images
 * Build: gcc -std=c11 -Wall -Wextra -Iinclude src/canvasos_gui.c tools/gui_demo.c -o gui_demo
 * Run:   ./gui_demo
 */
#include "canvasos_gui.h"
#include <stdio.h>
#include <string.h>

int main(void) {
    GuiContext ctx;

    /* ── Mobile home screen (320×480) ─────────────────────── */
    gui_init(&ctx, 320, 480);

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
    gui_home_add_icon(&ctx.home, "Compress",
                      (GuiPixel){200, 140, 40, 255}, 0);
    gui_home_add_icon(&ctx.home, "Debug",
                      (GuiPixel){200, 40, 40, 255}, 0);

    /* system layer panels */
    {
        GuiRect r = {160, 200, 140, 100};
        GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_PANEL, r,
                                    GUI_ELEM_VISIBLE | GUI_ELEM_DRAGGABLE);
        e->bg_color = (GuiPixel){40, 60, 120, 180};
        e->border_color = (GuiPixel){100, 160, 255, 255};
        e->border_width = 2;
        strncpy(e->label, "SJ Panel", GUI_LABEL_MAX);
        e->fg_color = (GuiPixel){255, 255, 255, 255};
    }
    {
        GuiRect r = {180, 240, 100, 28};
        GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_BUTTON, r,
                                    GUI_ELEM_VISIBLE | GUI_ELEM_FOCUSABLE);
        e->bg_color = (GuiPixel){220, 60, 60, 255};
        e->fg_color = (GuiPixel){255, 255, 255, 255};
        e->border_width = 1;
        strncpy(e->label, "Run", GUI_LABEL_MAX);
        gui_sys_set_z(e, 10);
    }

    gui_render(&ctx);
    gui_save_bmp(&ctx, "canvasos_gui_mobile.bmp");
    printf("Saved: canvasos_gui_mobile.bmp (320x480)\n");
    gui_free(&ctx);

    /* ── Desktop view (640×480) ───────────────────────────── */
    gui_init(&ctx, 640, 480);

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
    gui_home_add_icon(&ctx.home, "Compress",
                      (GuiPixel){200, 140, 40, 255}, 0);
    gui_home_add_icon(&ctx.home, "Debug",
                      (GuiPixel){200, 40, 40, 255}, 0);
    gui_home_add_icon(&ctx.home, "Tervas",
                      (GuiPixel){80, 180, 180, 255}, 0);
    gui_home_add_icon(&ctx.home, "Spatial",
                      (GuiPixel){140, 100, 200, 255}, 0);

    /* floating panels */
    {
        GuiRect r = {300, 60, 300, 180};
        GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_PANEL, r,
                                    GUI_ELEM_VISIBLE | GUI_ELEM_DRAGGABLE);
        e->bg_color = (GuiPixel){30, 50, 90, 200};
        e->border_color = (GuiPixel){80, 140, 220, 255};
        e->border_width = 2;
        strncpy(e->label, "CanvasOS Dashboard", GUI_LABEL_MAX);
        e->fg_color = (GuiPixel){255, 255, 255, 255};
    }
    {
        GuiRect r = {320, 140, 120, 30};
        GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_BUTTON, r,
                                    GUI_ELEM_VISIBLE);
        e->bg_color = (GuiPixel){40, 160, 80, 255};
        e->fg_color = (GuiPixel){255, 255, 255, 255};
        e->border_width = 1;
        strncpy(e->label, "Start Engine", GUI_LABEL_MAX);
        gui_sys_set_z(e, 10);
    }
    {
        GuiRect r = {460, 140, 120, 30};
        GuiElement *e = gui_sys_add(&ctx.sys, GUI_ELEM_BUTTON, r,
                                    GUI_ELEM_VISIBLE);
        e->bg_color = (GuiPixel){220, 60, 60, 255};
        e->fg_color = (GuiPixel){255, 255, 255, 255};
        e->border_width = 1;
        strncpy(e->label, "Stop", GUI_LABEL_MAX);
        gui_sys_set_z(e, 10);
    }

    gui_render(&ctx);
    gui_save_bmp(&ctx, "canvasos_gui_desktop.bmp");
    printf("Saved: canvasos_gui_desktop.bmp (640x480)\n");
    gui_free(&ctx);

    return 0;
}
