/*
 * test_patchH.c — Patch-H: Deterministic OS Tester
 *
 * 목적: CanvasOS의 결정론(Determinism) 속성을 다각도로 검증하고,
 *       스트레스 테스트 및 벤치마크를 통해 결과를 기록한다.
 *
 * H1:  DK-1 Tick Boundary Guard — 틱 경계 보호 검증
 * H2:  DK-2/4/5 정수 연산, 클램프, 노이즈 흡수 기본 검증
 * H3:  DK-3 Fixed Reduction Order — dk_cell_index 순서 보장
 * H4:  Stress: 동일 입력 N회 반복 → 동일 캔버스 해시
 * H5:  Stress: 100틱 시퀀스 재현성 (두 독립 실행 비교)
 * H6:  Gate + Mutation Order Invariance (DK-3 동일 순서 → 동일 hash)
 * H7:  CVP 저장/복원 결정론 스트레스 (3-save / 3-load / hash 비교)
 * H8:  Timewarp 결정론 — 스냅샷 → 변경 → 되돌리기 후 hash 재현
 * H9:  Benchmark — 단일 스레드 틱 처리량 측정 (ops/s 출력)
 * H10: Full Determinism Regression Gate (DK-1~5 통합 시나리오)
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_timeline.h"
#include "../include/cvp_io.h"

/* ── Static canvas buffers ─────────────────────────────────── */
static Cell      g_cells_a[CANVAS_W * CANVAS_H];
static Cell      g_cells_b[CANVAS_W * CANVAS_H];
static GateState g_gates_a[TILE_COUNT];
static GateState g_gates_b[TILE_COUNT];
static uint8_t   g_active_a[TILE_COUNT];
static uint8_t   g_active_b[TILE_COUNT];

static EngineContext mk_a(void) {
    memset(g_cells_a,  0, sizeof(g_cells_a));
    memset(g_gates_a,  0, sizeof(g_gates_a));
    memset(g_active_a, 0, sizeof(g_active_a));
    EngineContext ctx;
    engctx_init(&ctx, g_cells_a, CANVAS_W * CANVAS_H,
                 g_gates_a, g_active_a, NULL);
    engctx_tick(&ctx);
    return ctx;
}
static EngineContext mk_b(void) {
    memset(g_cells_b,  0, sizeof(g_cells_b));
    memset(g_gates_b,  0, sizeof(g_gates_b));
    memset(g_active_b, 0, sizeof(g_active_b));
    EngineContext ctx;
    engctx_init(&ctx, g_cells_b, CANVAS_W * CANVAS_H,
                 g_gates_b, g_active_b, NULL);
    engctx_tick(&ctx);
    return ctx;
}

/* Reproducible canvas setup (deterministic seed) */
static void setup_canvas(EngineContext *ctx) {
    gate_open_tile(ctx, 10);
    gate_open_tile(ctx, 42);
    gate_open_tile(ctx, 200);

    ctx->cells[256 * CANVAS_W + 256].B = 0x01;
    ctx->cells[256 * CANVAS_W + 256].G = 128;
    ctx->cells[256 * CANVAS_W + 256].R = 0x41;

    ctx->cells[257 * CANVAS_W + 256].B = 0x10;
    ctx->cells[257 * CANVAS_W + 256].G = 200;
    ctx->cells[257 * CANVAS_W + 256].R = 0x42;

    ctx->cells[512 * CANVAS_W + 512].B = 0x02;
    ctx->cells[512 * CANVAS_W + 512].G = 64;
    ctx->cells[512 * CANVAS_W + 512].R = 0xFF;

    engctx_tick(ctx);
    engctx_tick(ctx);
}

/* ── Test infrastructure ───────────────────────────────────── */
static int P = 0, F = 0;

#define T(n)     printf("  %-56s ", n)
#define PASS()   do { printf("PASS\n"); P++; } while(0)
#define FAIL(m)  do { printf("FAIL: %s\n", m); F++; return; } while(0)
#define CHK(c,m) do { if(!(c)) FAIL(m); } while(0)

