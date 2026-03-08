#pragma once
/*
 * canvas_lane.h — Phase 5: Execution Lane (채널 값 기반 멀티스레드)
 *
 * =================================================================
 * 핵심 개념
 * =================================================================
 *
 * CanvasOS는 단일 .cvp 파일 위에서 여러 실행 흐름(Lane)을 동시에
 * 지원한다.  "스레드"가 아니라 "Lane"이라 부르는 이유:
 *
 *   - Context switch 없음
 *   - 공유 메모리(Canvas)를 분리하는 것이 아니라
 *     A.B 채널 값(값 공간)으로 Lane을 구분한다.
 *   - Gate 필터링이 Lane 격리를 보장한다.
 *   - GPU 워크그룹 1개 = Lane 1개 매핑 가능.
 *
 * =================================================================
 * A.B 채널 기반 Lane 구분
 * =================================================================
 *
 * Cell.A의 상위 8비트(A[31:24]) = LaneID
 * Cell.B                        = 이 Lane의 opcode
 *
 * PageSelector(CR2)의 plane_mask로 특정 lane만 필터링.
 *
 *   예) LaneID=0x01 인 Lane 전용 PageSelector:
 *       plane_mask.A_hi8 = 0x01
 *       → Cell.A[31:24] == 0x01 인 셀만 실행 대상
 *
 * 이론상:
 *   - LaneID 8비트 → 최대 256 Lane / Canvas
 *   - 각 Lane이 64K~수TB 데이터를 CanvasFS로 참조 가능
 *   - WH 레코드에 LaneID를 넣으면 lane-aware replay 가능
 *
 * =================================================================
 * GPU 연동 시 캐시 친화성
 * =================================================================
 *
 * Canvas = 1024×1024 × 8bytes = 8MB = 전형적인 L2 캐시 크기.
 * Cell 구조(A/B/G/R) = GPU float4 texel과 동일.
 *
 * GLSL/HLSL 예:
 *   layout(rgba8ui) uniform uimage2D canvas;
 *   uvec4 cell = imageLoad(canvas, ivec2(x, y));
 *   // A=cell.r|cell.g<<8|... B=cell.a G=cell.b R=cell.g (endian 주의)
 *
 * Compute shader 1 invocation = 1 cell or 1 tile.
 * Lane 분기는 uniform branch로 처리 → warp divergence 없음.
 *
 * =================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "engine_time.h"

/* ---- Lane ID (A 채널 상위 8비트) ---- */
#define LANE_ID_SHIFT   24
#define LANE_ID_MASK    0xFFu
#define LANE_ID_MAX     256

#define LANE_ID_SYSTEM  0x00u  /* 커널/시스템 Lane */
#define LANE_ID_USER0   0x01u  /* 일반 사용자 Lane 시작 */
#define LANE_ID_BCAST   0xFFu  /* 브로드캐스트 (모든 Lane) */

static inline uint8_t lane_id_of_cell(const Cell *c) {
    return (uint8_t)((c->A >> LANE_ID_SHIFT) & LANE_ID_MASK);
}

static inline uint32_t lane_set_id(uint32_t a_word, uint8_t lane_id) {
    return (a_word & 0x00FFFFFFu) | ((uint32_t)lane_id << LANE_ID_SHIFT);
}

/* ---- Lane 디스크립터 ---- */
typedef struct {
    uint8_t   lane_id;         /* 0..255 */
    uint8_t   opcode_base;     /* B 채널 opcode 범위 시작 */
    uint8_t   opcode_mask;     /* B & opcode_mask == opcode_base 면 이 Lane */
    uint8_t   priority;        /* 낮을수록 먼저 실행 */

    uint16_t  gate_start;      /* 이 Lane 전용 gate_id 시작 */
    uint16_t  gate_count;      /* 전용 gate 수 */

    uint32_t  tick_born;       /* Lane 생성 tick */
    uint32_t  flags;           /* LANE_F_* 참조 */
} LaneDesc;

enum {
    LANE_F_ACTIVE   = (1u << 0),   /* 실행 중 */
    LANE_F_SLEEPING = (1u << 1),   /* 에너지 소진 대기 */
    LANE_F_GPU      = (1u << 2),   /* GPU dispatch 대상 */
    LANE_F_REPLAY   = (1u << 3),   /* replay 전용 (쓰기 금지) */
};

/* ---- Lane Table (최대 256 Lane) ---- */
typedef struct {
    LaneDesc lanes[LANE_ID_MAX];
    uint32_t active_mask[8];  /* 256bit: lane_id에 해당 bit = active */
    uint32_t count;
} LaneTable;

/* ---- API ---- */

/* LaneTable 초기화 */
void lane_table_init(LaneTable *lt);

/* Lane 등록. lane_id가 이미 있으면 업데이트. */
int  lane_register(LaneTable *lt, const LaneDesc *desc);

/* Lane 활성화 / 비활성화 */
void lane_activate(LaneTable *lt, uint8_t lane_id);
void lane_deactivate(LaneTable *lt, uint8_t lane_id);

/* lane_id에 해당하는 셀을 Canvas에서 필터링해 실행 (single lane tick).
 * ctx의 scan policy를 그대로 사용하지만, lane_id 필터를 추가 적용.
 * returns 실행된 셀 수, -1 on error.
 */
int  lane_tick(EngineContext *ctx, LaneTable *lt, uint8_t lane_id);

/* 모든 active Lane을 priority 순으로 한 tick씩 실행 */
int  lane_tick_all(EngineContext *ctx, LaneTable *lt);

/* WH 레코드에 LaneID를 태그해서 기록 (lane-aware replay용)
 * r->flags 상위 8비트에 lane_id를 넣음 */
static inline void lane_tag_wh_record(WhRecord *r, uint8_t lane_id) {
    r->flags = (r->flags & 0x00FFu) | (uint8_t)((uint32_t)lane_id << 8u);
}

static inline uint8_t lane_id_of_wh(const WhRecord *r) {
    return (uint8_t)(r->flags >> 8u);
}

/* GPU dispatch 힌트: 이 Lane의 타일 범위를 compute shader dispatch 크기로 반환 */
void lane_gpu_dispatch_size(const LaneDesc *d,
                            uint32_t *out_x, uint32_t *out_y, uint32_t *out_z);

/* Phase 5 스켈레톤: GPU Canvas 업로드/다운로드 인터페이스 */
#ifdef CANVAS_GPU_ENABLED
int  canvas_gpu_upload(EngineContext *ctx);    /* CPU→GPU */
int  canvas_gpu_download(EngineContext *ctx);  /* GPU→CPU */
int  canvas_gpu_dispatch_lane(uint8_t lane_id, uint32_t tick);
#endif /* CANVAS_GPU_ENABLED */
