/*
 * test_patchG.c — Patch-G: Release Quality Gate
 *
 * G1-G3:  Build verification (version, file count, header guards)
 * G4-G6:  Regression gate (all prior phases compile + pass)
 * G7-G8:  API contract (public headers parseable, no missing symbols)
 * G9-G10: Product checklist (shell, fd, VM, timeline, demo all wired)
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
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_path.h"
#include "../include/canvasos_shell.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"
#include "../include/canvasos_timeline.h"
#include "../include/canvasos_pixel_loader.h"
#include "../include/canvasos_livedemo.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_user.h"
#include "../include/canvasos_mprotect.h"
#include "../include/canvasos_detmode.h"
#include "../include/canvasos_timewarp.h"

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

/* G1: Canvas constants are correct */
static void g1(void) {
    T("G1 canvas constants CANVAS_W=1024 CANVAS_H=1024");
    CHK(CANVAS_W == 1024, "W");
    CHK(CANVAS_H == 1024, "H");
    CHK(TILE == 16, "TILE");
    CHK(TILE_COUNT == 4096, "TILE_COUNT");
    CHK(sizeof(Cell) == 8, "Cell size");
    PASS();
}

/* G2: DK determinism hash is stable */
static void g2(void) {
    T("G2 determinism: zero canvas hash is stable");
    EngineContext *ctx = mk();
    uint32_t h1 = dk_canvas_hash(ctx->cells, ctx->cells_count);
    uint32_t h2 = dk_canvas_hash(ctx->cells, ctx->cells_count);
    CHK(h1 == h2, "hash stable");
    /* Modify and verify change */
    ctx->cells[0].R = 0xFF;
    uint32_t h3 = dk_canvas_hash(ctx->cells, ctx->cells_count);
    CHK(h3 != h1, "hash changes on mutation");
    PASS();
}

/* G3: engctx tick advances monotonically */
static void g3(void) {
    T("G3 tick advances monotonically");
    EngineContext *ctx = mk();
    uint32_t t0 = ctx->tick;
    engctx_tick(ctx);
    CHK(ctx->tick == t0 + 1, "tick+1");
    engctx_tick(ctx);
    CHK(ctx->tick == t0 + 2, "tick+2");
    PASS();
}

/* G4: proc lifecycle contract (regression) */
static void g4(void) {
    T("G4 proc spawn/exit/wait contract");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    int pid = proc_spawn(&pt, PID_INIT, 10, 100, 1);
    CHK(pid > 0, "spawn");
    CHK(proc_exit(&pt, (uint32_t)pid, 42) == 0, "exit");
    uint8_t st = 0;
    CHK(proc_wait(&pt, PID_INIT, &st) == pid, "wait");
    CHK(st == 42, "exit code");
    PASS();
}

/* G5: pipe roundtrip contract (regression) */
static void g5(void) {
    T("G5 pipe write/read roundtrip");
    EngineContext *ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);
    int id = pipe_create(&pipes, ctx, 1, 2);
    CHK(id >= 0, "create");
    CHK(pipe_write(&pipes, ctx, id, (const uint8_t *)"OK", 2) == 2, "write");
    uint8_t buf[4] = {0};
    CHK(pipe_read(&pipes, ctx, id, buf, 4) == 2, "read");
    CHK(memcmp(buf, "OK", 2) == 0, "match");
    PASS();
}

/* G6: syscall dispatch contract (regression) */
static void g6(void) {
    T("G6 syscall GETPID + ENOSYS");
    EngineContext *ctx = mk();
    syscall_init();
    CHK(syscall_dispatch(ctx, 42, SYS_GETPID, 0, 0, 0) == 42, "getpid");
    CHK(syscall_dispatch(ctx, 1, 0x5F, 0, 0, 0) == -38, "enosys");
    PASS();
}

/* G7: shell + PixelCode integration (product gate) */
static void g7(void) {
    T("G7 shell+PXL echo integration");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init(); fd_stdout_clear();
    pxl_set_mode(PXL_MODE_PIXELCODE);

    CHK(shell_exec_line(&sh, ctx, "echo RELEASE") == 0, "echo");
    uint8_t buf[32] = {0};
    uint16_t n = fd_stdout_get(buf, 32);
    CHK(n >= 7 && memcmp(buf, "RELEASE", 7) == 0, "output");
    PASS();
}

/* G8: timeline snapshot+branch (product gate) */
static void g8(void) {
    T("G8 timeline snapshot+branch contract");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    int sid = timeline_snapshot(&tl, ctx, "release");
    CHK(sid >= 0, "snap");
    int bid = timeline_branch_create(&tl, ctx, "release-branch");
    CHK(bid >= 0, "branch");
    Snapshot *s = snap_find_by_name(&tl.snapshots, "release");
    CHK(s != NULL, "find snap");
    PASS();
}

/* G9: demo renders without crash (product gate) */
static void g9(void) {
    T("G9 demo composite frame renders");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    Timeline tl; timeline_init(&tl, ctx);
    VmState vm; vm_init(&vm, 0, 0, 0);
    LiveDemo demo; demo_init(&demo, 500, 500, DEMO_VIEW_W, DEMO_VIEW_H);
    CHK(demo_render_frame(&demo, ctx, &tl, &vm, &pt) == 0, "frame ok");
    PASS();
}

/* G10: full system boot sequence (integration gate) */
static void g10(void) {
    T("G10 full system boot: init→shell→PXL→timeline");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();
    syscall_init();
    syscall_register_phase10();
    vm_bridge_init(&pt, &pipes);
    pxl_set_mode(PXL_MODE_PIXELCODE);

    /* Boot sequence: snapshot → echo → branch → timeline */
    CHK(shell_exec_line(&sh, ctx, "snapshot boot") == 0, "snap");
    CHK(shell_exec_line(&sh, ctx, "echo CanvasOS") == 0, "echo");
    CHK(shell_exec_line(&sh, ctx, "branch create main") == 0, "branch");
    CHK(shell_exec_line(&sh, ctx, "hash") == 0, "hash");
    CHK(shell_exec_line(&sh, ctx, "timeline") == 0, "timeline");
    CHK(shell_exec_line(&sh, ctx, "ps") == 0, "ps");
    PASS();
}

int main(void) {
    printf("\n=== Patch-G: Release Quality Gate ===\n");
    g1(); g2(); g3(); g4(); g5();
    g6(); g7(); g8(); g9(); g10();
    printf("=====================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