/* Stringify helper */
#define STRINGIFY2(x) #x
#define STRINGIFY(x)  STRINGIFY2(x)

/* ── H1: DK-1 Tick Boundary Guard ─────────────────────────── */
static void h1(void) {
    T("H1 DK-1: tick boundary guard lifecycle");
    EngineContext ctx = mk_a();

    TickBoundaryGuard g = dk_begin_tick(&ctx, "h1");
    CHK(g.in_commit == true,      "guard starts in_commit");
    CHK(g.start_tick == ctx.tick, "guard records start_tick");

    /* tick must not change while guard is active */
    CHK(ctx.tick == g.start_tick, "tick stable during guard");

    dk_end_tick(&g);
    CHK(g.in_commit == false, "guard cleared after end");

    /* Advance tick and verify guard can be re-entered */
    engctx_tick(&ctx);
    TickBoundaryGuard g2 = dk_begin_tick(&ctx, "h1_2");
    CHK(g2.start_tick == ctx.tick, "guard re-enters at new tick");
    dk_end_tick(&g2);

    /* Multiple guard cycles must all record correct tick */
    for (int i = 0; i < 10; i++) {
        engctx_tick(&ctx);
        TickBoundaryGuard gi = dk_begin_tick(&ctx, "h1_loop");
        CHK(gi.start_tick == ctx.tick, "loop guard tick matches");
        dk_end_tick(&gi);
    }

    PASS();
}

/* ── H2: DK-2/4/5 Integer, Clamp, Noise Primitives ──────── */
static void h2(void) {
    T("H2 DK-2/4/5: integer clamp and noise-floor primitives");

    /* DK_INT — compile-time integer passthrough (C11 _Generic) */
    uint32_t v32 = DK_INT((uint32_t)0xDEADBEEFu);
    CHK(v32 == 0xDEADBEEFu, "DK_INT u32");

    uint8_t v8 = DK_INT((uint8_t)0xABu);
    CHK(v8 == 0xABu, "DK_INT u8");

    /* DK_CLAMP_U8 */
    CHK(DK_CLAMP_U8(0)    == 0,   "clamp_u8 zero");
    CHK(DK_CLAMP_U8(255)  == 255, "clamp_u8 max");
    CHK(DK_CLAMP_U8(256)  == 255, "clamp_u8 overflow");
    CHK(DK_CLAMP_U8(1000) == 255, "clamp_u8 large");
    CHK(DK_CLAMP_U8(128)  == 128, "clamp_u8 mid");

    /* DK_CLAMP_U16 */
    CHK(DK_CLAMP_U16(0)     == 0,     "clamp_u16 zero");
    CHK(DK_CLAMP_U16(65535) == 65535, "clamp_u16 max");
    CHK(DK_CLAMP_U16(65536) == 65535, "clamp_u16 overflow");

    /* DK_CLAMP_U32 */
    CHK(DK_CLAMP_U32(0)              == 0u,          "clamp_u32 zero");
    CHK(DK_CLAMP_U32(0xFFFFFFFFULL)  == 0xFFFFFFFFu, "clamp_u32 max");
    CHK(DK_CLAMP_U32(0x100000000ULL) == 0xFFFFFFFFu, "clamp_u32 overflow");

    /* DK_ABSORB_NOISE (DK-5) */
    CHK(DK_ABSORB_NOISE(10, 10) == 10, "absorb equal");
    uint8_t ab_pm1 = DK_ABSORB_NOISE(10, 11);
    CHK(ab_pm1 == 10 || ab_pm1 == 11, "absorb ±1 in range");
    CHK(DK_ABSORB_NOISE(0, 1)   == 1,  "absorb 0+1");
    uint8_t ab_hi = DK_ABSORB_NOISE(254, 255);
    CHK(ab_hi == 254 || ab_hi == 255,  "absorb near-max ±1");

    /* DK-5 absorption must be deterministic: same in → same out */
    uint8_t ab1 = DK_ABSORB_NOISE(100, 101);
    uint8_t ab2 = DK_ABSORB_NOISE(100, 101);
    CHK(ab1 == ab2, "absorb is deterministic");

    /* Verify 50 random-ish pairs are all deterministic */
    for (uint8_t i = 0; i < 50; i++) {
        uint8_t a = (uint8_t)(i * 5);
        uint8_t b = (uint8_t)(i * 5 + 1);
        CHK(DK_ABSORB_NOISE(a, b) == DK_ABSORB_NOISE(a, b),
            "absorb stable across calls");
    }

    PASS();
}

