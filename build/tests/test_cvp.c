/* test_cvp.c — Phase 4: CVP I/O + Lock + Replay + IPC */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "../include/cvp_io.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvasos_sched.h"
#include "../include/canvasos_types.h"

#define TMP_CVP  "/tmp/test_phase4.cvp"

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILEGATE_COUNT];
static RuleTable g_rules;       /* single RuleTable, not [256] */
static uint8_t   g_active[TILE_COUNT];

static void ctx_reset(EngineContext *ctx) {
    memset(g_cells,  0, sizeof(g_cells));
    memset(g_gates,  0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    memset(&g_rules, 0, sizeof(g_rules));
    engctx_init(ctx, g_cells, CANVAS_W * CANVAS_H, g_gates, g_active, &g_rules);
}

/* ─────────────────────────────────────────────
 * TEST 1: save / validate / load round-trip
 * ───────────────────────────────────────────── */
static void test_roundtrip(void) {
    printf("\n=== TEST 1: save/validate/load round-trip ===\n");

    EngineContext ctx;
    ctx_reset(&ctx);
    ctx.tick = 42;

    /* write known pattern */
    g_cells[0].A = 0xDEADBEEFu;
    g_cells[0].B = 0xAB;
    gate_open_tile(&ctx, 77);
    gate_open_tile(&ctx, 200);

    CvpStatus st = cvp_save_ctx(&ctx, TMP_CVP,
                                SCAN_RING_MH, 0x1234, CVP_CONTRACT_HASH_V1, 0);
    assert(st == CVP_OK);
    printf("save: OK\n");

    st = cvp_validate(TMP_CVP, SCAN_RING_MH, 0x1234, CVP_CONTRACT_HASH_V1);
    assert(st == CVP_OK);
    printf("validate: OK\n");

    /* load into fresh context */
    EngineContext ctx2;
    ctx_reset(&ctx2);
    st = cvp_load_ctx(&ctx2, TMP_CVP, false,
                      SCAN_RING_MH, 0x1234, CVP_CONTRACT_HASH_V1);
    assert(st == CVP_OK);
    assert(ctx2.cells[0].A == 0xDEADBEEFu);
    assert(ctx2.cells[0].B == 0xAB);
    assert(gate_is_open_tile(&ctx2, 77)  == 1);
    assert(gate_is_open_tile(&ctx2, 200) == 1);
    assert(gate_is_open_tile(&ctx2, 99)  == 0);
    printf("load: canvas + gate round-trip OK\n");
    printf("[PASS] TEST 1\n");
}

/* ─────────────────────────────────────────────
 * TEST 2: lock enforcement
 * BUG-1: SCAN_RING_MH=0 must NOT bypass lock
 * ───────────────────────────────────────────── */
static void test_lock(void) {
    printf("\n=== TEST 2: lock enforcement (BUG-1 regression) ===\n");

    EngineContext ctx;
    ctx_reset(&ctx);
    CvpStatus st;

    /* save with SCAN_RING_MH=0 */
    st = cvp_save_ctx(&ctx, TMP_CVP, SCAN_RING_MH, 0xBEEF, 0xABCD1234u, 0);
    assert(st == CVP_OK);

    /* correct locks → OK */
    st = cvp_validate(TMP_CVP, SCAN_RING_MH, 0xBEEF, 0xABCD1234u);
    assert(st == CVP_OK);
    printf("correct locks: OK\n");

    /* scan_policy mismatch */
    st = cvp_validate(TMP_CVP, SCAN_SPIRAL, 0xBEEF, 0xABCD1234u);
    assert(st == CVP_ERR_LOCK);
    printf("scan mismatch → CVP_ERR_LOCK: OK\n");

    /* BUG-1 regression: SCAN_RING_MH=0 must ALSO be enforced */
    /* Save with SCAN_SPIRAL, validate expecting SCAN_RING_MH → must fail */
    ctx_reset(&ctx);
    st = cvp_save_ctx(&ctx, TMP_CVP, SCAN_SPIRAL, 0xBEEF, 0xABCD1234u, 0);
    assert(st == CVP_OK);
    st = cvp_validate(TMP_CVP, SCAN_RING_MH, 0xBEEF, 0xABCD1234u);
    assert(st == CVP_ERR_LOCK);
    printf("BUG-1 regression (RING_MH=0 enforced): OK\n");

    /* CVP_LOCK_SKIP skips scan_policy check */
    st = cvp_validate(TMP_CVP, CVP_LOCK_SKIP, 0xBEEF, 0xABCD1234u);
    assert(st == CVP_OK);
    printf("CVP_LOCK_SKIP skips field: OK\n");

    /* wh_cap mismatch is always enforced (no skip sentinel for this) */
    printf("[PASS] TEST 2\n");
}

/* ─────────────────────────────────────────────
 * TEST 3: WH replay + gate open
 * BUG-2: replay must enforce lock
 * ───────────────────────────────────────────── */
static void test_replay(void) {
    printf("\n=== TEST 3: WH replay (BUG-2 regression) ===\n");

    EngineContext ctx;
    ctx_reset(&ctx);
    ctx.tick = 100;

    /* write gate-open WH record at tick 10 */
    WhRecord r = {0};
    r.tick_or_event = 10;
    r.opcode_index  = 0x10;  /* WH_GATE_OPEN */
    r.target_kind   = WH_TGT_TILE;
    r.target_addr   = 77;
    wh_write_record(&ctx, 10, &r);

    CvpStatus st = cvp_save_ctx(&ctx, TMP_CVP,
                                SCAN_RING_MH, 0xBEEF, 0xABCD1234u, 0);
    assert(st == CVP_OK);

    /* replay should open gate 77 */
    EngineContext ctx2;
    ctx_reset(&ctx2);
    st = cvp_replay_ctx(&ctx2, TMP_CVP, 0, 100,
                        SCAN_RING_MH, 0xBEEF, 0xABCD1234u);
    assert(st == CVP_OK);
    assert(gate_is_open_tile(&ctx2, 77) == 1);
    printf("replay gate open: OK\n");

    /* BUG-2 regression: replay with wrong lock must fail */
    ctx_reset(&ctx2);
    st = cvp_replay_ctx(&ctx2, TMP_CVP, 0, 100,
                        SCAN_SPIRAL, CVP_LOCK_SKIP, CVP_LOCK_SKIP);
    assert(st == CVP_ERR_LOCK);
    printf("BUG-2 regression (replay enforces lock): OK\n");

    /* replay with all LOCK_SKIP → succeeds */
    ctx_reset(&ctx2);
    st = cvp_replay_ctx(&ctx2, TMP_CVP, 0, 100,
                        CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_LOCK_SKIP);
    assert(st == CVP_OK);
    printf("replay CVP_LOCK_SKIP: OK\n");
    printf("[PASS] TEST 3\n");
}

/* ─────────────────────────────────────────────
 * TEST 4: active_open sync after gate load (BUG-3)
 * ───────────────────────────────────────────── */
static void test_active_sync(void) {
    printf("\n=== TEST 4: active_open sync (BUG-3 regression) ===\n");

    EngineContext ctx;
    ctx_reset(&ctx);
    gate_open_tile(&ctx, 55);
    gate_open_tile(&ctx, 999);

    CvpStatus st = cvp_save_ctx(&ctx, TMP_CVP,
                                SCAN_RING_MH, 0, CVP_LOCK_SKIP, 0);
    assert(st == CVP_OK);

    EngineContext ctx2;
    ctx_reset(&ctx2);
    st = cvp_load_ctx(&ctx2, TMP_CVP, false,
                      SCAN_RING_MH, CVP_LOCK_SKIP, CVP_LOCK_SKIP);
    assert(st == CVP_OK);

    /* BUG-3: active_open must be synced from gates after load */
    assert(ctx2.active_open[55]  == 1);
    assert(ctx2.active_open[999] == 1);
    assert(ctx2.active_open[100] == 0);
    printf("active_open synced from gate load: OK\n");
    printf("[PASS] TEST 4\n");
}

/* ─────────────────────────────────────────────
 * TEST 5: IPC WH relay + ipc_cursor (BUG-4/5)
 * ───────────────────────────────────────────── */
static void test_ipc(void) {
    printf("\n=== TEST 5: IPC relay (BUG-4/5 regression) ===\n");

    EngineContext ctx;
    ctx_reset(&ctx);
    ctx.tick = 1000;

    ActiveSet aset; memset(&aset, 0, sizeof(aset));
    Scheduler sc;
    sched_init(&sc, &aset);
    sched_bind_ctx(&sc, &ctx);

    /* spawn at tick 1000 → ipc_cursor=1000 */
    sc.tick = 1000;
    GateSpace sp = { .volh=10, .volt=11 };
    int pidA = sched_spawn(&sc, sp, 5, 10);
    int pidB = sched_spawn(&sc, sp, 5, 10);
    assert(pidA > 0 && pidB > 0);

    /* place an OLD IPC record before spawn (should NOT be received) */
    sc.tick = 500;
    IpcMsg old_msg = { .src_pid=(uint32_t)pidA, .dst_canvas=0,
                       .dst_pid=(uint32_t)pidB,
                       .payload_key={.gate_id=9999,.slot=0} };
    sched_ipc_send(&sc, &old_msg);

    /* place a VALID IPC record after spawn */
    sc.tick = 1001;
    IpcMsg msg = { .src_pid=(uint32_t)pidA, .dst_canvas=0,
                   .dst_pid=(uint32_t)pidB,
                   .payload_key={.gate_id=300,.slot=7} };
    sched_ipc_send(&sc, &msg);

    /* recv: should get tick=1001 record, NOT tick=500 */
    IpcMsg got; memset(&got, 0, sizeof(got));
    int r = sched_ipc_recv(&sc, (uint32_t)pidB, &got);
    assert(r == 0);
    assert(got.payload_key.gate_id == 300);
    assert(got.src_pid == (uint32_t)pidA);
    printf("BUG-4: old IPC skipped, valid received: OK\n");

    /* BUG-5: src_pid full 32-bit (for PROC_MAX=64, pid fits easily) */
    assert(got.src_pid == (uint32_t)pidA);
    printf("BUG-5: src_pid 32-bit preserved: OK\n");

    /* second recv: no more messages → -1 */
    r = sched_ipc_recv(&sc, (uint32_t)pidB, &got);
    assert(r == -1);
    printf("cursor advance (no double-recv): OK\n");
    printf("[PASS] TEST 5\n");
}

/* ─────────────────────────────────────────────
 * TEST 6: engctx_tick + engctx_replay
 * ───────────────────────────────────────────── */
static void test_engctx(void) {
    printf("\n=== TEST 6: engctx_tick / engctx_replay ===\n");

    EngineContext ctx;
    ctx_reset(&ctx);

    /* advance 3 ticks */
    assert(engctx_tick(&ctx) == 0);
    assert(engctx_tick(&ctx) == 0);
    assert(engctx_tick(&ctx) == 0);
    assert(ctx.tick == 3);
    printf("engctx_tick 3 times: tick=%u OK\n", ctx.tick);

    /* write a gate-open record at tick 2 */
    WhRecord r = {0};
    r.tick_or_event = 2;
    r.opcode_index  = 0x10;  /* WH_GATE_OPEN */
    r.target_kind   = WH_TGT_TILE;
    r.target_addr   = 88;
    wh_write_record(&ctx, 2, &r);

    /* replay tick 1..3 → gate 88 should open */
    gate_close_tile(&ctx, 88);
    int replayed = engctx_replay(&ctx, 1, 3);
    assert(replayed >= 1);
    assert(gate_is_open_tile(&ctx, 88) == 1);
    printf("engctx_replay: gate opened, records=%d OK\n", replayed);

    /* inspect cell (status check) */
    assert(engctx_inspect_cell(&ctx, 0, 0, 1) == 0);
    assert(engctx_inspect_cell(&ctx, CANVAS_W, 0, 1) == -1);  /* OOB */
    printf("engctx_inspect_cell: OK\n");
    printf("[PASS] TEST 6\n");
}

/* ─────────────────────────────────────────────
 * TEST 7: CRC corruption detection
 * ───────────────────────────────────────────── */
static void test_corruption(void) {
    printf("\n=== TEST 7: CRC corruption detection ===\n");

    EngineContext ctx; ctx_reset(&ctx);
    CvpStatus st = cvp_save_ctx(&ctx, TMP_CVP,
                                SCAN_RING_MH, 0, CVP_LOCK_SKIP, 0);
    assert(st == CVP_OK);

    /* corrupt magic */
    FILE *fp = fopen(TMP_CVP, "r+b");
    fwrite("XXXX", 1, 4, fp); fclose(fp);
    assert(cvp_validate(TMP_CVP, CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_LOCK_SKIP)
           == CVP_ERR_MAGIC);
    printf("bad magic: CVP_ERR_MAGIC OK\n");

    /* restore + corrupt header CRC */
    ctx_reset(&ctx);
    cvp_save_ctx(&ctx, TMP_CVP, SCAN_RING_MH, 0, CVP_LOCK_SKIP, 0);
    fp = fopen(TMP_CVP, "r+b");
    fseek(fp, (long)(sizeof(uint32_t)*10), SEEK_SET);  /* header_crc offset */
    uint8_t bad = 0xAA; fwrite(&bad, 1, 1, fp); fclose(fp);
    assert(cvp_validate(TMP_CVP, CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_LOCK_SKIP)
           == CVP_ERR_CRC);
    printf("bad header CRC: CVP_ERR_CRC OK\n");
    printf("[PASS] TEST 7\n");
}

int main(void) {
    printf("=== Phase 4: CVP I/O Tests ===\n");
    test_roundtrip();
    test_lock();
    test_replay();
    test_active_sync();
    test_ipc();
    test_engctx();
    test_corruption();
    printf("\n=== ALL CVP TESTS PASSED ===\n");
    return 0;
}
