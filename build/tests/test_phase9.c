/*
 * test_phase9.c — Phase-9 PixelCode VM Tests (20 cases)
 */
#include <stdio.h>
#include <string.h>
#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_pixelcode.h"
#include "../include/canvasos_syscall.h"

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

/* ── Round 1: VM 코어 ─────────────────────────────────── */

/* V1: vm_init 기본값 */
static void v1(void) {
    T("V1 vm_init defaults");
    VmState vm; vm_init(&vm, 100, 200, 5);
    CHK(vm.pc_x == 100, "pc_x"); CHK(vm.pc_y == 200, "pc_y");
    CHK(vm.pid == 5, "pid"); CHK(!vm.running, "not running");
    CHK(vm.sp == 0, "sp=0"); CHK(vm.tick_count == 0, "ticks=0");
    PASS();
}

/* V2: NOP → PC advances */
static void v2(void) {
    T("V2 NOP advances PC Y↓");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    /* cell (512,512) is already 0 = NOP */
    vm.running = true;
    vm_step(ctx, &vm);
    CHK(vm.pc_y == 513, "y=513"); CHK(vm.pc_x == 512, "x=512");
    PASS();
}

/* V3: PRINT outputs R channel */
static void v3(void) {
    T("V3 PRINT outputs R channel");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    vm_plant(ctx, 512, 512, 0, VM_PRINT, 0, 'X');
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);
    vm.running = true;
    vm_step(ctx, &vm); /* PRINT */
    CHK(vm.pc_y == 513, "advanced");
    /* output goes to stdout - can't easily test, check no crash */
    PASS();
}

