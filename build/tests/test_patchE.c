/*
 * test_patchE.c — Patch-E TDD: Timewarp + Branch + Merge UX
 *
 * Req TM-E-001: snapshot create/restore
 * Req BR-E-002: branch isolation
 * Req MG-E-003: merge conflict detect
 * Req TM-E-004: file write + restore semantics
 * Req TM-E-005: A/B experiment + rollback
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
#include "../include/canvasos_shell.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"
#include "../include/canvasos_timeline.h"

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

/* ═══════════ Unit: Snapshot ═══════════════════════ */

/* E1: snapshot create stores tick + hash */
static void e1(void) {
    T("E1 snapshot create stores tick + hash");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);

    /* Advance to tick 5 */
    for (int i = 0; i < 4; i++) engctx_tick(ctx);
    CHK(ctx->tick == 5, "tick=5");

    int id = timeline_snapshot(&tl, ctx, "snap1");
    CHK(id >= 0, "created");

    Snapshot *s = snap_find(&tl.snapshots, (uint32_t)id);
    CHK(s != NULL, "found");
    CHK(s->tick == 5, "tick saved");
    CHK(s->canvas_hash != 0, "hash saved");
    CHK(strcmp(s->name, "snap1") == 0, "name");
    PASS();
}

/* E2: snapshot find by name */
static void e2(void) {
    T("E2 snapshot find by name");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);

    timeline_snapshot(&tl, ctx, "alpha");
    timeline_snapshot(&tl, ctx, "beta");

    Snapshot *a = snap_find_by_name(&tl.snapshots, "alpha");
    Snapshot *b = snap_find_by_name(&tl.snapshots, "beta");
    CHK(a != NULL && b != NULL, "both found");
    CHK(a->id != b->id, "different ids");
    CHK(snap_find_by_name(&tl.snapshots, "nope") == NULL, "not found");
    PASS();
}

/* ═══════════ Unit: Branch ═══════════════════════ */

/* E3: branch create + list */
static void e3(void) {
    T("E3 branch create and list");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);

    int rc = timeline_branch_create(&tl, ctx, "branchA");
    CHK(rc >= 0, "create A");
    rc = timeline_branch_create(&tl, ctx, "branchB");
    CHK(rc >= 0, "create B");

    CHK(tl.branches.count >= 2, "2+ branches");
    PASS();
}

/* E4: branch isolation — writes in A don't affect B */
static void e4(void) {
    T("E4 branch isolation (write-set)");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    ws_table_init(&tl.writesets);

    int a_id = timeline_branch_create(&tl, ctx, "A");
    int b_id = timeline_branch_create(&tl, ctx, "B");
    CHK(a_id > 0 && b_id > 0, "created");

    /* Switch to A, write cell 100 */
    timeline_branch_switch(&tl, ctx, (uint32_t)a_id);
    ctx->cells[100].R = 0xAA;
    ws_record(&tl.writesets, (uint32_t)a_id, 100);

    /* Switch to B, write cell 200 */
    timeline_branch_switch(&tl, ctx, (uint32_t)b_id);
    ctx->cells[200].R = 0xBB;
    ws_record(&tl.writesets, (uint32_t)b_id, 200);

    /* No conflict: A wrote 100, B wrote 200 */
    int conflict = ws_detect_conflict(&tl.writesets,
                                      (uint32_t)a_id, (uint32_t)b_id);
    CHK(conflict == 0, "no conflict");
    PASS();
}

/* E5: branch conflict detection */
static void e5(void) {
    T("E5 branch conflict detection");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    ws_table_init(&tl.writesets);

    int a_id = timeline_branch_create(&tl, ctx, "X");
    int b_id = timeline_branch_create(&tl, ctx, "Y");

    /* Both write to cell 500 → conflict */
    ws_record(&tl.writesets, (uint32_t)a_id, 500);
    ws_record(&tl.writesets, (uint32_t)b_id, 500);

    int conflict = ws_detect_conflict(&tl.writesets,
                                      (uint32_t)a_id, (uint32_t)b_id);
    CHK(conflict > 0, "conflict detected");
    PASS();
}

/* ═══════════ Integration: Merge ═══════════════════ */

