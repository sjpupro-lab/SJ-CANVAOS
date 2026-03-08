/*
 * test_delta_merge.c — Δ-Commit + Deterministic Merge 검증
 *
 * T1) Lane은 Canvas 직접 쓰기 금지 → Δ 버퍼에만 기록
 * T2) merge 전 Canvas 불변, merge 후 반영 확인
 * T3) LaneID 오름차순 merge → LAST_WINS (결정론)
 * T4) PRIORITY 정책: 낮은 priority lane 우선
 * T5) ENERGY_MAX 정책: G 채널 큰 값 채택
 * T6) GPU export: DeltaCell flatten 정확성
 * T7) tick_barrier() 결정론: 동일 입력 → 동일 canvas hash
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvas_delta.h"
#include "../include/canvas_determinism.h"
#include "../include/lane_exec.h"
#include "../include/canvas_lane.h"
#include "../include/engine_time.h"
#include "../include/canvasos_gate_ops.h"

/* ── 테스트 환경 ── */
static Cell      gc[1024*1024];
static GateState gg[4096];
static uint8_t   ga[4096];
static Cell      gc2[1024*1024];
static GateState gg2[4096];
static uint8_t   ga2[4096];

static EngineContextV2 make_v2(Cell *c, GateState *g, uint8_t *a) {
    memset(c, 0, sizeof(Cell)*1024*1024);
    memset(g, 0, sizeof(GateState)*4096);
    memset(a, 0, sizeof(uint8_t)*4096);
    EngineContextV2 v2;
    engctx_v2_init(&v2, c, 1024*1024, g, a, NULL);
    return v2;
}

static int pass=0, fail=0;
#define TEST(n)    printf("  [%-48s] ", n)
#define PASS()     do { printf("PASS\n"); pass++; } while(0)
#define FAIL(msg)  do { printf("FAIL: %s\n", msg); fail++; return; } while(0)
#define ASSERT_EQ(a,b,m) do { if((a)!=(b)) FAIL(m); } while(0)
#define ASSERT_NE(a,b,m) do { if((a)==(b)) FAIL(m); } while(0)
#define ASSERT_GT(a,b,m) do { if(!((a)>(b))) FAIL(m); } while(0)

/* ══════════════════════════════
   T1: Δ 버퍼 쓰기 + Canvas 불변
══════════════════════════════ */
static void t1_delta_write_no_canvas_change(void) {
    TEST("T1 lane_write_delta: Canvas 불변 확인");
    EngineContextV2 v2 = make_v2(gc, gg, ga);

    Cell orig = gc[512*1024 + 512];
    Cell new_val = { .A=0x01000000, .B=0x01, .G=100, .R=0x41 };

    lane_write_delta(&v2, 1, 512*1024+512, &new_val);

    /* Canvas는 아직 변하지 않아야 함 */
    ASSERT_EQ(gc[512*1024+512].B, orig.B, "Canvas가 Δ 전에 변경됨 [W-1 위반]");
    /* Δ 버퍼에는 기록됨 */
    ASSERT_EQ(v2.lane_delta[1].count, 1u, "Δ 버퍼에 기록 안 됨");
    ASSERT_EQ(v2.lane_delta[1].cells[0].addr, 512u*1024u+512u, "addr 불일치");

    engctx_v2_free(&v2);
    PASS();
}

/* ══════════════════════════════
   T2: merge 후 Canvas 반영
══════════════════════════════ */
static void t2_merge_applies_delta(void) {
    TEST("T2 merge_tick: Canvas에 Δ 반영");
    EngineContextV2 v2 = make_v2(gc, gg, ga);

    Cell new_val = { .A=0x01000000, .B=0x02, .G=200, .R=0x42 };
    lane_write_delta(&v2, 0, 100*1024+200, &new_val);

    /* merge 전 */
    ASSERT_EQ(gc[100*1024+200].B, 0, "merge 전 Canvas 이미 변경됨");

    merge_tick(V2_CTX(&v2), (uint64_t)v2.base.tick);

    /* merge 후 */
    ASSERT_EQ(gc[100*1024+200].B, 2u, "merge 후 B 채널 불일치");
    ASSERT_EQ(gc[100*1024+200].G, 200u, "merge 후 G 채널 불일치");
    ASSERT_EQ(gc[100*1024+200].R, 0x42u, "merge 후 R 채널 불일치");

    engctx_v2_free(&v2);
    PASS();
}