/* ── H3: DK-3 Fixed Reduction Order ───────────────────────── */
static void h3(void) {
    T("H3 DK-3: dk_cell_index fixed reduction order");

    /* Origin */
    CHK(dk_cell_index(0, 0) == 0u, "origin index");

    /* Row 1 (y=1) starts at CANVAS_W */
    CHK(dk_cell_index(0, 1) == (uint32_t)CANVAS_W, "row1 col0");

    /* Arbitrary cell */
    CHK(dk_cell_index(5, 3) == (uint32_t)(3 * CANVAS_W + 5), "arbitrary");

    /* Last cell */
    uint32_t last = dk_cell_index((uint16_t)(CANVAS_W - 1),
                                  (uint16_t)(CANVAS_H - 1));
    CHK(last == (uint32_t)(CANVAS_W * CANVAS_H - 1), "last cell");

    /* Strict ascending order along row */
    for (uint16_t x = 0; x < 32; x++) {
        CHK(dk_cell_index(x, 0) == (uint32_t)x, "row0 ascending");
    }

    /* Strict ascending order along column */
    for (uint16_t y = 0; y < 32; y++) {
        CHK(dk_cell_index(0, y) == (uint32_t)(y * CANVAS_W), "col0 ascending");
    }

    /* DK-3: index(x,y) < index(x+1,y) and < index(0,y+1) */
    for (uint16_t y = 0; y < 8; y++) {
        for (uint16_t x = 0; x < (uint16_t)(CANVAS_W - 1); x++) {
            CHK(dk_cell_index(x, y) < dk_cell_index((uint16_t)(x + 1), y),
                "strict row order");
        }
        if (y < (uint16_t)(CANVAS_H - 1)) {
            uint32_t row_end  = dk_cell_index((uint16_t)(CANVAS_W - 1), y);
            uint32_t next_row = dk_cell_index(0, (uint16_t)(y + 1));
            CHK(row_end < next_row, "row boundary order");
        }
    }

    /* tile_cell_base: tile_id=0 → cell 0 */
    CHK(dk_tile_cell_base(0) == 0u, "tile0 base");

    /* Verify tile base increases with tile_id */
    for (uint32_t t = 0; t + 1 < 16; t++) {
        CHK(dk_tile_cell_base(t) < dk_tile_cell_base(t + 1),
            "tile base ascending");
    }

    PASS();
}

/* ── H4: Stress — N identical runs → identical hash ───────── */
#define STRESS_RUNS 50
static void h4(void) {
    T("H4 Stress: " STRINGIFY(STRESS_RUNS) " runs → identical hash");
    uint32_t ref_hash = 0;
    for (int i = 0; i < STRESS_RUNS; i++) {
        EngineContext ctx = mk_a();
        setup_canvas(&ctx);
        uint32_t h = dk_canvas_hash(ctx.cells, ctx.cells_count);
        if (i == 0) {
            ref_hash = h;
        } else {
            if (h != ref_hash) {
                printf("FAIL: run %d hash 0x%08x != ref 0x%08x\n",
                       i, h, ref_hash);
                F++;
                return;
            }
        }
    }
    CHK(ref_hash != 0u, "hash not zero");
    PASS();
}

