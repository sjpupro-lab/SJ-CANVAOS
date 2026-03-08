#pragma once
/*
 * canvas_merge.h — Phase 5: Δ-Commit Merge (정합성 강화판)
 *
 * =================================================================
 * TODO_SPEC_PHASE5_FIXED §3 반영
 * =================================================================
 *
 * 확정 규약:
 *
 * [M-1] Merge는 반드시 틱 경계에서만 수행 (DK-1 강제)
 *
 * [M-2] 동일 tick + 동일 셀에 여러 Δ가 있을 경우:
 *        → "last Δ wins" (마지막 Δ만 유효)
 *        → before 값은 최초 1회만 기록 (after-chain 축약)
 *
 * [M-3] 충돌 정책: LOCK-family Rule 우선
 *        (예: gate CLOSE를 요청하는 Δ가 있으면 항상 승리)
 *        그 다음: plane_mask 우선순위 (낮은 bit = 우선)
 *
 * [M-4] 충돌 처리: RECORD(WH 기록) / GATE_CLOSE / IGNORE 중 1택
 *        기본: RECORD (감사 추적)
 *
 * [M-5] MergePolicy 세트 확정:
 *        MERGE_LAST_WINS    — 마지막 Δ로 덮어쓰기 (기본)
 *        MERGE_FIRST_WINS   — 최초 Δ 유지
 *        MERGE_ADDITIVE_G   — G 채널 누적 (DK-4 clamp)
 *        MERGE_MAX_G        — G 채널 최대값 취
 *        MERGE_LOCK_WINS    — gate CLOSE 우선 (보안)
 *        MERGE_CUSTOM       — RuleTable 기반 커스텀
 *
 * =================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvas_determinism.h"

/* ---- MergePolicy (확정 세트) ---- */
typedef enum {
    MERGE_LAST_WINS  = 0,   /* 마지막 Δ로 덮어쓰기 (기본) */
    MERGE_FIRST_WINS = 1,   /* 최초 Δ 유지               */
    MERGE_ADDITIVE_G = 2,   /* G 채널 누적 + DK-4 clamp  */
    MERGE_MAX_G      = 3,   /* G 채널 최대값 취           */
    MERGE_LOCK_WINS  = 4,   /* gate CLOSE 우선 (보안 우선) */
    MERGE_CUSTOM     = 5,   /* RuleTable 기반 커스텀      */
} MergePolicy;

/* ---- 충돌 처리 (conflict disposition) ---- */
typedef enum {
    CONFLICT_RECORD     = 0, /* WH에 충돌 레코드 기록 (기본, 감사) */
    CONFLICT_GATE_CLOSE = 1, /* 해당 gate 강제 CLOSE (보안)        */
    CONFLICT_IGNORE     = 2, /* 충돌 무시 (성능 우선)               */
} ConflictDisposition;

/* ---- Δ 레코드 (merge 입력 단위) ---- */
typedef struct {
    uint32_t tick;          /* 이 Δ가 발생한 tick */
    uint16_t x, y;          /* 대상 셀 좌표        */
    uint8_t  lane_id;       /* 발생 Lane           */
    uint8_t  plane_mask;    /* 발생 Universe       */

    /* 채널별 before/after (정수만, DK-2) */
    uint32_t before_A, after_A;
    uint8_t  before_B, after_B;
    uint8_t  before_G, after_G;
    uint8_t  before_R, after_R;

    uint8_t  flags;         /* DeltaFlags 참조 */
} Delta;

enum DeltaFlags {
    DF_GATE_CLOSE = (1u << 0),  /* 이 Δ가 gate CLOSE를 수반 */
    DF_SYSTEM     = (1u << 1),  /* 시스템 Lane (Lane 0) 발생 */
    DF_REPLAYED   = (1u << 2),  /* replay로 생성된 Δ        */
};

/* ---- Merge 설정 ---- */
typedef struct {
    MergePolicy        policy;
    ConflictDisposition on_conflict;
    uint8_t            priority_plane_mask;  /* 낮은 bit = 우선 */
    bool               strict_tick_boundary; /* DK-1 abort on violation */
} MergeConfig;

/* 기본 설정 */
static inline MergeConfig merge_config_default(void) {
    return (MergeConfig){
        .policy               = MERGE_LAST_WINS,
        .on_conflict          = CONFLICT_RECORD,
        .priority_plane_mask  = 0x01u,   /* A 채널 우선 */
        .strict_tick_boundary = true,
    };
}

/* ---- Merge 컨텍스트 (틱별로 생성/소멸) ---- */
#define MERGE_MAX_DELTAS  1024u

typedef struct {
    Delta    deltas[MERGE_MAX_DELTAS];
    uint32_t count;
    uint32_t current_tick;          /* [M-1] 틱 경계 고정 */
    MergeConfig cfg;
    TickBoundaryGuard guard;        /* [DK-1] 틱 경계 가드 */

    /* 통계 */
    uint32_t conflicts_detected;
    uint32_t conflicts_resolved;
    uint32_t deltas_suppressed;     /* [M-2] after-chain 축약 수 */
} MergeCtx;

/* ---- API ---- */

/*
 * merge_ctx_begin: 틱 경계 선언 + MergeCtx 초기화.
 * [M-1] 반드시 틱 시작 시점에 호출.
 */
void merge_ctx_begin(MergeCtx *mc, EngineContext *ctx, MergeConfig cfg);

/*
 * merge_add_delta: Δ를 MergeCtx에 추가.
 * [M-2] 동일 tick+셀의 기존 Δ가 있으면 before는 보존하고 after만 교체.
 */
int merge_add_delta(MergeCtx *mc, const Delta *d);

/*
 * merge_resolve_conflicts: MergeCtx 내 충돌을 정책에 따라 해소.
 * [M-3] LOCK-family 우선, 그 다음 plane_mask 우선순위.
 * [M-4] on_conflict 설정에 따라 WH 기록/gate close/무시.
 * [DK-1] 틱 경계 검증.
 */
int merge_resolve_conflicts(MergeCtx *mc, EngineContext *ctx);

/*
 * merge_apply: 해소된 Δ를 캔버스에 실제 적용.
 * [DK-3] cell_index 오름차순으로 적용.
 * [DK-4] G 채널은 clamp 통과.
 */
int merge_apply(MergeCtx *mc, EngineContext *ctx);

/*
 * merge_ctx_end: 틱 경계 완료 선언. guard 해제.
 * MergeCtx는 이후 재사용하지 않는다.
 */
void merge_ctx_end(MergeCtx *mc);

/*
 * merge_run: begin → add → resolve → apply → end 를 원샷으로 수행.
 * 간단한 케이스용.
 */
int merge_run(EngineContext *ctx, const Delta *deltas, uint32_t count,
              MergeConfig cfg);

/* ---- 내부 헬퍼 (테스트에서도 사용) ---- */

/* [M-2] 동일 셀에 Δ가 이미 있는지 찾기 */
int merge_find_existing(MergeCtx *mc, uint16_t x, uint16_t y, uint32_t tick);

/* [M-3] 충돌 판정: gate CLOSE 우선 규칙 */
static inline bool merge_is_lock_priority(const Delta *d) {
    return (d->flags & DF_GATE_CLOSE) != 0u;
}

/* [DK-3] 셀 인덱스 오름차순 비교 (qsort용) */
int merge_delta_cmp_cell_index(const void *a, const void *b);
