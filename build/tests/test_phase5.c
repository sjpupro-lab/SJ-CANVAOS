/*
 * tests/test_phase5.c — Phase 5 결정론 + BH 압축 + Merge 테스트
 *
 * 테스트 세트:
 *   T1) lane_tick 필터 — LaneID=1 셀만 실행되는지 검증
 *   T2) DK 해시 — 동일 Canvas → 동일 해시 (결정론)
 *   T3) Merge tick 경계 — tick 불일치 시 abort 방지 확인
 *   T4) Merge ADDITIVE_G — G 채널 누적 + clamp
 *   T5) Merge conflict — LOCK_WINS 충돌 해소
 *   T6) BH-IDLE 감지 — WH 무변화 구간 압축
 *   T7) BH replay — IDLE 복원 = no-op
 *   T8) 결정론 full — Canvas 초기화 → tick → 해시 비교 (2회 동일)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvas_determinism.h"
#include "../include/canvas_lane.h"
#include "../include/canvas_merge.h"
#include "../include/canvas_bh_compress.h"
#include "../include/engine_time.h"

/* ---- 최소 Canvas 환경 (테스트용 1024×1024) ---- */
static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];

static EngineContext make_ctx(void) {
    memset(g_cells,  0, sizeof(g_cells));
    memset(g_gates,  0, sizeof(g_gates)); /* 전부 CLOSE */
    memset(g_active, 0, sizeof(g_active));
    EngineContext ctx;
    engctx_init(&ctx, g_cells, CANVAS_W*CANVAS_H,
                 g_gates, g_active, NULL);
    return ctx;
}

static int pass = 0, fail = 0;
#define TEST(name)   printf("  [%-40s] ", name)
#define PASS()       do { printf("OK\n"); pass++; } while(0)
#define FAIL(msg)    do { printf("FAIL: %s\n", msg); fail++; } while(0)
#define ASSERT_EQ(a,b,msg) do { if((a)!=(b)) { FAIL(msg); return; } } while(0)

/* -------------------------------------------------------
 * T1) lane_tick 필터: LaneID=1 셀만 카운트
 * ------------------------------------------------------- */
static void test_lane_filter(void) {
    TEST("T1 lane_tick A-channel filter");
    EngineContext ctx = make_ctx();

    /* gate 0 OPEN */
    ctx.gates[0] = GATE_OPEN;

    /* 셀 (0,0): LaneID=1, B=1 → 실행 대상 */
    ctx.cells[0].A = (uint32_t)1u << LANE_ID_SHIFT;
    ctx.cells[0].B = 1;

    /* 셀 (1,0): LaneID=0 (기본), B=1 → LaneID 불일치, 건너뜀 */
    ctx.cells[1].A = 0;
    ctx.cells[1].B = 1;

    LaneTable lt;
    lane_table_init(&lt);
    LaneDesc ld = { .lane_id=1, .flags=LANE_F_ACTIVE,
                    .gate_start=0, .gate_count=1 };
    lane_register(&lt, &ld);

    int n = lane_tick(&ctx, &lt, 1);
    ASSERT_EQ(n, 1, "lane_tick should count only LaneID=1 cells");
    PASS();
}

/* -------------------------------------------------------
 * T2) DK 해시 결정론
 * ------------------------------------------------------- */
static void test_dk_hash(void) {
    TEST("T2 dk_canvas_hash determinism");
    EngineContext ctx = make_ctx();

    ctx.cells[0].A = 0x01020304u;
    ctx.cells[0].B = 0xAB;
    ctx.cells[0].G = 0xCD;
    ctx.cells[0].R = 0xEF;

    uint32_t h1 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    uint32_t h2 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    ASSERT_EQ(h1, h2, "same canvas => same hash");

    ctx.cells[0].R = 0xEE; /* 1바이트 변경 */
    uint32_t h3 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    if (h3 == h1) { FAIL("different canvas => different hash"); return; }
    PASS();
}

/* -------------------------------------------------------
 * T3) Merge tick 경계 선언 후 count 초기화 확인
 * ------------------------------------------------------- */
static void test_merge_begin_end(void) {
    TEST("T3 merge_ctx begin/end tick boundary");
    EngineContext ctx = make_ctx();
    ctx.tick = 42;

    MergeCtx mc;
    merge_ctx_begin(&mc, &ctx, merge_config_default());
    ASSERT_EQ(mc.current_tick, 42u, "current_tick must match ctx.tick");
    ASSERT_EQ(mc.count, 0u, "count must start at 0");
    merge_ctx_end(&mc);
    PASS();
}

/* -------------------------------------------------------
 * T4) Merge ADDITIVE_G clamp
 * ------------------------------------------------------- */
static void test_merge_additive_g(void) {
    TEST("T4 merge ADDITIVE_G + DK_CLAMP_U8");
    EngineContext ctx = make_ctx();

    /* 셀 (0,0).G = 200 */
    ctx.cells[0].G = 200;

    MergeConfig cfg = merge_config_default();
    cfg.policy = MERGE_ADDITIVE_G;

    Delta d = {
        .tick=ctx.tick, .x=0, .y=0, .lane_id=0,
        .before_G=200, .after_G=100,   /* 200+100=300 → clamp to 255 */
    };
    merge_run(&ctx, &d, 1, cfg);

    ASSERT_EQ(ctx.cells[0].G, 255u, "G should clamp to 255");
    PASS();
}