/* E6: merge non-conflict succeeds */
static void e6(void) {
    T("E6 merge non-conflict succeeds");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    ws_table_init(&tl.writesets);

    int a = timeline_branch_create(&tl, ctx, "M1");
    int b = timeline_branch_create(&tl, ctx, "M2");

    /* A writes cell 10, B writes cell 20 — no overlap */
    timeline_branch_switch(&tl, ctx, (uint32_t)a);
    ctx->cells[10].R = 0x11;
    ws_record(&tl.writesets, (uint32_t)a, 10);

    timeline_branch_switch(&tl, ctx, (uint32_t)b);
    ctx->cells[20].R = 0x22;
    ws_record(&tl.writesets, (uint32_t)b, 20);

    MergeResult mr;
    int rc = timeline_merge(&tl, ctx, (uint32_t)a, (uint32_t)b, &mr);
    CHK(rc == 0, "merge ok");
    CHK(!mr.has_conflict, "no conflict");

    /* Both writes should be visible */
    CHK(ctx->cells[10].R == 0x11, "A's write preserved");
    CHK(ctx->cells[20].R == 0x22, "B's write preserved");
    PASS();
}

/* E7: merge conflict detected */
static void e7(void) {
    T("E7 merge conflict reports correctly");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    ws_table_init(&tl.writesets);

    int a = timeline_branch_create(&tl, ctx, "C1");
    int b = timeline_branch_create(&tl, ctx, "C2");

    /* Both write to same cell */
    ws_record(&tl.writesets, (uint32_t)a, 300);
    ws_record(&tl.writesets, (uint32_t)b, 300);

    MergeResult mr;
    int rc = timeline_merge(&tl, ctx, (uint32_t)a, (uint32_t)b, &mr);
    CHK(rc == 0, "merge completed");
    CHK(mr.has_conflict, "conflict flagged");
    CHK(mr.conflict_count >= 1, "count >= 1");
    PASS();
}

/* ═══════════ Scenario: Timewarp ══════════════════ */

/* E8: timewarp restores canvas state */
static void e8(void) {
    T("E8 timewarp restores state");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);

    /* Snapshot at current tick */
    uint32_t tick_before = ctx->tick;
    uint32_t hash_before = dk_canvas_hash(ctx->cells, ctx->cells_count);
    timeline_snapshot(&tl, ctx, "base");

    /* Modify canvas */
    ctx->cells[0].R = 0xFF;
    ctx->cells[1].R = 0xFE;
    engctx_tick(ctx);

    uint32_t hash_after = dk_canvas_hash(ctx->cells, ctx->cells_count);
    CHK(hash_after != hash_before, "canvas changed");

    /* Timewarp back */
    int rc = timeline_timewarp(&tl, ctx, tick_before);
    CHK(rc == 0, "timewarp ok");
    CHK(ctx->tick == tick_before, "tick restored");
    PASS();
}

/* E9: shell commands work */
static void e9(void) {
    T("E9 shell snapshot/branch/timeline cmds");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    CHK(shell_exec_line(&sh, ctx, "snapshot base") == 0, "snapshot");
    CHK(shell_exec_line(&sh, ctx, "branch create test") == 0, "branch create");
    CHK(shell_exec_line(&sh, ctx, "branch list") == 0, "branch list");
    CHK(shell_exec_line(&sh, ctx, "timeline") == 0, "timeline");
    PASS();
}

/* E10: end-to-end A/B experiment */
static void e10(void) {
    T("E10 A/B experiment + merge");
    EngineContext *ctx = mk();
    Timeline tl; timeline_init(&tl, ctx);
    ws_table_init(&tl.writesets);

    /* Snapshot base state */
    timeline_snapshot(&tl, ctx, "base");

    /* Create two branches */
    int a = timeline_branch_create(&tl, ctx, "expA");
    int b = timeline_branch_create(&tl, ctx, "expB");

    /* Branch A: paint cell 50 red */
    timeline_branch_switch(&tl, ctx, (uint32_t)a);
    ctx->cells[50].R = 0xAA;
    ws_record(&tl.writesets, (uint32_t)a, 50);

    /* Branch B: paint cell 60 blue */
    timeline_branch_switch(&tl, ctx, (uint32_t)b);
    ctx->cells[60].R = 0xBB;
    ws_record(&tl.writesets, (uint32_t)b, 60);

    /* Merge — should succeed (no conflict) */
    MergeResult mr;
    int rc = timeline_merge(&tl, ctx, (uint32_t)a, (uint32_t)b, &mr);
    CHK(rc == 0, "merge ok");
    CHK(!mr.has_conflict, "no conflict");
    CHK(ctx->cells[50].R == 0xAA, "A paint preserved");
    CHK(ctx->cells[60].R == 0xBB, "B paint preserved");
    PASS();
}

int main(void) {
    printf("\n=== Patch-E: Timewarp + Branch + Merge UX ===\n");
    e1(); e2(); e3(); e4(); e5();
    e6(); e7(); e8(); e9(); e10();
    printf("=============================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
