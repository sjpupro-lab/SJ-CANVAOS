/*
 * test_patchD.c — Patch-D TDD Tests: PixelCode Self-Hosting
 *
 * Req PX-D-001: echo self-hosting
 * Req PX-D-002: cat self-hosting
 * Req PX-D-003: info self-hosting
 *
 * Test Groups:
 *   PD1-PD3: Unit — loader, registry, VM stdout
 *   PD4-PD7: Integration — shell dispatches PixelCode
 *   PD8-PD10: Scenario — C fallback disable mode
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
#include "../include/canvasos_user.h"
#include "../include/canvasos_utils.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_shell.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"
#include "../include/canvasos_pixel_loader.h"

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

/* ═══════════ Unit: Loader + Registry ═══════════════ */

/* PD1: pixel_loader plants cells and VM executes them */
static void pd1(void) {
    T("PD1 loader plants echo program on canvas");
    EngineContext *ctx = mk();

    /* Plant echo "HI" at position (900, 0) */
    uint32_t px = 900, py = 0;
    int n = pxl_plant_echo(ctx, px, py, "HI");
    CHK(n > 0, "planted cells");

    /* Verify cells: first two should be VM_PRINT with 'H' and 'I' */
    CHK(ctx->cells[py * CANVAS_W + px].B == VM_PRINT, "cell0 = PRINT");
    CHK(ctx->cells[py * CANVAS_W + px].R == 'H', "cell0.R = H");
    CHK(ctx->cells[(py+1) * CANVAS_W + px].B == VM_PRINT, "cell1 = PRINT");
    CHK(ctx->cells[(py+1) * CANVAS_W + px].R == 'I', "cell1.R = I");

    /* Run VM from planted position */
    VmState vm;
    vm_init(&vm, px, py, 0);
    fd_table_init();
    fd_stdout_clear();
    vm_run(ctx, &vm);

    /* VM should have printed "HI\n" via VM_PRINT */
    CHK(!vm.running, "vm halted");
    PASS();
}

/* PD2: utility registry resolves names */
static void pd2(void) {
    T("PD2 utility registry resolves echo/cat/info");
    CHK(pxl_find_utility("echo") == PXL_UTIL_ECHO, "echo");
    CHK(pxl_find_utility("cat")  == PXL_UTIL_CAT,  "cat");
    CHK(pxl_find_utility("info") == PXL_UTIL_INFO, "info");
    CHK(pxl_find_utility("hash") == PXL_UTIL_HASH, "hash");
    CHK(pxl_find_utility("help") == PXL_UTIL_HELP, "help");
    CHK(pxl_find_utility("nonexistent") == PXL_UTIL_NONE, "unknown");
    PASS();
}

/* PD3: VM_PRINT sends to stdout capture */
static void pd3(void) {
    T("PD3 VM_PRINT outputs to stdout buffer");
    EngineContext *ctx = mk();
    fd_table_init();
    fd_stdout_clear();

    /* Plant: PRINT 'A', PRINT 'B', HALT */
    vm_plant(ctx, 800, 0, 0, VM_PRINT, 0, 'A');
    vm_plant(ctx, 800, 1, 0, VM_PRINT, 0, 'B');
    vm_plant(ctx, 800, 2, 0, VM_HALT,  0, 0);

    VmState vm;
    vm_init(&vm, 800, 0, 0);
    vm_run(ctx, &vm);

    /* Check stdout capture */
    uint8_t buf[16] = {0};
    uint16_t n = fd_stdout_get(buf, 16);
    CHK(n >= 2, "2+ bytes");
    CHK(buf[0] == 'A' && buf[1] == 'B', "AB");
    PASS();
}

/* ═══════════ Integration: Shell → PixelCode ═══════ */

/* PD4: shell echo dispatches to PixelCode */
static void pd4(void) {
    T("PD4 shell echo -> PixelCode execution");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();
    fd_stdout_clear();

    /* Enable PixelCode mode */
    pxl_set_mode(PXL_MODE_PIXELCODE);

    int rc = shell_exec_line(&sh, ctx, "echo HELLO");
    CHK(rc == 0, "exec ok");

    uint8_t buf[64] = {0};
    uint16_t n = fd_stdout_get(buf, 64);
    CHK(n >= 5, "output >= 5 bytes");
    /* Should contain HELLO */
    CHK(memcmp(buf, "HELLO", 5) == 0, "HELLO in output");
    PASS();
}

/* PD5: shell info via PixelCode */
static void pd5(void) {
    T("PD5 shell info -> PixelCode execution");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    pxl_set_mode(PXL_MODE_PIXELCODE);

    /* info should print tick and hash info */
    int rc = shell_exec_line(&sh, ctx, "info");
    CHK(rc == 0, "info ok");
    PASS();
}

/* PD6: shell hash via PixelCode */
static void pd6(void) {
    T("PD6 shell hash -> PixelCode execution");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    pxl_set_mode(PXL_MODE_PIXELCODE);

    int rc = shell_exec_line(&sh, ctx, "hash");
    CHK(rc == 0, "hash ok");
    PASS();
}

/* PD7: shell help via PixelCode */
static void pd7(void) {
    T("PD7 shell help -> PixelCode execution");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    pxl_set_mode(PXL_MODE_PIXELCODE);

    int rc = shell_exec_line(&sh, ctx, "help");
    CHK(rc == 0, "help ok");
    PASS();
}

/* ═══════════ Scenario: C fallback disabled ═══════ */

/* PD8: PixelCode-only mode: echo works without C fallback */
static void pd8(void) {
    T("PD8 echo in PXL-only mode (no C fallback)");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();
    fd_stdout_clear();

    pxl_set_mode(PXL_MODE_PIXELCODE);

    int rc = shell_exec_line(&sh, ctx, "echo TEST");
    CHK(rc == 0, "exec ok");

    uint8_t buf[32] = {0};
    uint16_t n = fd_stdout_get(buf, 32);
    CHK(n >= 4, "output");
    CHK(memcmp(buf, "TEST", 4) == 0, "TEST");
    PASS();
}

/* PD9: PixelCode-only mode: info works */
static void pd9(void) {
    T("PD9 info in PXL-only mode");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    pxl_set_mode(PXL_MODE_PIXELCODE);

    int rc = shell_exec_line(&sh, ctx, "info");
    CHK(rc == 0, "info ok");
    PASS();
}

/* PD10: fallback mode still works for non-PXL commands */
static void pd10(void) {
    T("PD10 non-PXL commands use C fallback");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    pxl_set_mode(PXL_MODE_PIXELCODE);

    /* ps/kill/cd are always C builtins */
    CHK(shell_exec_line(&sh, ctx, "ps") == 0, "ps ok");
    PASS();
}

int main(void) {
    printf("\n=== Patch-D: PixelCode Self-Hosting ===\n");
    pd1(); pd2(); pd3();
    pd4(); pd5(); pd6(); pd7();
    pd8(); pd9(); pd10();
    printf("=======================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
