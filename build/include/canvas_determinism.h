#pragma once
/*
 * canvas_determinism.h — Phase 5: Determinism Kernel Contract
 *
 * =================================================================
 * 결정론 커널 규약 (Determinism Kernel Contract) — 고정 불변
 * =================================================================
 *
 * Phase 5는 Lane/Branch/BH-압축/Merge/GPU가 동시에 동작하므로
 * 결정론 붕괴 위험이 커진다. 이 헤더는 해당 위험을 **코드 수준에서
 * 강제**하는 헬퍼/매크로를 제공한다.
 *
 * 5가지 규약:
 *
 *   [DK-1] TICK BOUNDARY
 *     Δ-Commit / Merge / BH 수행은 반드시 틱(WH 프레임) 경계에서만.
 *     mid-tick 실행은 결정론 위반.
 *     → ASSERT_TICK_BOUNDARY(ctx) 로 강제.
 *
 *   [DK-2] INTEGER ONLY
 *     결정론 경계 내부: float/double 금지.
 *     GPU rgba8ui / u32 / u16 / u8 정수 연산만.
 *     → DK_INT(expr) 매크로: 정수 타입만 수식에 사용 가능하게 래핑.
 *
 *   [DK-3] FIXED REDUCTION ORDER
 *     병렬/GPU 리덕션은 cell_index 오름차순(tile_id→y→x)으로만.
 *     역순 금지, 랜덤 순서 금지.
 *     → dk_cell_index(x,y) = y*CANVAS_W + x 로 항상 계산.
 *
 *   [DK-4] NORMALIZE
 *     G 채널 에너지, B 채널 opcode 결과는 clamp/round를 거쳐야 함.
 *     → DK_CLAMP_U8(v), DK_CLAMP_U16(v) 사용.
 *
 *   [DK-5] NOISE FLOOR
 *     ±1 흡수 규칙: 동일 셀에 두 채널이 ±1 차이면 결정론적으로 처리.
 *     → DK_ABSORB_NOISE(a, b) = ((uint32_t)(a)+(uint32_t)(b)+1u)/2u
 *
 * =================================================================
 * 위반 감지
 * =================================================================
 *
 * 디버그 빌드(-DCANVAS_DK_STRICT):
 *   ASSERT_TICK_BOUNDARY → abort() + stderr 출력
 *   float 사용 시 컴파일 경고 (DK_INT 강제)
 *
 * 릴리즈 빌드:
 *   ASSERT_TICK_BOUNDARY → no-op (성능)
 *   그러나 DK_INT / DK_CLAMP_U8 는 항상 동작
 *
 * =================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"

/* ---- [DK-1] Tick Boundary ---- */

/* tick_committed: 이번 틱에서 이미 Δ-Commit/Merge/BH가 시작됐으면 true.
 * TickBoundaryGuard를 스택에 올려서 mid-tick 재진입을 막는다.       */
typedef struct {
    uint32_t  start_tick;       /* guard 생성 시 tick */
    bool      in_commit;        /* true = 틱 경계 커밋 중 */
    const char *caller;         /* 진단용 */
} TickBoundaryGuard;

/* guard 초기화 (틱 경계 시작 선언) */
static inline TickBoundaryGuard dk_begin_tick(EngineContext *ctx,
                                               const char *caller) {
    return (TickBoundaryGuard){
        .start_tick = ctx->tick,
        .in_commit  = true,
        .caller     = caller,
    };
}

/* guard 해제 (틱 경계 완료) */
static inline void dk_end_tick(TickBoundaryGuard *g) {
    g->in_commit = false;
}

#ifdef CANVAS_DK_STRICT
#include <stdlib.h>
/* mid-tick 진입 시 abort */
static inline void _dk_assert_boundary(EngineContext *ctx,
                                        uint32_t expected_tick,
                                        const char *caller) {
    if (ctx->tick != expected_tick) {
        fprintf(stderr,
            "[DK-1 VIOLATION] %s: tick drifted during commit "
            "(expected=%u got=%u)\n",
            caller, expected_tick, ctx->tick);
        abort();
    }
}
#define ASSERT_TICK_BOUNDARY(ctx, guard) \
    _dk_assert_boundary((ctx), (guard).start_tick, (guard).caller)
