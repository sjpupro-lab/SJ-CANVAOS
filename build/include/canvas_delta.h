#pragma once
/*
 * canvas_delta.h — Phase 6: Lane Δ-Buffer 시스템
 *
 * ===================================================================
 * 핵심 설계 원칙
 * ===================================================================
 *
 * [D-1] Lane은 Canvas를 직접 overwrite 금지.
 *       모든 쓰기는 LaneDelta 버퍼에만 기록.
 *
 * [D-2] Merge는 tick boundary에서만 (TickBoundaryGuard 필수).
 *       순서: LaneID 오름차순 → addr(y*W+x) 오름차순.
 *
 * [D-3] DeltaCell.value = packed ABGR (uint32 x 2 = 8바이트):
 *         value_lo = A (32비트)
 *         value_hi = B | (G<<8) | (R<<16) | (pad<<24)
 *       Cell 구조체와 동일 레이아웃이므로 memcpy 가능.
 *
 * [D-4] MergePolicy는 충돌(동일 addr, 다른 lane) 해결 규칙.
 *       기본: LAST_WINS (lane_id 오름차순 → 마지막이 최종)
 *
 * ===================================================================
 * GPU 매핑 (Phase 7+)
 * ===================================================================
 *
 * LaneDelta.cells 배열은 GPU SSBO로 직접 업로드 가능:
 *   layout(std430) buffer DeltaBuf { DeltaCell cells[]; };
 *   cells[i].addr  → imageStore coord
 *   cells[i].value_lo → A channel
 *   cells[i].value_hi → B/G/R channels (rgba8ui)
 *
 * GPU merge shader: 동일 addr 중 lane_id max 값 우선 적용.
 * (CPU와 동일 결과 보장: addr 정렬 + last-wins)
 * ===================================================================
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "canvasos_types.h"

/* ── DeltaCell: 최소 단위 변경 레코드 ── */
typedef struct {
    uint32_t addr;       /* linear index: y * CANVAS_W + x          */
    uint32_t value_lo;   /* Cell.A (32비트 전체)                     */
    uint32_t value_hi;   /* Cell.B | (G<<8) | (R<<16) | (lane<<24)  */
                         /* lane_id를 hi[31:24]에 넣어 GPU 정렬 지원 */
} DeltaCell;

/* DeltaCell ↔ Cell 변환 */
static inline DeltaCell delta_cell_from(uint32_t addr, const Cell *c,
                                         uint8_t lane_id) {
    return (DeltaCell){
        .addr     = addr,
        .value_lo = c->A,
        .value_hi = (uint32_t)c->B
                  | ((uint32_t)c->G << 8)
                  | ((uint32_t)c->R << 16)
                  | ((uint32_t)lane_id << 24),
    };
}
static inline void delta_cell_apply(const DeltaCell *dc, Cell *c) {
    c->A   = dc->value_lo;
    c->B   = (uint8_t)(dc->value_hi & 0xFFu);
    c->G   = (uint8_t)((dc->value_hi >> 8)  & 0xFFu);
    c->R   = (uint8_t)((dc->value_hi >> 16) & 0xFFu);
    c->pad = 0;
}
static inline uint8_t delta_cell_lane(const DeltaCell *dc) {
    return (uint8_t)((dc->value_hi >> 24) & 0xFFu);
}

/* ── LaneDelta: Lane 당 Δ 버퍼 ── */
#define LANE_DELTA_INIT_CAP 64u

typedef struct {
    DeltaCell *cells;
    uint32_t   count;
    uint32_t   cap;
    uint8_t    lane_id;
    uint32_t   tick;     /* 이 버퍼가 속한 tick (검증용) */
} LaneDelta;

/* 버퍼 예약 (realloc 기반, 2배 성장) */
static inline int lane_delta_reserve(LaneDelta *d, uint32_t need) {
    if (d->count + need <= d->cap) return 0;
    uint32_t nc = d->cap ? d->cap : LANE_DELTA_INIT_CAP;
    while (nc < d->count + need) nc *= 2;
    DeltaCell *p = (DeltaCell *)realloc(d->cells, nc * sizeof(DeltaCell));
    if (!p) return -1;
    d->cells = p;
    d->cap   = nc;
    return 0;
}

/* 셀 추가 */
static inline int lane_delta_push(LaneDelta *d, uint32_t addr,
                                   const Cell *c, uint8_t lane_id) {
    if (lane_delta_reserve(d, 1) != 0) return -1;
    d->cells[d->count++] = delta_cell_from(addr, c, lane_id);
    return 0;
}

/* 버퍼 초기화 (메모리 유지, count만 0) */
static inline void lane_delta_clear(LaneDelta *d) {
    d->count = 0;
    d->tick  = 0;
}

/* 버퍼 해제 */
static inline void lane_delta_free(LaneDelta *d) {
    free(d->cells);
    d->cells = NULL;
    d->count = d->cap = 0;
}

/* ── MergePolicy (충돌 해결 규칙) ── */
typedef enum {
    DELTA_MERGE_LAST_WINS    = 0,  /* lane_id 오름차순 → 마지막 덮어쓰기 (기본) */
    DELTA_MERGE_FIRST_WINS   = 1,  /* 첫 번째 lane이 쓴 값 유지 */
    DELTA_MERGE_PRIORITY     = 2,  /* LaneDesc.priority 낮은 lane 우선 */
    DELTA_MERGE_ENERGY_MAX   = 3,  /* 에너지(G채널) 가장 큰 값 채택 */
    DELTA_MERGE_ENERGY_MIN   = 4,  /* 에너지 가장 작은 값 채택 */
    DELTA_MERGE_OR_BITS      = 5,  /* B/G/R OR 연산 (누적 플래그용) */
} DeltaMergePolicy;

/* ── GPU export 메타데이터 ── */
typedef struct {
    uint32_t lane_id;
    uint32_t offset;   /* DeltaCell 배열 내 시작 인덱스 */
    uint32_t count;    /* 이 lane의 Δ 개수 */
    uint32_t tick;
} GpuDeltaLaneInfo;

/* GPU export: 전체 Δ를 연속 배열로 flatten (SSBO 업로드용) */
typedef struct {
    DeltaCell      *flat;        /* malloc'd 연속 배열 */
    uint32_t        total;       /* 전체 DeltaCell 수 */
    GpuDeltaLaneInfo lanes[256]; /* lane별 오프셋 정보 */
    uint32_t        lane_count;  /* 유효 lane 수 */
} GpuDeltaExport;

/* API */
int  gpu_delta_export_build(const LaneDelta *ld_array, int n_lanes,
                             GpuDeltaExport *out);
void gpu_delta_export_free(GpuDeltaExport *ex);