/* V4: HALT stops execution */
static void v4(void) {
    T("V4 HALT stops VM");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    vm_plant(ctx, 512, 512, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(!vm.running, "stopped"); CHK(vm.tick_count == 1, "1 step");
    PASS();
}

/* V5: HELLO program — PRINT 5 chars + HALT */
static void v5(void) {
    T("V5 HELLO program (5 PRINT + HALT)");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    const char *hello = "HELLO";
    for (int i = 0; i < 5; i++)
        vm_plant(ctx, 512, 512 + (uint32_t)i, 0, VM_PRINT, 0, (uint8_t)hello[i]);
    vm_plant(ctx, 512, 517, 0, VM_HALT, 0, 0);
    printf("\n    output: ");
    vm_run(ctx, &vm);
    printf("\n");
    CHK(!vm.running, "halted"); CHK(vm.tick_count == 6, "6 steps");
    PASS();
}

/* V6: SET writes G,R to target cell */
static void v6(void) {
    T("V6 SET writes to target cell");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t target = 100 * CANVAS_W + 100; /* cell (100,100) */
    vm_plant(ctx, 512, 512, target, VM_SET, 77, 0xAB);
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(ctx->cells[target].G == 77, "G=77");
    CHK(ctx->cells[target].R == 0xAB, "R=AB");
    PASS();
}

/* V7: ADD / SUB modify target G */
static void v7(void) {
    T("V7 ADD/SUB modify target G");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t target = 200 * CANVAS_W + 200;
    ctx->cells[target].G = 50;
    vm_plant(ctx, 512, 512, target, VM_ADD, 0, 30); /* +30 → 80 */
    vm_plant(ctx, 512, 513, target, VM_SUB, 0, 10); /* -10 → 70 */
    vm_plant(ctx, 512, 514, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(ctx->cells[target].G == 70, "G=70");
    PASS();
}

/* V8: CMP sets flag */
static void v8(void) {
    T("V8 CMP sets flag correctly");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t target = 300 * CANVAS_W + 300;
    ctx->cells[target].G = 42;
    vm_plant(ctx, 512, 512, target, VM_CMP, 0, 42); /* equal → flag=1 */
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(vm.flag == 1, "flag=1 (equal)");
    PASS();
}

/* V9: JMP changes PC */
static void v9(void) {
    T("V9 JMP changes PC");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t target_addr = 520 * CANVAS_W + 512; /* → (512,520) */
    vm_plant(ctx, 512, 512, target_addr, VM_JMP, 0, 0);
    vm_plant(ctx, 512, 520, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(vm.pc_y == 520 && vm.pc_x == 512, "jumped to 520");
    PASS();
}

/* V10: JZ — jump when flag=1 (equal) */
static void v10(void) {
    T("V10 JZ conditional jump");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t t = 400 * CANVAS_W + 400;
    ctx->cells[t].G = 99;
    vm_plant(ctx, 512, 512, t, VM_CMP, 0, 99);       /* flag=1 */
    uint32_t jaddr = 520 * CANVAS_W + 512;
    vm_plant(ctx, 512, 513, jaddr, VM_JZ, 0, 0);      /* jump */
    vm_plant(ctx, 512, 514, 0, VM_PRINT, 0, 'N');     /* skipped */
    vm_plant(ctx, 512, 520, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(vm.pc_y == 520, "jumped to 520");
    PASS();
}

/* V11: CALL/RET subroutine */
static void v11(void) {
    T("V11 CALL/RET subroutine");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t sub_addr = 600 * CANVAS_W + 512;
    vm_plant(ctx, 512, 512, sub_addr, VM_CALL, 0, 0);
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);        /* return here → halt */
    vm_plant(ctx, 512, 600, 0, VM_NOP, 0, 0);         /* subroutine body */
    vm_plant(ctx, 512, 601, 0, VM_RET, 0, 0);
    vm_run(ctx, &vm);
    CHK(vm.pc_y == 513, "returned to 513"); /* HALT at 513 */
    PASS();
}

/* V12: LOAD / STORE */
static void v12(void) {
    T("V12 LOAD/STORE register transfer");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t src = 500 * CANVAS_W + 500;
    uint32_t dst = 501 * CANVAS_W + 500;
    ctx->cells[src].G = 88;
    ctx->cells[src].R = 0xCC;
    vm_plant(ctx, 512, 512, src, VM_LOAD, 0, 0);      /* load from src */
    vm_plant(ctx, 512, 513, dst, VM_STORE, 0, 0);     /* store to dst */
    vm_plant(ctx, 512, 514, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(ctx->cells[dst].G == 88, "G copied");
    CHK(ctx->cells[dst].R == 0xCC, "R copied");
    PASS();
}

/* V13: GATE_ON/OFF */
static void v13(void) {
    T("V13 GATE ON/OFF via VM");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint16_t tid = 100;
    CHK(!gate_is_open_tile(ctx, tid), "initially closed");
    vm_plant(ctx, 512, 512, tid, VM_GATE_ON, 0, 0);
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(gate_is_open_tile(ctx, tid), "opened by VM");
    PASS();
}

/* V14: DRAW writes R to target */
static void v14(void) {
    T("V14 DRAW pixel write");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t target = 300 * CANVAS_W + 300;
    vm_plant(ctx, 512, 512, target, VM_DRAW, 0, 0xFF);
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(ctx->cells[target].R == 0xFF, "pixel set");
    PASS();
}

/* V15: RECT fills area */
static void v15(void) {
    T("V15 RECT area fill");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    uint32_t origin = 400 * CANVAS_W + 400;
    /* G = (w-1)<<4 | (h-1) = 3<<4|3 = 0x33 → 4x4 rect */
    vm_plant(ctx, 512, 512, origin, VM_RECT, 0x33, 0xAA);
    vm_plant(ctx, 512, 513, 0, VM_HALT, 0, 0);
    vm_run(ctx, &vm);
    CHK(ctx->cells[400 * CANVAS_W + 400].R == 0xAA, "corner");
    CHK(ctx->cells[403 * CANVAS_W + 403].R == 0xAA, "far corner");
    PASS();
}

/* V16: tick_limit prevents infinite loop */
static void v16(void) {
    T("V16 tick_limit halts infinite loop");
    EngineContext *ctx = mk(); VmState vm;
    vm_init(&vm, 512, 512, 0);
    vm.tick_limit = 10;
    /* all NOPs → runs until limit */
    vm_run(ctx, &vm);
    CHK(!vm.running, "stopped"); CHK(vm.tick_count == 10, "exactly 10");
    PASS();
}

/* ── Round 2: PixelCode 파서 ──────────────────────────── */

/* V17: px commit via ! */
static void v17(void) {
    T("V17 PixelCode B=01 G=100 R='H' !");
    EngineContext *ctx = mk(); VmState vm; PxState px;
    vm_init(&vm, 512, 512, 0); pxstate_init(&px);
    px_exec_line(ctx, &px, &vm, "B=01 G=100 R='H' !");
    uint32_t idx = ORIGIN_Y * CANVAS_W + ORIGIN_X;
    CHK(ctx->cells[idx].B == 0x01, "B=01");
    CHK(ctx->cells[idx].G == 100, "G=100");
    CHK(ctx->cells[idx].R == 'H', "R=H");
    CHK(px.cy == ORIGIN_Y + 1, "cursor advanced");
    PASS();
}

/* V18: px @home movement */
static void v18(void) {
    T("V18 PixelCode @(100,200) cursor move");
    EngineContext *ctx = mk(); VmState vm; PxState px;
    vm_init(&vm, 512, 512, 0); pxstate_init(&px);
    px_exec_line(ctx, &px, &vm, "@(100,200)");
    CHK(px.cx == 100 && px.cy == 200, "moved");
    PASS();
}

/* V19: px R="HELLO" string stream */
static void v19(void) {
    T("V19 PixelCode R=\"HELLO\" string stream");
    EngineContext *ctx = mk(); VmState vm; PxState px;
    vm_init(&vm, 512, 512, 0); pxstate_init(&px);
    px.reg_B = VM_PRINT; px.reg_G = 100;
    px_exec_line(ctx, &px, &vm, "R=\"HELLO\"");
    CHK(ctx->cells[ORIGIN_Y * CANVAS_W + ORIGIN_X].R == 'H', "H");
    CHK(ctx->cells[(ORIGIN_Y+1) * CANVAS_W + ORIGIN_X].R == 'E', "E");
    CHK(ctx->cells[(ORIGIN_Y+4) * CANVAS_W + ORIGIN_X].R == 'O', "O");
    CHK(px.cy == ORIGIN_Y + 5, "5 commits");
    PASS();
}

/* V20: px gate command */
static void v20(void) {
    T("V20 PixelCode gate open/close");
    EngineContext *ctx = mk(); VmState vm; PxState px;
    vm_init(&vm, 512, 512, 0); pxstate_init(&px);
    CHK(!gate_is_open_tile(ctx, 100), "closed");
    px_exec_line(ctx, &px, &vm, "gate 100");
    CHK(gate_is_open_tile(ctx, 100), "opened");
    px_exec_line(ctx, &px, &vm, "gate -100");
    CHK(!gate_is_open_tile(ctx, 100), "closed again");
    PASS();
}

int main(void) {
    printf("\n=== Phase-9 PixelCode VM Tests ===\n");
    v1(); v2(); v3(); v4(); v5(); v6(); v7(); v8(); v9(); v10();
    v11(); v12(); v13(); v14(); v15(); v16(); v17(); v18(); v19(); v20();
    printf("==================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