/* ══════════════════════════════
   T3: LAST_WINS — LaneID 오름차순 → 마지막이 이김
══════════════════════════════ */
static void t3_last_wins_policy(void) {
    TEST("T3 LAST_WINS: 높은 LaneID가 최종값");
    EngineContextV2 v2 = make_v2(gc, gg, ga);
    v2.merge_policy = DELTA_MERGE_LAST_WINS;

    uint32_t addr = 300*1024 + 300;
    Cell c0 = { .A=0, .B=0x10, .G=50,  .R=0x10 };
    Cell c1 = { .A=0, .B=0x20, .G=100, .R=0x20 };
    Cell c2 = { .A=0, .B=0x30, .G=150, .R=0x30 };

    /* lane 0, 1, 2가 동일 addr에 기록 */
    lane_write_delta(&v2, 0, addr, &c0);
    lane_write_delta(&v2, 1, addr, &c1);
    lane_write_delta(&v2, 2, addr, &c2);

    merge_tick(V2_CTX(&v2), 0);

    /* LaneID 오름차순 → lane2가 마지막 → lane2 값 채택 */
    ASSERT_EQ(gc[addr].B, 0x30u, "LAST_WINS: lane2 B 채널 불일치");
    ASSERT_EQ(gc[addr].G, 150u,  "LAST_WINS: lane2 G 채널 불일치");

    engctx_v2_free(&v2);
    PASS();
}

/* ══════════════════════════════
   T4: ENERGY_MAX — G 채널 최댓값 채택
══════════════════════════════ */
static void t4_energy_max_policy(void) {
    TEST("T4 ENERGY_MAX: G 채널 가장 큰 Lane 채택");
    EngineContextV2 v2 = make_v2(gc, gg, ga);
    v2.merge_policy = DELTA_MERGE_ENERGY_MAX;

    uint32_t addr = 400*1024 + 400;
    Cell c0 = { .B=0x01, .G=50,  .R=0x01 };
    Cell c1 = { .B=0x02, .G=200, .R=0x02 };  /* G 최대 */
    Cell c2 = { .B=0x03, .G=80,  .R=0x03 };

    lane_write_delta(&v2, 0, addr, &c0);
    lane_write_delta(&v2, 1, addr, &c1);
    lane_write_delta(&v2, 2, addr, &c2);

    merge_tick(V2_CTX(&v2), 0);

    /* lane1이 G=200으로 최대 → 채택 */
    ASSERT_EQ(gc[addr].G, 200u, "ENERGY_MAX: G=200 아님");

    engctx_v2_free(&v2);
    PASS();
}

/* ══════════════════════════════
   T5: addr 오름차순 정렬 확인
══════════════════════════════ */
static void t5_addr_sort_order(void) {
    TEST("T5 addr 오름차순 정렬 후 merge");
    EngineContextV2 v2 = make_v2(gc, gg, ga);

    /* 역순으로 push */
    uint32_t addrs[] = {999, 500, 1, 9999, 100};
    for (int i = 0; i < 5; i++) {
        Cell c = { .B=(uint8_t)(i+1), .G=(uint8_t)(i*10) };
        lane_write_delta(&v2, 0, addrs[i], &c);
    }

    /* merge → 정렬 후 적용 */
    merge_tick(V2_CTX(&v2), 0);

    /* addrs[]={999,500,1,9999,100}, B=(i+1) = {1,2,3,4,5} */
    ASSERT_EQ(gc[1].B,    3u, "addr=1 B 불일치 (expect 3)");
    ASSERT_EQ(gc[100].B,  5u, "addr=100 B 불일치 (expect 5)");
    ASSERT_EQ(gc[500].B,  2u, "addr=500 B 불일치 (expect 2)");
    ASSERT_EQ(gc[999].B,  1u, "addr=999 B 불일치 (expect 1)");
    ASSERT_EQ(gc[9999].B, 4u, "addr=9999 B 불일치 (expect 4)");

    engctx_v2_free(&v2);
    PASS();
}