/* ── H5: Stress — 100-tick reproducibility ────────────────── */
#define TICK_STRESS 100
static void h5(void) {
    T("H5 Stress: " STRINGIFY(TICK_STRESS) "-tick reproducibility");

    EngineContext a = mk_a();
    setup_canvas(&a);
    for (int i = 0; i < TICK_STRESS; i++) engctx_tick(&a);
    uint32_t ha = dk_canvas_hash(a.cells, a.cells_count);

    EngineContext b = mk_b();
    gate_open_tile(&b, 10);
    gate_open_tile(&b, 42);
    gate_open_tile(&b, 200);
    b.cells[256 * CANVAS_W + 256].B = 0x01;
    b.cells[256 * CANVAS_W + 256].G = 128;
    b.cells[256 * CANVAS_W + 256].R = 0x41;
    b.cells[257 * CANVAS_W + 256].B = 0x10;
    b.cells[257 * CANVAS_W + 256].G = 200;
    b.cells[257 * CANVAS_W + 256].R = 0x42;
    b.cells[512 * CANVAS_W + 512].B = 0x02;
    b.cells[512 * CANVAS_W + 512].G = 64;
    b.cells[512 * CANVAS_W + 512].R = 0xFF;
    engctx_tick(&b);
    engctx_tick(&b);
    for (int i = 0; i < TICK_STRESS; i++) engctx_tick(&b);
    uint32_t hb = dk_canvas_hash(b.cells, b.cells_count);

    CHK(ha == hb, "100-tick hash mismatch between independent runs");
    CHK(ha != 0u, "hash not zero");
    PASS();
}

/* ── H6: Gate + Mutation Order Invariance ─────────────────── */
static void h6(void) {
    T("H6 DK-3: gate+mutation in fixed order → identical hash");

    /* Context A: apply mutations in DK-3 index order (ascending) */
    EngineContext a = mk_a();
    for (uint16_t y = 0; y < 8; y++) {
        for (uint16_t x = 0; x < 8; x++) {
            uint32_t idx = dk_cell_index(x, y);
            a.cells[idx].G = DK_CLAMP_U8((uint32_t)((y * 8 + x) * 7 + 3));
            a.cells[idx].R = DK_ABSORB_NOISE((uint8_t)(x * 11),
                                             (uint8_t)(x * 11 + 1));
        }
    }
    gate_open_tile(&a, 5);
    gate_open_tile(&a, 15);
    gate_open_tile(&a, 25);
    engctx_tick(&a);
    uint32_t ha = dk_canvas_hash(a.cells, a.cells_count);

    /* Context B: same operations, same order */
    EngineContext b = mk_b();
    for (uint16_t y = 0; y < 8; y++) {
        for (uint16_t x = 0; x < 8; x++) {
            uint32_t idx = dk_cell_index(x, y);
            b.cells[idx].G = DK_CLAMP_U8((uint32_t)((y * 8 + x) * 7 + 3));
            b.cells[idx].R = DK_ABSORB_NOISE((uint8_t)(x * 11),
                                             (uint8_t)(x * 11 + 1));
        }
    }
    gate_open_tile(&b, 5);
    gate_open_tile(&b, 15);
    gate_open_tile(&b, 25);
    engctx_tick(&b);
    uint32_t hb = dk_canvas_hash(b.cells, b.cells_count);

    CHK(ha == hb, "DK-3 ordered mutations: hash mismatch");
    CHK(ha != 0u, "hash not zero");

    /* Verify mutation changed the hash vs empty canvas */
    EngineContext empty = mk_a();
    engctx_tick(&empty);
    uint32_t h_empty = dk_canvas_hash(empty.cells, empty.cells_count);
    CHK(ha != h_empty, "mutations changed canvas hash");

    /* Cross-check: dk_check_determinism confirms */
    CHK(dk_check_determinism(ha, hb, "h6"), "dk_check_determinism");

    PASS();
}