#else
#define ASSERT_TICK_BOUNDARY(ctx, guard) ((void)0)
#endif

/* ---- [DK-2] Integer Only ---- */

/* DK_INT: 컴파일타임에 정수 타입인지 확인.
 * C11 _Generic 사용. float/double 이면 컴파일 에러.               */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define DK_INT(x) (_Generic((x), \
    uint8_t:  (x), \
    uint16_t: (x), \
    uint32_t: (x), \
    uint64_t: (x), \
    int8_t:   (x), \
    int16_t:  (x), \
    int32_t:  (x), \
    int64_t:  (x)))
/* float/double은 매핑 없음 → 컴파일 에러: "no match for _Generic" */
#else
#define DK_INT(x) (x)   /* C99 fallback: 무검사 */
#endif

/* ---- [DK-3] Fixed Reduction Order ---- */

/* 항상 y*W+x 순서. 역산도 동일 공식. */
static inline uint32_t dk_cell_index(uint16_t x, uint16_t y) {
    return (uint32_t)DK_INT(y) * (uint32_t)CANVAS_W + (uint32_t)DK_INT(x);
}

/* tile_id → cell_index (tile 내 첫 셀) */
static inline uint32_t dk_tile_cell_base(uint32_t tile_id) {
    uint32_t tx = (tile_id % TILES_X) * TILE;
    uint32_t ty = (tile_id / TILES_X) * TILE;
    return dk_cell_index((uint16_t)tx, (uint16_t)ty);
}

/* ---- [DK-4] Normalize (clamp) ---- */

static inline uint8_t  DK_CLAMP_U8 (uint32_t v) { return (uint8_t)(v > 255u ? 255u : v); }
static inline uint16_t DK_CLAMP_U16(uint32_t v) { return (uint16_t)(v > 65535u ? 65535u : v); }
static inline uint32_t DK_CLAMP_U32(uint64_t v) { return (uint32_t)(v > 0xFFFFFFFFULL ? 0xFFFFFFFFU : v); }

/* ---- [DK-5] Noise Floor (±1 흡수 규칙) ---- */

/* 두 값의 결정론적 평균 (정수, 올림 없음) */
static inline uint8_t DK_ABSORB_NOISE(uint8_t a, uint8_t b) {
    return (uint8_t)(((uint32_t)DK_INT(a) + (uint32_t)DK_INT(b) + 1u) >> 1u);
}

/* ---- 결정론 검증: 캔버스 해시 ---- */
/* 동일 Canvas 상태 → 동일 해시. replay 전후 비교에 사용.            */

/* FNV-1a 32bit (결정론적, 순서 의존) */
static inline uint32_t dk_canvas_hash(const Cell *cells, uint32_t count) {
    uint32_t h = 2166136261u;
    const uint8_t *p = (const uint8_t *)cells;
    uint32_t bytes = count * (uint32_t)sizeof(Cell);
    for (uint32_t i = 0; i < bytes; i++) {
        h ^= (uint32_t)p[i];
        h *= 16777619u;
    }
    return h;
}

/* 두 해시가 다르면 결정론 위반 로그 */
static inline bool dk_check_determinism(uint32_t hash_a, uint32_t hash_b,
                                         const char *label) {
    if (hash_a != hash_b) {
#ifdef CANVAS_DK_STRICT
        fprintf(stderr,
            "[DK VIOLATION] %s: canvas hash mismatch "
            "(0x%08x != 0x%08x)\n", label, hash_a, hash_b);
#endif
        return false;
    }
    return true;
}

/* ---- 요약 ---- */
/*
 * 사용 예:
 *
 *   // 틱 경계 커밋 시작
 *   TickBoundaryGuard g = dk_begin_tick(ctx, __func__);
 *
 *   // 중간에 tick이 바뀌면 abort (STRICT 모드)
 *   ASSERT_TICK_BOUNDARY(ctx, g);
 *
 *   // 정수 연산 보장
 *   uint8_t energy = DK_CLAMP_U8(DK_INT(old_energy) - DK_INT(decay));
 *
 *   // 노이즈 흡수
 *   uint8_t merged = DK_ABSORB_NOISE(lane_a_val, lane_b_val);
 *
 *   // 검증
 *   uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
 *   dk_check_determinism(h, expected_hash, "after_merge");
 *
 *   dk_end_tick(&g);
 */