/* ══════════════════════════════
   T6: GPU export flatten
══════════════════════════════ */
static void t6_gpu_delta_export(void) {
    TEST("T6 gpu_delta_export: flatten + lane_count");
    EngineContextV2 v2 = make_v2(gc, gg, ga);

    Cell c = { .B=1, .G=10, .R=0x41 };
    lane_write_delta(&v2, 0,  100, &c);
    lane_write_delta(&v2, 0,  200, &c);
    lane_write_delta(&v2, 5,  300, &c);
    lane_write_delta(&v2, 10, 400, &c);
    lane_write_delta(&v2, 10, 500, &c);
    lane_write_delta(&v2, 10, 600, &c);

    GpuDeltaExport ex;
    int r = gpu_delta_export_build(v2.lane_delta, MAX_LANES, &ex);

    ASSERT_EQ(r, 0, "gpu_delta_export_build failed");
    ASSERT_EQ(ex.total, 6u, "total DeltaCell 수 불일치 (expect 6)");
    ASSERT_EQ(ex.lane_count, 3u, "유효 lane 수 불일치 (expect 3)");

    /* lane10의 offset = lane0(2) + lane5(1) = 3 */
    ASSERT_EQ(ex.lanes[2].offset, 3u, "lane10 offset 불일치");
    ASSERT_EQ(ex.lanes[2].count,  3u, "lane10 count 불일치");

    gpu_delta_export_free(&ex);
    engctx_v2_free(&v2);
    PASS();
}

/* ══════════════════════════════
   T7: tick_barrier() 결정론
══════════════════════════════ */
static void t7_tick_barrier_determinism(void) {
    TEST("T7 tick_barrier() 결정론: 동일 입력 → 동일 hash");

    EngineContextV2 a = make_v2(gc,  gg,  ga);
    EngineContextV2 b = make_v2(gc2, gg2, ga2);

    /* 동일 초기 상태 */
    auto void setup(EngineContextV2 *v2);
    void setup(EngineContextV2 *v2) {
        gate_open_tile(V2_CTX(v2), 10);
        uint32_t idx = 512*1024 + 512;
        v2->base.cells[idx].A = lane_set_id(0, 1); /* lane 1 */
        v2->base.cells[idx].B = 0x01;
        v2->base.cells[idx].G = 50;
        V2_CTX(v2)->active_open[tile_id_of_xy(512,512)] = 1;
    }
    setup(&a); setup(&b);

    LaneTable lt_a, lt_b;
    lane_table_init(&lt_a); lane_table_init(&lt_b);
    LaneDesc ld = { .lane_id=1, .flags=LANE_F_ACTIVE };
    lane_register(&lt_a, &ld); lane_register(&lt_b, &ld);

    /* 동일 tick_barrier 3회 */
    for (int i = 0; i < 3; i++) {
        tick_barrier(&a, &lt_a);
        tick_barrier(&b, &lt_b);
    }

    uint32_t ha = dk_canvas_hash(gc,  1024*1024);
    uint32_t hb = dk_canvas_hash(gc2, 1024*1024);

    ASSERT_EQ(ha, hb, "tick_barrier 결정론 실패: hash 불일치");
    ASSERT_NE(ha, 0u, "hash가 0 (아무것도 실행 안 됨)");

    engctx_v2_free(&a);
    engctx_v2_free(&b);
    PASS();
}

int main(void) {
    printf("\n=== Delta-Commit + Merge Tests ===\n");
    t1_delta_write_no_canvas_change();
    t2_merge_applies_delta();
    t3_last_wins_policy();
    t4_energy_max_policy();
    t5_addr_sort_order();
    t6_gpu_delta_export();
    t7_tick_barrier_determinism();
    printf("==================================\n");
    printf("PASS: %d / FAIL: %d\n\n", pass, fail);
    return fail ? 1 : 0;
}