/* ── H7: CVP save/load determinism stress ─────────────────── */
#define CVP_STRESS_ROUNDS 3
static void h7(void) {
    T("H7 CVP stress: " STRINGIFY(CVP_STRESS_ROUNDS)
      "x(save/load) hash stable");

    EngineContext ctx = mk_a();
    setup_canvas(&ctx);
    uint32_t h_orig = dk_canvas_hash(ctx.cells, ctx.cells_count);
    CHK(h_orig != 0u, "initial hash non-zero");

    const char *tmpfile = "/tmp/canvasos_patchH_stress.cvp";

    for (int round = 0; round < CVP_STRESS_ROUNDS; round++) {
        int rc = (int)cvp_save_ctx(&ctx, tmpfile, 0, 0,
                                   CVP_CONTRACT_HASH_V1, 0);
        if (rc != CVP_OK) {
            printf("FAIL: save round %d (rc=%d)\n", round, rc);
            F++;
            remove(tmpfile);
            return;
        }

        EngineContext restored = mk_b();
        rc = (int)cvp_load_ctx(&restored, tmpfile, false,
                               CVP_LOCK_SKIP, CVP_LOCK_SKIP,
                               CVP_CONTRACT_HASH_V1);
        if (rc != CVP_OK) {
            printf("FAIL: load round %d (rc=%d)\n", round, rc);
            F++;
            remove(tmpfile);
            return;
        }

        uint32_t h_loaded = dk_canvas_hash(restored.cells,
                                           restored.cells_count);
        if (h_loaded != h_orig) {
            printf("FAIL: round %d hash 0x%08x != 0x%08x\n",
                   round, h_loaded, h_orig);
            F++;
            remove(tmpfile);
            return;
        }
    }

    remove(tmpfile);
    PASS();
}

/* ── H8: Timewarp determinism ──────────────────────────────── */
static void h8(void) {
    T("H8 Timewarp: snapshot hash invariant + CVP state restore");

    EngineContext ctx = mk_a();
    setup_canvas(&ctx);

    Timeline tl; timeline_init(&tl, &ctx);

    /* Baseline state */
    uint32_t tick_base = ctx.tick;
    uint32_t hash_base = dk_canvas_hash(ctx.cells, ctx.cells_count);
    int sid = timeline_snapshot(&tl, &ctx, "det_base");
    CHK(sid >= 0, "snapshot created");

    /* Snapshot must record exact tick and hash */
    Snapshot *s = snap_find(&tl.snapshots, (uint32_t)sid);
    CHK(s != NULL,                     "snapshot found by id");
    CHK(s->canvas_hash == hash_base,   "snapshot hash matches baseline");
    CHK(s->tick        == tick_base,   "snapshot tick matches baseline");

    /* Mutate canvas and advance ticks */
    for (int i = 0; i < 64; i++) {
        ctx.cells[i].R = (uint8_t)(i & 0xFF);
        ctx.cells[i].G = (uint8_t)((i * 3) & 0xFF);
    }
    engctx_tick(&ctx);
    engctx_tick(&ctx);
    uint32_t hash_mut = dk_canvas_hash(ctx.cells, ctx.cells_count);
    CHK(hash_mut != hash_base, "mutation changed hash");

    /* timeline_timewarp: restores ctx.tick, snapshot hash stays invariant */
    int rc = timeline_timewarp(&tl, &ctx, tick_base);
    CHK(rc == 0,              "timewarp succeeded");
    CHK(ctx.tick == tick_base, "tick restored");

    /* Snapshot hash must remain unchanged (it is the deterministic anchor) */
    Snapshot *s2 = snap_find_by_name(&tl.snapshots, "det_base");
    CHK(s2 != NULL,                     "snapshot persists after timewarp");
    CHK(s2->canvas_hash == hash_base,   "snapshot hash invariant after timewarp");

    /* CVP-based full state restoration (the canonical "timewarp to past" path):
     * Save before mutation → load after mutation → hash matches saved state. */
    EngineContext ctx2 = mk_b();
    setup_canvas(&ctx2);
    uint32_t hash2_pre = dk_canvas_hash(ctx2.cells, ctx2.cells_count);
    CHK(hash2_pre == hash_base, "independent ctx same initial hash");

    const char *tmpfile = "/tmp/canvasos_h8.cvp";
    rc = (int)cvp_save_ctx(&ctx2, tmpfile, 0, 0, CVP_CONTRACT_HASH_V1, 0);
    CHK(rc == CVP_OK, "cvp save before mutation");

    ctx2.cells[0].R = 0xFF;
    ctx2.cells[1].R = 0xEE;
    engctx_tick(&ctx2);
    uint32_t hash2_mut = dk_canvas_hash(ctx2.cells, ctx2.cells_count);
    CHK(hash2_mut != hash_base, "ctx2 mutation changed hash");

    rc = (int)cvp_load_ctx(&ctx2, tmpfile, false,
                           CVP_LOCK_SKIP, CVP_LOCK_SKIP,
                           CVP_CONTRACT_HASH_V1);
    CHK(rc == CVP_OK, "cvp load restores state");
    uint32_t hash2_restored = dk_canvas_hash(ctx2.cells, ctx2.cells_count);
    CHK(hash2_restored == hash_base, "CVP-based hash restoration");

    remove(tmpfile);
    PASS();
}

