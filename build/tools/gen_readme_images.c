/*
 * gen_readme_images.c — README용 설명 이미지 생성
 *
 * 1. Cell ABGR 구조 다이어그램
 * 2. 아키텍처 레이어 다이어그램
 * 3. 실행 파이프라인 (Gate → Scan → Exec → WH → BH)
 * 4. GUI-Engine Bridge 시각화 예시
 */
#include "../include/canvasos_gui.h"
#include <string.h>
#include <stdio.h>

/* ── 색상 팔레트 ── */
static const GuiPixel C_BG     = {15, 15, 25, 255};
static const GuiPixel C_WHITE  = {240, 240, 240, 255};
static const GuiPixel C_GRAY   = {120, 120, 140, 255};
static const GuiPixel C_DGRAY  = {50, 50, 65, 255};
static const GuiPixel C_RED    = {220, 80, 80, 255};
static const GuiPixel C_GREEN  = {80, 220, 120, 255};
static const GuiPixel C_BLUE   = {80, 140, 255, 255};
static const GuiPixel C_YELLOW = {240, 220, 80, 255};
static const GuiPixel C_ORANGE = {255, 160, 60, 255};
static const GuiPixel C_PURPLE = {180, 100, 255, 255};
static const GuiPixel C_CYAN   = {80, 220, 240, 255};
static const GuiPixel C_PINK   = {255, 120, 180, 255};

static void draw_rounded_box(GuiBuffer *buf, int32_t x, int32_t y,
                             uint32_t w, uint32_t h, GuiPixel bg, GuiPixel border) {
    gui_buffer_fill_rect(buf, x+1, y+1, w-2, h-2, bg);
    gui_buffer_rect_outline(buf, x, y, w, h, border, 1);
}

static void draw_arrow_right(GuiBuffer *buf, int32_t x, int32_t y, int32_t len, GuiPixel c) {
    /* 수평 화살표 */
    gui_buffer_fill_rect(buf, x, y, (uint32_t)len, 2, c);
    /* 삼각형 헤드 */
    for (int i = 0; i < 5; i++) {
        gui_buffer_fill_rect(buf, x + len - 1, y - i, 1, 1, c);
        gui_buffer_fill_rect(buf, x + len - 1, y + 2 + i, 1, 1, c);
        gui_buffer_fill_rect(buf, x + len + i, y - (4-i), 1, (uint32_t)(2*(4-i)+2), c);
    }
}

static void draw_arrow_down(GuiBuffer *buf, int32_t x, int32_t y, int32_t len, GuiPixel c) {
    gui_buffer_fill_rect(buf, x, y, 2, (uint32_t)len, c);
    for (int i = 0; i < 5; i++) {
        gui_buffer_fill_rect(buf, x - (4-i), y + len + i, (uint32_t)(2*(4-i)+2), 1, c);
    }
}

/* ════════════════════════════════════════════════
 * Image 1: Cell ABGR Structure (480×200)
 * ════════════════════════════════════════════════ */
