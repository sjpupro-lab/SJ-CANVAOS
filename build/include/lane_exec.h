#pragma once
/*
 * Lane Execution (Phase 6)
 *
 * [W-1] lane_exec_tick: (lane_id,page_id) 단위 스캔 + bpage 해석 + Δ 생성
 * [W-2] merge_tick: tick 경계에서만, lane_id 오름차순 고정
 * [DK-1] TickBoundaryGuard 강제
 */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_engine_ctx.h"
#include "canvas_lane.h"
#include "canvas_merge.h"
#include "canvas_bh_compress.h"

typedef struct {
    uint16_t lane_id;
    uint16_t page_id;
    uint64_t tick;
} LaneExecKey;

/* 단일 lane exec (Δ만 수집, overwrite 없음) */
void lane_exec_tick(EngineContext* ctx, LaneExecKey k);

/* 모든 lane Δ를 lane_id 오름차순으로 merge + BH 요약 */
void merge_tick(EngineContext* ctx, uint64_t tick);

/* 원샷: active lane 전체 exec → merge → engctx_tick */
void lane_exec_full_tick(EngineContext* ctx, LaneTable* lt);