/* ── H9: Benchmark — tick throughput ──────────────────────── */
#define BENCH_TICKS 500
static void h9(void) {
    T("H9 Benchmark: " STRINGIFY(BENCH_TICKS) "-tick throughput");

    EngineContext ctx = mk_a();
    setup_canvas(&ctx);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    for (int i = 0; i < BENCH_TICKS; i++) engctx_tick(&ctx);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed_ms = (double)(t1.tv_sec  - t0.tv_sec)  * 1000.0
                      + (double)(t1.tv_nsec - t0.tv_nsec) / 1.0e6;
    double tps = (elapsed_ms > 0.0)
                 ? ((double)BENCH_TICKS / (elapsed_ms / 1000.0))
                 : 0.0;

    uint32_t final_hash = dk_canvas_hash(ctx.cells, ctx.cells_count);
    CHK(final_hash != 0u, "benchmark canvas hash not zero");

    printf("PASS  [bench: %.0f ticks/s  %.3f ms/tick  hash=0x%08X]\n",
           tps, elapsed_ms / BENCH_TICKS, final_hash);
    P++;
    return;
}

/* ── H10: Full Determinism Regression Gate ─────────────────── */
static void h10(void) {
    T("H10 Full regression: DK-1~5 integration scenario");

    /* Setup two independent contexts with identical seeds */
    EngineContext a = mk_a();
    EngineContext b = mk_b();

    /* DK-1: tick boundary guards wrap state mutations */
    TickBoundaryGuard ga = dk_begin_tick(&a, "h10_a");
    TickBoundaryGuard gb = dk_begin_tick(&b, "h10_b");

    /* DK-3: write cells via fixed index order */
    for (uint16_t y = 0; y < 4; y++) {
        for (uint16_t x = 0; x < 4; x++) {
            uint32_t idx = dk_cell_index(x, y);
            /* DK-4: clamp before storing */
            a.cells[idx].G = DK_CLAMP_U8((uint32_t)((y * 4 + x) * 17));
            b.cells[idx].G = DK_CLAMP_U8((uint32_t)((y * 4 + x) * 17));
            /* DK-5: noise absorption on R channel */
            a.cells[idx].R = DK_ABSORB_NOISE((uint8_t)(x * 10),
                                             (uint8_t)(x * 10 + 1));
            b.cells[idx].R = DK_ABSORB_NOISE((uint8_t)(x * 10),
                                             (uint8_t)(x * 10 + 1));
        }
    }

    dk_end_tick(&ga);
    dk_end_tick(&gb);
    CHK(!ga.in_commit, "a guard released");
    CHK(!gb.in_commit, "b guard released");

    /* DK-2: integer-only canvas hash must match */
    uint32_t ha = dk_canvas_hash(a.cells, a.cells_count);
    uint32_t hb = dk_canvas_hash(b.cells, b.cells_count);
    CHK(ha == hb, "DK-2 hash must match between contexts");
    CHK(dk_check_determinism(ha, hb, "h10_initial"), "dk_check_determinism");

    /* Multi-tick stability: 20 ticks, both contexts must stay in sync */
    for (int i = 0; i < 20; i++) {
        engctx_tick(&a);
        engctx_tick(&b);
    }
    uint32_t ha2 = dk_canvas_hash(a.cells, a.cells_count);
    uint32_t hb2 = dk_canvas_hash(b.cells, b.cells_count);
    CHK(ha2 == hb2, "20-tick hash must match");

    /* Snapshot + timewarp regression:
     * - snapshot.canvas_hash is captured BEFORE the WH record is written
     * - CVP save captures state AFTER the WH record (different hash)
     * - CVP restore brings back the post-snapshot state
     * - timewarp restores ctx.tick */
    Timeline tl_a; timeline_init(&tl_a, &a);
    uint32_t tick_snap = a.tick;

    /* hash BEFORE snapshot = what snapshot.canvas_hash will record */
    uint32_t hash_pre_snap = dk_canvas_hash(a.cells, a.cells_count);
    int snap_id = timeline_snapshot(&tl_a, &a, "h10_snap");
    CHK(snap_id >= 0, "snapshot created");

    /* Verify snapshot captured the pre-WH hash deterministically */
    Snapshot *sp = snap_find(&tl_a.snapshots, (uint32_t)snap_id);
    CHK(sp != NULL,                       "snapshot findable");
    CHK(sp->canvas_hash == hash_pre_snap, "snapshot records pre-WH hash");
    CHK(sp->tick        == tick_snap,     "snapshot records correct tick");

    /* CVP save AFTER snapshot (includes the new WH record) */
    uint32_t hash_post_snap = dk_canvas_hash(a.cells, a.cells_count);
    const char *tmpfile2 = "/tmp/canvasos_h10_pre.cvp";
    int rc2 = (int)cvp_save_ctx(&a, tmpfile2, 0, 0,
                                CVP_CONTRACT_HASH_V1, 0);
    CHK(rc2 == CVP_OK, "post-snapshot cvp save");

    /* Mutate and advance */
    a.cells[0].R = 0xFF;
    a.cells[1].G = 0xEE;
    engctx_tick(&a);
    uint32_t hash_post_mut = dk_canvas_hash(a.cells, a.cells_count);
    CHK(hash_post_mut != hash_post_snap, "mutation changes hash");

    /* Timewarp: restores tick */
    int tw_rc = timeline_timewarp(&tl_a, &a, tick_snap);
    CHK(tw_rc == 0,           "timewarp succeeded");
    CHK(a.tick == tick_snap,  "timewarp restores tick");

    /* Snapshot hash is invariant across timewarp */
    Snapshot *sp2 = snap_find_by_name(&tl_a.snapshots, "h10_snap");
    CHK(sp2 != NULL,                        "snapshot persists post-warp");
    CHK(sp2->canvas_hash == hash_pre_snap,  "snapshot hash invariant post-warp");

    /* CVP-based full state restore: load post-snapshot state */
    rc2 = (int)cvp_load_ctx(&a, tmpfile2, false,
                            CVP_LOCK_SKIP, CVP_LOCK_SKIP,
                            CVP_CONTRACT_HASH_V1);
    CHK(rc2 == CVP_OK, "cvp load for full restore");
    uint32_t hash_restored = dk_canvas_hash(a.cells, a.cells_count);
    CHK(hash_restored == hash_post_snap, "CVP restore recovers deterministic hash");

    remove(tmpfile2);

    /* Verify CVP round-trip preserves hash */
    const char *tmpfile = "/tmp/canvasos_h10.cvp";
    int rc = (int)cvp_save_ctx(&b, tmpfile, 0, 0,
                               CVP_CONTRACT_HASH_V1, 0);
    CHK(rc == CVP_OK, "cvp_save in regression");

    EngineContext b_loaded = mk_b();
    rc = (int)cvp_load_ctx(&b_loaded, tmpfile, false,
                           CVP_LOCK_SKIP, CVP_LOCK_SKIP,
                           CVP_CONTRACT_HASH_V1);
    CHK(rc == CVP_OK, "cvp_load in regression");

    uint32_t hb_loaded = dk_canvas_hash(b_loaded.cells, b_loaded.cells_count);
    CHK(hb_loaded == hb2, "CVP roundtrip preserves 20-tick hash");

    remove(tmpfile);
    PASS();
}

/* ── Main ──────────────────────────────────────────────────── */
int main(void) {
    printf("\n=== Patch-H: Deterministic OS Tester ===\n");
    h1(); h2(); h3(); h4(); h5();
    h6(); h7(); h8(); h9(); h10();
    printf("========================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