static void gen_cell_abgr(void) {
    GuiBuffer buf;
    gui_buffer_init(&buf, 480, 200);
    gui_buffer_clear(&buf, C_BG);

    /* 타이틀 */
    gui_font_draw_string(&buf, 140, 8, "Cell = 8 Bytes (ABGR)", C_WHITE, 2);

    /* 4개 채널 박스 */
    int bx = 20, by = 50, bw = 105, bh = 55, gap = 8;

    /* A channel */
    draw_rounded_box(&buf, bx, by, bw, bh, (GuiPixel){40,30,30,255}, C_RED);
    gui_font_draw_string(&buf, bx+8, by+6, "A  32-bit", C_RED, 2);
    gui_font_draw_string(&buf, bx+8, by+28, "Address", C_WHITE, 1);
    gui_font_draw_string(&buf, bx+8, by+38, "Where?", C_GRAY, 1);

    /* B channel */
    bx += bw + gap;
    draw_rounded_box(&buf, bx, by, bw, bh, (GuiPixel){30,40,30,255}, C_GREEN);
    gui_font_draw_string(&buf, bx+8, by+6, "B  8-bit", C_GREEN, 2);
    gui_font_draw_string(&buf, bx+8, by+28, "Behavior", C_WHITE, 1);
    gui_font_draw_string(&buf, bx+8, by+38, "What?", C_GRAY, 1);

    /* G channel */
    bx += bw + gap;
    draw_rounded_box(&buf, bx, by, bw, bh, (GuiPixel){30,30,45,255}, C_BLUE);
    gui_font_draw_string(&buf, bx+8, by+6, "G  8-bit", C_BLUE, 2);
    gui_font_draw_string(&buf, bx+8, by+28, "State", C_WHITE, 1);
    gui_font_draw_string(&buf, bx+8, by+38, "Energy?", C_GRAY, 1);

    /* R channel */
    bx += bw + gap;
    draw_rounded_box(&buf, bx, by, bw, bh, (GuiPixel){45,35,20,255}, C_ORANGE);
    gui_font_draw_string(&buf, bx+8, by+6, "R  8-bit", C_ORANGE, 2);
    gui_font_draw_string(&buf, bx+8, by+28, "Stream", C_WHITE, 1);
    gui_font_draw_string(&buf, bx+8, by+38, "Data?", C_GRAY, 1);

    /* 하단 설명 */
    gui_font_draw_string(&buf, 20, 120, "Cell = Pixel = Instruction = File Entry", C_YELLOW, 1);
    gui_font_draw_string(&buf, 20, 135, "1024 x 1024 Canvas = 1,048,576 Cells = 8 MB", C_CYAN, 1);
    gui_font_draw_string(&buf, 20, 155, "No separate memory/storage/code segments", C_GRAY, 1);
    gui_font_draw_string(&buf, 20, 170, "Everything lives on the canvas", C_GRAY, 1);

    gui_bmp_write(&buf, "docs/img_cell_abgr.bmp");
    gui_buffer_free(&buf);
    printf("  -> docs/img_cell_abgr.bmp\n");
}

/* ════════════════════════════════════════════════
 * Image 2: Architecture Layers (480×320)
 * ════════════════════════════════════════════════ */
static void gen_architecture(void) {
    GuiBuffer buf;
    gui_buffer_init(&buf, 480, 340);
    gui_buffer_clear(&buf, C_BG);

    gui_font_draw_string(&buf, 130, 8, "CanvasOS Architecture", C_WHITE, 2);

    struct { const char *name; const char *detail; GuiPixel c; } layers[] = {
        {"GUI / Presentation",   "HomeScreen  SystemCanvas  BMP", C_PINK},
        {"Pixel Runtime",        "VM  PixelCode  pixel_loader",   C_PURPLE},
        {"OS Runtime",           "proc  fd  pipe  signal  syscall", C_BLUE},
        {"Temporal Engine",      "WH  BH  branch  merge  timewarp", C_CYAN},
        {"CanvasFS",             "slot  payload  path  metadata",   C_GREEN},
        {"Core Engine",          "scan  gate  lane  determinism",   C_YELLOW},
        {"Canvas Memory",        "1024x1024 Cell[ABGR] = 8 MB",    C_ORANGE},
    };
    int n = 7;
    int lx = 30, ly = 40, lw = 420, lh = 34, gap = 6;

    for (int i = 0; i < n; i++) {
        int y = ly + i * (lh + gap);
        GuiPixel bg = {(uint8_t)(layers[i].c.r/5), (uint8_t)(layers[i].c.g/5),
                       (uint8_t)(layers[i].c.b/5), 255};
        draw_rounded_box(&buf, lx, y, lw, lh, bg, layers[i].c);
        gui_font_draw_string(&buf, lx+10, y+4, layers[i].name, layers[i].c, 2);
        gui_font_draw_string(&buf, lx+10, y+22, layers[i].detail, C_GRAY, 1);
    }

    /* 우측 화살표 */
    gui_font_draw_string(&buf, 30, ly + n*(lh+gap) + 8,
                         "All layers share the same 8MB canvas", C_WHITE, 1);

    gui_bmp_write(&buf, "docs/img_architecture.bmp");
    gui_buffer_free(&buf);
    printf("  -> docs/img_architecture.bmp\n");
}

/* ════════════════════════════════════════════════
 * Image 3: Execution Pipeline (560×180)
 * ════════════════════════════════════════════════ */