/* -------------------------------------------------------
 * T5) Merge LOCK_WINS 충돌
 * ------------------------------------------------------- */
static void test_merge_lock_wins(void) {
    TEST("T5 merge LOCK_WINS conflict resolution");
    EngineContext ctx = make_ctx();
    ctx.cells[0].G = 10;

    MergeConfig cfg = merge_config_default();
    cfg.policy     = MERGE_LOCK_WINS;
    cfg.on_conflict= CONFLICT_IGNORE;

    /* Δ A: gate CLOSE (lock priority) */
    Delta da = { .tick=ctx.tick, .x=0, .y=0,
                 .before_G=10, .after_G=99, .flags=DF_GATE_CLOSE };
    /* Δ B: 일반 쓰기 (같은 셀) */
    Delta db = { .tick=ctx.tick, .x=0, .y=0,
                 .before_G=10, .after_G=50, .flags=0 };

    Delta deltas[2] = {da, db};
    merge_run(&ctx, deltas, 2, cfg);

    /* LOCK_WINS: da의 after_G=99가 적용되어야 함 */
    ASSERT_EQ(ctx.cells[0].G, 99u, "LOCK_WINS: gate close delta should win");
    PASS();
}

/* -------------------------------------------------------
 * T6) BH-IDLE 감지
 * ------------------------------------------------------- */
static void test_bh_idle_detect(void) {
    TEST("T6 BH-IDLE detection");
    EngineContext ctx = make_ctx();

    /* WH를 비워둔 상태 (모든 레코드 NOP) = IDLE 구간 */
    BhSummary s;
    int found = bh_analyze_window(&ctx, 0, BH_IDLE_MIN_TICKS + 1, 0, &s);

    ASSERT_EQ(found, 1, "should detect IDLE");
    ASSERT_EQ(s.rule, (int)BH_RULE_IDLE, "rule must be IDLE");
    PASS();
}

/* -------------------------------------------------------
 * T7) BH-IDLE replay = no-op
 * ------------------------------------------------------- */
static void test_bh_idle_replay(void) {
    TEST("T7 BH-IDLE replay is no-op");
    EngineContext ctx = make_ctx();

    uint32_t h_before = dk_canvas_hash(ctx.cells, ctx.cells_count);

    BhSummary s = {
        .rule=BH_RULE_IDLE, .from_tick=0, .to_tick=100,
        .gate_id=0, .count=100
    };
    int r = bh_replay_summary(&ctx, &s);
    ASSERT_EQ(r, 0, "replay IDLE should return 0");

    uint32_t h_after = dk_canvas_hash(ctx.cells, ctx.cells_count);
    ASSERT_EQ(h_before, h_after, "IDLE replay must not change canvas");
    PASS();
}

/* -------------------------------------------------------
 * T8) 결정론 full round-trip
 * ------------------------------------------------------- */
static void test_determinism_full(void) {
    TEST("T8 determinism: same init => same hash after lane_tick");
    EngineContext ctx = make_ctx();
    LaneTable lt;
    lane_table_init(&lt);

    /* gate 0 열기, LaneID=0 셀 배치 */
    ctx.gates[0] = GATE_OPEN;
    ctx.cells[0].A = 0; /* LaneID=0 */
    ctx.cells[0].B = 1;

    LaneDesc ld = { .lane_id=0, .flags=LANE_F_ACTIVE, .gate_count=0 };
    lane_register(&lt, &ld);

    /* 1회 실행 후 해시 */
    lane_tick(&ctx, &lt, 0);
    uint32_t h1 = dk_canvas_hash(ctx.cells, ctx.cells_count);

    /* 동일 초기 상태로 재설정 후 다시 실행 */
    EngineContext ctx2 = make_ctx();
    ctx2.gates[0] = GATE_OPEN;
    ctx2.cells[0].A = 0;
    ctx2.cells[0].B = 1;
    LaneTable lt2;
    lane_table_init(&lt2);
    lane_register(&lt2, &ld);
    lane_tick(&ctx2, &lt2, 0);
    uint32_t h2 = dk_canvas_hash(ctx2.cells, ctx2.cells_count);

    ASSERT_EQ(h1, h2, "same init + same ops => same hash");
    PASS();
}

/* ---- main ---- */
int main(void) {
    printf("\n=== Phase 5 Tests ===\n");
    test_lane_filter();
    test_dk_hash();
    test_merge_begin_end();
    test_merge_additive_g();
    test_merge_lock_wins();
    test_bh_idle_detect();
    test_bh_idle_replay();
    test_determinism_full();
    printf("=====================\n");
    printf("PASS: %d / FAIL: %d\n\n", pass, fail);
    return fail ? 1 : 0;
}
