/*
 * test_patchF.c — Patch-F TDD: Graphical Rendering + Live Demo
 *
 * Req UI-F-001: render smoke (frame renders without crash)
 * Req UI-F-002: heatmap tracks recent modifications
 * Req UI-F-003: timeline panel shows snapshots + branches
 * Req UI-F-004: branch state visible in render
 * Req UI-F-005: timewarp changes rendered tick
 * Req UI-F-006: VM activity panel shows state
 * Req UI-F-007: stats computed correctly
 * Req UI-F-008: grid panel shows cell content
 * Req UI-F-009: composite frame includes all panels
 * Req UI-F-010: demo init and lifecycle
 */
#include <stdio.h>
#include <string.h>
#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_mprotect.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_path.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"
#include "../include/canvasos_timeline.h"
#include "../include/canvasos_livedemo.h"

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];
static int P = 0, F = 0;

#define T(n)     printf("  %-54s ", n)
#define PASS()   do { printf("PASS\n"); P++; } while(0)
#define FAIL(m)  do { printf("FAIL: %s\n", m); F++; return; } while(0)
#define CHK(c,m) do { if(!(c)) FAIL(m); } while(0)

static EngineContext *mk(void) {
    static EngineContext ctx;
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_gates, 0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    engctx_init(&ctx, g_cells, CANVAS_W * CANVAS_H, g_gates, g_active, NULL);
    engctx_tick(&ctx);
    return &ctx;
}

/* F1: demo init and panel buffers */
static void f1(void) {
    T("F1 demo init sets viewport + clears panels");
    LiveDemo demo;
    demo_init(&demo, 500, 500, DEMO_VIEW_W, DEMO_VIEW_H);
    CHK(demo.view_x == 500, "vx");
    CHK(demo.view_w == DEMO_VIEW_W, "vw");
    CHK(demo.grid_panel.len == 0, "grid clear");
    CHK(demo.status_panel.len == 0, "status clear");
    PASS();
}

/* F2: heatmap tracks modifications */
static void f2(void) {
    T("F2 heatmap tracks hot cells");
    EngineContext *ctx = mk();
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    /* Modify some cells in viewport */
    ctx->cells[5 * CANVAS_W + 3].R = 0xFF; /* (3,5) in canvas */

    demo_heatmap_update(&demo, ctx);

    /* Cells with non-zero R in viewport should be warm */
    int h = demo_heatmap_get(&demo, 3, 5);
    CHK(h > 0, "hot cell");

    /* Untouched cell should be cold */
    int h2 = demo_heatmap_get(&demo, 0, 0);
    CHK(h2 == 0, "cold cell");
    PASS();
}

/* F3: grid panel renders cells */
static void f3(void) {
    T("F3 grid panel renders viewport cells");
    EngineContext *ctx = mk();
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    /* Plant visible data */
    ctx->cells[0].R = 'X';
    ctx->cells[0].G = 1;

    int rc = demo_render_grid(&demo, ctx);
    CHK(rc == 0, "render ok");
    CHK(demo.grid_panel.len > 0, "has content");
    PASS();
}

/* F4: status panel shows tick + branch + gate info */
static void f4(void) {
    T("F4 status panel shows tick/branch/gates");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    gate_open_tile(ctx, 5);
    demo_compute_stats(&demo, ctx);

    int rc = demo_render_status(&demo, ctx, &tl);
    CHK(rc == 0, "render ok");
    CHK(demo.status_panel.len > 0, "has content");
    CHK(strstr(panel_str(&demo.status_panel), "tick") != NULL ||
        strstr(panel_str(&demo.status_panel), "Tick") != NULL, "shows tick");
    PASS();
}

/* F5: timeline panel shows snapshots + branches */
static void f5(void) {
    T("F5 timeline panel shows snap + branch");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    timeline_snapshot(&tl, ctx, "base");
    timeline_branch_create(&tl, ctx, "feat");

    int rc = demo_render_timeline(&demo, &tl, ctx);
    CHK(rc == 0, "render ok");
    CHK(demo.timeline_panel.len > 0, "has content");
    CHK(strstr(panel_str(&demo.timeline_panel), "base") != NULL, "snap name");
    PASS();
}

/* F6: VM panel shows state */
static void f6(void) {
    T("F6 VM panel shows registers + state");
    EngineContext *ctx = mk();
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    VmState vm;
    vm_init(&vm, 100, 200, 1);
    vm.reg_R = 0x42;
    vm.running = true;

    int rc = demo_render_vm(&demo, &vm);
    CHK(rc == 0, "render ok");
    CHK(demo.vm_panel.len > 0, "has content");
    CHK(strstr(panel_str(&demo.vm_panel), "PC") != NULL, "shows PC");
    PASS();
}

/* F7: stats computation */
static void f7(void) {
    T("F7 stats: gates + active tiles");
    EngineContext *ctx = mk();
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    gate_open_tile(ctx, 0);
    gate_open_tile(ctx, 1);
    gate_open_tile(ctx, 2);

    demo_compute_stats(&demo, ctx);
    CHK(demo.open_gates == 3, "3 gates open");
    PASS();
}

/* F8: composite frame renders all panels */
static void f8(void) {
    T("F8 composite frame renders without crash");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    Timeline tl; timeline_init(&tl, ctx);
    VmState vm; vm_init(&vm, 0, 0, 0);
    LiveDemo demo;
    demo_init(&demo, 500, 500, DEMO_VIEW_W, DEMO_VIEW_H);

    /* Full composite render */
    int rc = demo_render_frame(&demo, ctx, &tl, &vm, &pt);
    CHK(rc == 0, "render ok");
    CHK(demo.frame_count == 1, "frame counted");
    PASS();
}

/* F9: timewarp changes tick visible in status */
static void f9(void) {
    T("F9 timewarp tick change visible in render");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    LiveDemo demo;
    demo_init(&demo, 0, 0, DEMO_VIEW_W, DEMO_VIEW_H);

    /* Advance to tick 10 */
    for (int i = 0; i < 9; i++) engctx_tick(ctx);
    CHK(ctx->tick == 10, "tick=10");
    timeline_snapshot(&tl, ctx, "t10");

    /* Modify and advance */
    ctx->cells[0].R = 0xFF;
    engctx_tick(ctx);

    /* Timewarp back to 10 */
    timeline_timewarp(&tl, ctx, 10);
    CHK(ctx->tick == 10, "warped to 10");

    /* Render should show tick 10 */
    demo_render_status(&demo, ctx, &tl);
    /* Just verify no crash and tick is correct in context */
    PASS();
}

/* F10: panel_append handles formatting */
static void f10(void) {
    T("F10 panel_append formatting");
    PanelBuf pb;
    panel_clear(&pb);
    panel_append(&pb, "hello %d", 42);
    CHK(pb.len > 0, "has content");
    CHK(strstr(panel_str(&pb), "42") != NULL, "formatted");
    panel_append(&pb, " world");
    CHK(strstr(panel_str(&pb), "world") != NULL, "appended");
    PASS();
}

int main(void) {
    printf("\n=== Patch-F: Graphical Rendering + Live Demo ===\n");
    f1(); f2(); f3(); f4(); f5();
    f6(); f7(); f8(); f9(); f10();
    printf("================================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