static void gen_pipeline(void) {
    GuiBuffer buf;
    gui_buffer_init(&buf, 560, 200);
    gui_buffer_clear(&buf, C_BG);

    gui_font_draw_string(&buf, 140, 8, "Execution Pipeline", C_WHITE, 2);

    struct { const char *name; GuiPixel c; } stages[] = {
        {"Gate",    C_RED},
        {"Scan",    C_ORANGE},
        {"Exec",    C_YELLOW},
        {"Delta",   C_GREEN},
        {"Merge",   C_CYAN},
        {"WH",      C_BLUE},
        {"BH",      C_PURPLE},
    };
    int n = 7;
    int sx = 15, sy = 50, sw = 60, sh = 40;

    for (int i = 0; i < n; i++) {
        int x = sx + i * (sw + 12);
        GuiPixel bg = {(uint8_t)(stages[i].c.r/5), (uint8_t)(stages[i].c.g/5),
                       (uint8_t)(stages[i].c.b/5), 255};
        draw_rounded_box(&buf, x, sy, sw, sh, bg, stages[i].c);
        gui_font_draw_string(&buf, x+6, sy+14, stages[i].name, stages[i].c, 2);

        if (i < n-1) {
            draw_arrow_right(&buf, x+sw+1, sy+18, 8, C_GRAY);
        }
    }

    /* 하단 설명 */
    gui_font_draw_string(&buf, 15, 105, "Gate: O(1) tile filter  |  Scan: Ring/Spiral adaptive", C_GRAY, 1);
    gui_font_draw_string(&buf, 15, 120, "Exec: cell.B opcode    |  Delta: collect changes", C_GRAY, 1);
    gui_font_draw_string(&buf, 15, 135, "Merge: DK-3 sorted     |  WH: 32K event log", C_GRAY, 1);
    gui_font_draw_string(&buf, 15, 150, "BH: auto-compressed history (idle/loop/burst)", C_GRAY, 1);
    gui_font_draw_string(&buf, 15, 170, "Deterministic: same input = same output, always", C_YELLOW, 1);

    gui_bmp_write(&buf, "docs/img_pipeline.bmp");
    gui_buffer_free(&buf);
    printf("  -> docs/img_pipeline.bmp\n");
}

/* ════════════════════════════════════════════════
 * Image 4: Key Advantages (480×350)
 * ════════════════════════════════════════════════ */
static void gen_advantages(void) {
    GuiBuffer buf;
    gui_buffer_init(&buf, 480, 360);
    gui_buffer_clear(&buf, C_BG);

    gui_font_draw_string(&buf, 100, 8, "CanvasOS Key Advantages", C_WHITE, 2);

    struct { const char *icon; const char *title; const char *desc; GuiPixel c; } items[] = {
        {"[V]", "Visualization",  "Cell=Pixel: see everything",   C_PINK},
        {"[T]", "Timeline",       "WH/BH: replay any moment",    C_CYAN},
        {"[L]", "Live Editor",    "Edit cells = edit programs",   C_GREEN},
        {"[S]", "Stability",      "DK rules: deterministic",     C_YELLOW},
        {"[X]", "Security",       "Gate O(1): instant isolate",   C_RED},
        {"[C]", "Compatibility",  "Image format: runs anywhere",  C_BLUE},
    };
    int n = 6;
    int ix = 20, iy = 40, iw = 440, ih = 45, gap = 6;

    for (int i = 0; i < n; i++) {
        int y = iy + i * (ih + gap);
        GuiPixel bg = {(uint8_t)(items[i].c.r/6), (uint8_t)(items[i].c.g/6),
                       (uint8_t)(items[i].c.b/6), 255};
        draw_rounded_box(&buf, ix, y, iw, ih, bg, items[i].c);
        gui_font_draw_string(&buf, ix+8, y+4, items[i].icon, items[i].c, 2);
        gui_font_draw_string(&buf, ix+50, y+4, items[i].title, items[i].c, 2);
        gui_font_draw_string(&buf, ix+50, y+24, items[i].desc, C_GRAY, 1);
    }

    gui_font_draw_string(&buf, 20, iy + n*(ih+gap) + 8,
                         "58 advantages across 6 pillars", C_WHITE, 1);

    gui_bmp_write(&buf, "docs/img_advantages.bmp");
    gui_buffer_free(&buf);
    printf("  -> docs/img_advantages.bmp\n");
}

int main(void) {
    printf("Generating README images...\n");
    gen_cell_abgr();
    gen_architecture();
    gen_pipeline();
    gen_advantages();
    printf("Done!\n");
    return 0;
}
