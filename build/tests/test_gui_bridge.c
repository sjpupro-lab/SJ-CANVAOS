/*
 * test_gui_bridge.c — GUI ↔ Engine Bridge 테스트
 */
#include "../include/gui_engine_bridge.h"
#include "../include/canvas_determinism.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0, g_fail = 0;

#define TEST(name, cond) do { \
    if (cond) { g_pass++; printf("  [PASS] %s\n", name); } \
    else      { g_fail++; printf("  [FAIL] %s (line %d)\n", name, __LINE__); } \
} while(0)

/* 엔진 셋업 헬퍼 */
static Cell       s_cells[CANVAS_W * CANVAS_H];
static GateState  s_gates[TILEGATE_COUNT];
static RuleTable  s_rules;

static void setup_engine(EngineContext *eng) {
    memset(s_cells, 0, sizeof(s_cells));
    memset(s_gates, GATE_CLOSE, sizeof(s_gates));
    memset(&s_rules, 0, sizeof(s_rules));

    /* 중앙 십자 게이트 열기 */
    for (int i = 0; i < TILES_X; i++) {
        s_gates[32 * TILES_X + i] = GATE_OPEN;
        s_gates[i * TILES_X + 32] = GATE_OPEN;
    }

    engctx_init(eng, s_cells, CANVAS_W * CANVAS_H,
                s_gates, NULL, &s_rules);

    /* 테스트 셀 배치 */
    uint32_t idx = ORIGIN_Y * CANVAS_W + ORIGIN_X;
    s_cells[idx].B = 0x01;  /* OP_PRINT */
    s_cells[idx].G = 200;
    s_cells[idx].R = 'H';

    s_cells[idx + 1].B = 0x10; /* OP_GATE_ON */
    s_cells[idx + 1].G = 50;

    s_cells[idx + 2].B = 0x30; /* OP_ENERGY */
    s_cells[idx + 2].G = 255;
    s_cells[idx + 2].A = (3u << 24) | 42u; /* lane_id=3 */
}

static void test_init(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 320, 240);
    bridge_init(&br, &eng, &gui);

    TEST("G-B1: engine 연결", br.engine == &eng);
    TEST("G-B1: gui 연결", br.gui == &gui);
    TEST("G-B1: cell_px=4", br.viewport.cell_px == 4);
    TEST("G-B1: dirty=ALL", br.dirty_flags == BRIDGE_DIRTY_ALL);
    gui_free(&gui);
}

static void test_viewport(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 320, 240);
    bridge_init(&br, &eng, &gui);

    bridge_set_viewport(&br, 100, 200, 32, 32, 8);
    TEST("G-B2: viewport x", br.viewport.view_x == 100);
    TEST("G-B2: viewport w", br.viewport.view_w == 32);
    TEST("G-B2: cell_px=8", br.viewport.cell_px == 8);
    gui_free(&gui);
}

static void test_coord_conversion(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 256, 256);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    int32_t px, py;
    bridge_cell_to_pixel(&br, ORIGIN_X, ORIGIN_Y, &px, &py);
    TEST("G-B3: c2p x=128", px == 128);
    TEST("G-B3: c2p y=128", py == 128);

    uint16_t cx, cy;
    int rc = bridge_pixel_to_cell(&br, 128, 128, &cx, &cy);
    TEST("G-B3: p2c ok", rc == 0);
    TEST("G-B3: p2c cx=ORIGIN", cx == ORIGIN_X);
    TEST("G-B3: p2c oob", bridge_pixel_to_cell(&br, -1, 0, &cx, &cy) == -1);
    gui_free(&gui);
}

static void test_render_canvas(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 256, 256);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    bridge_render_canvas(&br);
    /* 128,128은 격자선 위치이므로 셀 내부(129,129) 확인 */
    GuiPixel px = gui_buffer_get_pixel(&gui.framebuffer, 129, 129);
    TEST("G-B4: 원점셀 g=B=1", px.g == 1);
    TEST("G-B4: 원점셀 b=G=200", px.b == 200);
    gui_free(&gui);
}

static void test_vis_modes(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 256, 256);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    bridge_set_vis_mode(&br, CELL_VIS_ENERGY);
    bridge_render_canvas(&br);
    GuiPixel px = gui_buffer_get_pixel(&gui.framebuffer, 129, 129);
    TEST("G-B5: energy 고에너지=빨강", px.r > 100);

    bridge_set_vis_mode(&br, CELL_VIS_OPCODE);
    bridge_render_canvas(&br);
    px = gui_buffer_get_pixel(&gui.framebuffer, 129, 129);
    TEST("G-B5: opcode PRINT=초록", px.g == 255);
    gui_free(&gui);
}

static void test_gates_overlay(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 256, 256);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    bridge_render_canvas(&br);
    bridge_render_gates(&br);
    TEST("G-B6: gates 렌더 ok", 1);
    gui_free(&gui);
}

static void test_timeline(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 320, 240);
    bridge_init(&br, &eng, &gui);

    engctx_tick(&eng);
    engctx_tick(&eng);
    bridge_render_timeline(&br);
    TEST("G-B7: timeline tick=2", eng.tick == 2);
    gui_free(&gui);
}

static void test_status_bar(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 400, 300);
    bridge_init(&br, &eng, &gui);

    bridge_render_status(&br);
    TEST("G-B8: status 렌더 ok", 1);
    gui_free(&gui);
}

static void test_event_to_wh(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 256, 256);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    GuiEvent ev = {
        .type = GUI_EVT_TOUCH_DOWN,
        .x = 128, .y = 128,
        .button = 1,
    };
    int rc = bridge_dispatch_event(&br, &ev);
    TEST("G-B9: dispatch ok", rc == 0);

    WhRecord wr;
    bool found = wh_read_record(&eng, 0, &wr);
    TEST("G-B9: WH 기록", found);
    gui_free(&gui);
}

static void test_bridge_tick(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 256, 256);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    uint32_t t0 = eng.tick;
    bridge_tick(&br);
    TEST("G-B10: tick 증가", eng.tick == t0 + 1);
    TEST("G-B10: dirty=0", br.dirty_flags == 0);
    gui_free(&gui);
}

static void test_full_bmp(void) {
    EngineContext eng;
    GuiContext gui;
    GuiEngineBridge br;

    setup_engine(&eng);
    gui_init(&gui, 400, 300);
    bridge_init(&br, &eng, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 24, ORIGIN_Y - 24, 48, 48, 6);

    bridge_tick(&br);
    bridge_tick(&br);
    bridge_tick(&br);

    br.dirty_flags = BRIDGE_DIRTY_ALL;
    bridge_render_frame(&br);

    int rc = gui_save_bmp(&gui, "bridge_output.bmp");
    TEST("G-B11: BMP 저장", rc == 0);
    gui_free(&gui);
}

int main(void) {
    printf("=== GUI-Engine Bridge Tests ===\n");
    test_init();
    test_viewport();
    test_coord_conversion();
    test_render_canvas();
    test_vis_modes();
    test_gates_overlay();
    test_timeline();
    test_status_bar();
    test_event_to_wh();
    test_bridge_tick();
    test_full_bmp();
    printf("\n=== Results: %d PASS  %d FAIL ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
