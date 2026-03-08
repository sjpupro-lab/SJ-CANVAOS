/*
 * demo_patchF.c — CanvasOS Patch-F: Interactive Live Demo
 *
 * Three demo scenarios as specified:
 *   Demo-1: PixelCode execution + file I/O + heatmap
 *   Demo-2: Branch A/B creation + visual differentiation + merge
 *   Demo-3: Timewarp execution + visual tick restoration
 *
 * Build: make demo_patchF
 * Run:   ./demo_patchF
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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
#include "../include/canvasos_shell.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"
#include "../include/canvasos_timeline.h"
#include "../include/canvasos_pixel_loader.h"
#include "../include/canvasos_livedemo.h"

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];

static EngineContext g_ctx;
static ProcTable     g_pt;
static PipeTable     g_pipes;
static Shell         g_sh;
static VmState       g_vm;
static LiveDemo      g_demo;

static void init_system(void) {
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_gates, 0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    engctx_init(&g_ctx, g_cells, CANVAS_W * CANVAS_H, g_gates, g_active, NULL);
    engctx_tick(&g_ctx);

    proctable_init(&g_pt, &g_ctx);
    pipe_table_init(&g_pipes);
    fd_table_init();
    shell_init(&g_sh, &g_pt, &g_pipes, &g_ctx);
    vm_init(&g_vm, 0, 0, PID_SHELL);
    vm_bridge_init(&g_pt, &g_pipes);
    pxl_set_mode(PXL_MODE_PIXELCODE);

    /* Demo viewport centered on origin area */
    demo_init(&g_demo, 500, 500, DEMO_VIEW_W, DEMO_VIEW_H);
}

static void render(void) {
    printf("\033[H\033[J"); /* clear screen */
    demo_render_frame(&g_demo, &g_ctx, &g_sh.timeline, &g_vm, &g_pt);
}

static void demo1_pixelcode_io(void) {
    printf("\n=== Demo 1: PixelCode Execution + File I/O ===\n\n");

    /* Run PixelCode echo — plants cells on canvas */
    shell_exec_line(&g_sh, &g_ctx, "echo Hello CanvasOS!");
    engctx_tick(&g_ctx);

    /* Show system info via PixelCode */
    shell_exec_line(&g_sh, &g_ctx, "info");
    engctx_tick(&g_ctx);

    /* Canvas hash */
    shell_exec_line(&g_sh, &g_ctx, "hash");
    engctx_tick(&g_ctx);

    /* Open some gates for visual effect */
    for (int i = 0; i < 8; i++)
        gate_open_tile(&g_ctx, (uint16_t)(i + 10));

    /* Render the state */
    demo_init(&g_demo, 896, 0, DEMO_VIEW_W, DEMO_VIEW_H); /* near PXL_PROG_X */
    render();

    printf("\n[Demo 1 complete — PixelCode utilities executed on canvas]\n");
}

static void demo2_branch_merge(void) {
    printf("\n=== Demo 2: Branch A/B + Merge ===\n\n");

    /* Snapshot baseline */
    shell_exec_line(&g_sh, &g_ctx, "snapshot baseline");

    /* Create branch A */
    shell_exec_line(&g_sh, &g_ctx, "branch create alpha");
    /* Paint some cells in branch A area */
    for (int i = 0; i < 8; i++) {
        g_ctx.cells[505 * CANVAS_W + 500 + i].R = 'A';
        g_ctx.cells[505 * CANVAS_W + 500 + i].G = 200;
    }
    engctx_tick(&g_ctx);

    /* Create branch B */
    shell_exec_line(&g_sh, &g_ctx, "branch create beta");
    /* Paint different cells in branch B area */
    for (int i = 0; i < 8; i++) {
        g_ctx.cells[510 * CANVAS_W + 500 + i].R = 'B';
        g_ctx.cells[510 * CANVAS_W + 500 + i].G = 150;
    }
    engctx_tick(&g_ctx);

    /* Show branches */
    shell_exec_line(&g_sh, &g_ctx, "branch list");

    /* Render with both branch effects visible */
    demo_init(&g_demo, 498, 503, DEMO_VIEW_W, DEMO_VIEW_H);
    render();

    /* Merge */
    printf("\n[Merging branches...]\n");
    shell_exec_line(&g_sh, &g_ctx, "merge 1 2");

    /* Timeline view */
    shell_exec_line(&g_sh, &g_ctx, "timeline");

    printf("\n[Demo 2 complete — branches created, painted, and merged]\n");
}

static void demo3_timewarp(void) {
    printf("\n=== Demo 3: Timewarp Time Travel ===\n\n");

    /* Record current tick */
    uint32_t save_tick = g_ctx.tick;
    printf("  Current tick: %u\n", save_tick);
    shell_exec_line(&g_sh, &g_ctx, "snapshot before-change");

    /* Make changes */
    for (int y = 0; y < 4; y++)
        for (int x = 0; x < 16; x++) {
            g_ctx.cells[(500+y) * CANVAS_W + (500+x)].R = '#';
            g_ctx.cells[(500+y) * CANVAS_W + (500+x)].G = 255;
        }

    for (int i = 0; i < 5; i++) engctx_tick(&g_ctx);
    printf("  After changes, tick: %u\n", g_ctx.tick);

    /* Render the changed state */
    demo_init(&g_demo, 498, 498, DEMO_VIEW_W, DEMO_VIEW_H);
    render();

    /* Timewarp back */
    printf("\n  [Executing timewarp to tick %u...]\n", save_tick);
    char tw_cmd[64];
    snprintf(tw_cmd, sizeof(tw_cmd), "timewarp %u", save_tick);
    shell_exec_line(&g_sh, &g_ctx, tw_cmd);

    printf("  Restored tick: %u\n", g_ctx.tick);

    /* Show timeline */
    shell_exec_line(&g_sh, &g_ctx, "timeline");

    printf("\n[Demo 3 complete — time travel executed]\n");
}

int main(void) {
    init_system();

    printf("\033[H\033[J"); /* clear screen */
    printf(
        "╔══════════════════════════════════════════════════╗\n"
        "║        CanvasOS Patch-F Live Demo                ║\n"
        "║                                                  ║\n"
        "║  A spatial OS that runs programs on a canvas,    ║\n"
        "║  travels through time, and branches reality.     ║\n"
        "╚══════════════════════════════════════════════════╝\n\n"
    );

    demo1_pixelcode_io();
    printf("\n");
    demo2_branch_merge();
    printf("\n");
    demo3_timewarp();

    printf("\n=== All demos complete ===\n");
    printf("Final state:\n");
    demo_init(&g_demo, 498, 498, DEMO_VIEW_W, DEMO_VIEW_H);
    render();

    return 0;
}
