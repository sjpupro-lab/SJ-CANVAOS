#pragma once
/*
 * canvas_branch.h — Phase 5: Branch (복사 없는 다중 우주)
 *
 * =================================================================
 * 개념
 * =================================================================
 *
 * "브랜치"는 캔버스 복사(fork)가 아니다.
 * PageSelector(CR2)로 "어떤 영역을 실행할지"만 바꾸는 것이다.
 *
 * 비유:
 *   - git branch: 같은 object store, HEAD 포인터만 다름
 *   - CanvasOS branch: 같은 Canvas 메모리, PageSelector만 다름
 *
 * 이로 인해:
 *   - 브랜치 생성 = O(1) (PageSelector 1개 등록)
 *   - 브랜치 전환 = O(1) (gate_policy + plane_mask 전환)
 *   - 브랜치 간 격리 = Gate 필터링으로 보장
 *   - 브랜치 병합 = Δ-Commit(WH 레코드)로 선택적 적용
 *
 * =================================================================
 * Y축 = 시간 의미
 * =================================================================
 *
 * Canvas의 Y축을 시간축으로 사용하는 경우:
 *   - y = tick 번호 (또는 tick 구간)
 *   - 같은 x 좌표의 셀들을 Y 방향으로 쌓으면 시계열 데이터
 *   - scan을 Y 방향으로 하면 "타임라인 재생"
 *
 * 브랜치 = "다른 Y 범위를 보는 뷰"
 *   branch A: y∈[0,511]   → "과거"
 *   branch B: y∈[512,1023] → "현재"
 *
 * 병렬 실행:
 *   두 브랜치를 다른 Lane에 할당 → 동시 실행
 *   결과를 WH에 기록 후 merge
 *
 * =================================================================
 * 다중 우주(Multiverse) 모델
 * =================================================================
 *
 * 채널 깊이(A/B/G/R 각 채널)를 "우주 차원"으로 사용:
 *
 *   Universe 0: plane_mask = A only  → Cell.A만 실행 대상
 *   Universe 1: plane_mask = B only  → Cell.B만 실행 대상
 *   Universe 2: plane_mask = A+G     → A, G 채널 복합
 *
 * PageSelector.plane_mask (4비트: A/B/G/R)로 정의.
 * 이론상 2^4 = 16 채널 조합 = 16 병렬 우주.
 *
 * 각 우주는 동일 캔버스 메모리를 다른 "렌즈"로 본다.
 * 데이터 복사 없이 완전히 독립적 실행.
 *
 * =================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvas_lane.h"

/* ---- plane_mask 상수 (4비트: bit0=A bit1=B bit2=G bit3=R) ---- */
#define PLANE_A   (1u << 0)
#define PLANE_B   (1u << 1)
#define PLANE_G   (1u << 2)
#define PLANE_R   (1u << 3)
#define PLANE_ALL 0x0Fu

/* ---- BranchID ---- */
#define BRANCH_ROOT   0x00000000u
#define BRANCH_NONE   0xFFFFFFFFu
#define BRANCH_MAX    256u

/* ---- Branch 디스크립터 ---- */
typedef struct {
    uint32_t  branch_id;       /* 고유 ID */
    uint32_t  parent_id;       /* BRANCH_ROOT = 최상위 */

    /* PageSelector 설정 */
    uint16_t  x_min, x_max;   /* x 범위 */
    uint16_t  y_min, y_max;   /* y 범위 (시간축 구간) */
    uint8_t   quadrant_mask;  /* bit0=Q0 bit1=Q1 bit2=Q2 bit3=Q3 */
    uint8_t   plane_mask;     /* PLANE_A/B/G/R 조합 = "우주 차원" */

    uint8_t   gate_policy;    /* 0=follow 1=force_open 2=force_close */
    uint8_t   lane_id;        /* 이 Branch를 실행하는 Lane */

    uint32_t  tick_born;
    uint32_t  flags;
} BranchDesc;

/* ---- BranchTable (CR1에 물리 저장, here는 in-memory view) ---- */
typedef struct {
    BranchDesc branches[BRANCH_MAX];
    uint32_t   count;
    uint32_t   active_branch;  /* 현재 실행 중인 branch_id */
} BranchTable;

/* ---- Δ-Commit (branch 변경사항 기록) ---- */
/* WH에 기록되는 최소 단위. branch_id + delta(셀 좌표+값) */
typedef struct {
    uint32_t  branch_id;
    uint16_t  x, y;
    Cell      before;   /* 이전 값 (undo용) */
    Cell      after;    /* 이후 값 */
    uint32_t  tick;
} DeltaCommit;

/* ---- Merge 정책 ---- */
typedef enum {
    MERGE_OVERWRITE = 0,  /* after로 무조건 덮어쓰기 */
    MERGE_ADDITIVE  = 1,  /* G값 누적 */
    MERGE_MAX       = 2,  /* G값 최대 취 */
    MERGE_CUSTOM    = 3,  /* RuleTable 기반 커스텀 */
} MergePolicy;

/* ---- API ---- */

void branch_table_init(BranchTable *bt);

/* Branch 생성 (PageSelector + Lane 할당)
 * parent_id = BRANCH_ROOT 면 루트에서 분기.
 * Returns branch_id or BRANCH_NONE on error.
 */
uint32_t branch_create(BranchTable *bt, uint32_t parent_id,
                       uint8_t plane_mask,
                       uint16_t x_min, uint16_t x_max,
                       uint16_t y_min, uint16_t y_max,
                       uint8_t lane_id);

/* Branch 전환 (O(1): PageSelector 갱신만) */
int branch_switch(EngineContext *ctx, BranchTable *bt, uint32_t branch_id);

/* Branch 삭제 */
int branch_destroy(BranchTable *bt, uint32_t branch_id);

/* Δ-Commit을 WH에 기록 */
int branch_commit_delta(EngineContext *ctx, const DeltaCommit *d);

/* 특정 branch의 모든 Δ를 parent로 병합
 * WH의 delta 레코드를 순서대로 parent에 적용.
 */
int branch_merge(EngineContext *ctx, BranchTable *bt,
                 uint32_t branch_id, MergePolicy policy);

/* Y축 시간 범위로 스캔 (branch의 y_min..y_max만 실행) */
int branch_scan_y_range(EngineContext *ctx, const BranchDesc *b);

/* 두 Branch를 병렬로 tick (서로 다른 plane_mask → 독립 실행)
 * a_lane_id, b_lane_id 를 순차로 실행.
 * Phase 6에서 진짜 병렬화 (pthread / OpenCL).
 */
int branch_parallel_tick(EngineContext *ctx,
                         BranchTable *bt,
                         uint32_t branch_a,
                         uint32_t branch_b);

/* CanvasFS: BranchDesc를 CR1 타일에 직렬화 */
int branch_table_flush(EngineContext *ctx, BranchTable *bt);
int branch_table_load(EngineContext *ctx, BranchTable *bt);
