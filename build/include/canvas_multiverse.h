#pragma once
/*
 * canvas_multiverse.h — Phase 5: Multiverse Engine
 *
 * =================================================================
 * 핵심 원리
 * =================================================================
 *
 * 1024×1024 = 8MB 캔버스가 "전부"가 아닌 이유:
 *
 *   [스레드 확장]
 *   A 채널 상위 8비트 = LaneID → 256 Lane
 *   각 Lane이 CanvasFS slot을 통해 외부 데이터 참조
 *   WH/BH를 lane별로 구역 배치(Y 오프셋) → lane당 독립 타임라인
 *   → 이론상 단일 Canvas가 수십TB 병렬 처리의 "라우터"
 *
 *   [채널 깊이 = 다중 우주]
 *   plane_mask 조합 16가지 → 동일 셀을 다른 "우주"로 해석
 *   A+B = 우주 0, G+R = 우주 1, A+G = 우주 2 ...
 *   각 우주는 독립 PageSelector, 독립 Branch, 독립 WH 구역
 *
 *   [Y축 = 시간]
 *   y 좌표가 곧 tick 인덱스 역할
 *   scan을 Y 방향으로 → 타임라인 시뮬레이션
 *   WH record의 y = tick % WH_H → 이미 Y축이 시간
 *
 *   [512×512 단일 모드 용량]
 *   WH_CAP = 32768 레코드 × 채널 깊이(16) × Lane(256)
 *   = 이론상 ~134M 동시 이벤트 / snapshot
 *   CanvasFS slot 참조(외부 파일 체인)까지 포함하면
 *   단일 Canvas가 1TB+ 데이터의 인덱스/라우터로 동작 가능
 *
 * =================================================================
 * WH/BH 위치 분리 (Lane별 구역 배치)
 * =================================================================
 *
 * 기본 배치 (Phase 3/4):
 *   WH: (512,512) + 512×128
 *   BH: (512,640) + 512×64
 *
 * Phase 5 확장:
 *   Lane 0: WH@(512,512) + BH@(512,640)   ← 기본
 *   Lane 1: WH@(512,512) + BH@(512,640), Y 오프셋 +32 per lane
 *   또는 Q0/Q1 사용:
 *   Lane 0: Q2 영역 (0,512)...(511,1023)
 *   Lane 1: Q3 영역 (512,512)...(1023,1023)
 *
 * WH/BH는 "위치"가 아니라 "정책"이다.
 * 어디든 배치하고 PageSelector로 필터링하면 된다.
 *
 * =================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvas_lane.h"
#include "canvas_branch.h"

/* ---- Universe ID (plane_mask 기반) ---- */
#define UNIVERSE_MAX   16u   /* 2^4 (ABGR 조합) */
#define UNIVERSE_ID(plane_mask) ((uint8_t)((plane_mask) & 0x0Fu))

/* ---- WH/BH Zone 정의 (Lane × Universe별 독립 배치 가능) ---- */
typedef struct {
    uint16_t wh_x0, wh_y0;   /* WH 기준점 */
    uint16_t wh_w, wh_h;     /* WH 영역 크기 */
    uint16_t bh_x0, bh_y0;   /* BH 기준점 */
    uint16_t bh_w, bh_h;     /* BH 영역 크기 */
    uint32_t wh_cap;          /* 이 zone의 WH_CAP */
} WH_BH_Zone;

/* 기본 Zone (Phase 3/4 호환) */
static inline WH_BH_Zone zone_default(void) {
    return (WH_BH_Zone){
        .wh_x0=512, .wh_y0=512, .wh_w=512, .wh_h=128,
        .bh_x0=512, .bh_y0=640, .bh_w=512, .bh_h=64,
        .wh_cap=32768
    };
}

/* Q2 영역 기반 Zone (Lane 1용, 좌하단 사분면) */
static inline WH_BH_Zone zone_q2(void) {
    return (WH_BH_Zone){
        .wh_x0=0, .wh_y0=512, .wh_w=512, .wh_h=128,
        .bh_x0=0, .bh_y0=640, .bh_w=512, .bh_h=64,
        .wh_cap=32768
    };
}

/* ---- Multiverse 설정 ---- */
typedef struct {
    /* 활성 Lane 수 */
    uint8_t      lane_count;
    LaneTable    lanes;

    /* 활성 Branch 수 */
    uint32_t     branch_count;
    BranchTable  branches;

    /* Universe별 PageSelector (plane_mask 인덱스) */
    uint8_t      universe_count;
    PageSelector universe_ps[UNIVERSE_MAX];

    /* Lane별 WH/BH Zone */
    WH_BH_Zone   zones[LANE_ID_MAX];

    /* Y축 시간 정책 */
    bool         y_is_time;      /* true: y = tick 구간 */
    uint16_t     y_tick_stride;  /* y 1 step = 몇 tick */

    /* 전체 tick */
    uint64_t     global_tick;
} MultiverseEngine;

/* ---- API ---- */

/* 초기화 (기본: lane_count=1, 단일 우주, Y-time=false) */
void mve_init(MultiverseEngine *mve, EngineContext *ctx);

/* Lane 추가 및 Zone 배치
 * lane_id 에 WH/BH zone을 배정하고 LaneDesc를 등록.
 * zone=NULL 이면 zone_default() 사용.
 */
int mve_add_lane(MultiverseEngine *mve, EngineContext *ctx,
                 uint8_t lane_id, const WH_BH_Zone *zone);

/* Universe(채널 깊이) 추가
 * plane_mask = 이 우주가 보는 채널 집합 (PLANE_A|PLANE_B 등)
 * 해당 PageSelector를 CR2에 등록.
 */
int mve_add_universe(MultiverseEngine *mve, EngineContext *ctx,
                     uint8_t plane_mask);

/* Y축 시간 모드 활성화
 * y_tick_stride: y 1픽셀이 몇 tick에 해당하는지
 * 이 모드에서 scan은 y_min..y_max = from_tick..to_tick
 */
void mve_enable_y_time(MultiverseEngine *mve, uint16_t y_tick_stride);

/* 전체 Multiverse tick 진행
 * 1. 모든 active Lane을 priority 순으로 tick
 * 2. Y-time 모드면 각 Lane의 y 범위를 tick에 맞게 자동 계산
 * 3. WH/BH를 각 zone에 기록
 * 4. global_tick++
 */
int mve_tick(MultiverseEngine *mve, EngineContext *ctx);

/* 특정 Lane+Universe 조합으로 tick
 * plane_mask로 셀 필터링, lane_id로 gate 필터링
 */
int mve_tick_lu(MultiverseEngine *mve, EngineContext *ctx,
                uint8_t lane_id, uint8_t plane_mask);

/* Branch를 통한 분기 실행
 * branch_a, branch_b를 같은 tick에 다른 Y구간으로 실행
 * → "과거"와 "현재"를 동시에 처리
 */
int mve_branch_fork_tick(MultiverseEngine *mve, EngineContext *ctx,
                         uint32_t branch_a, uint32_t branch_b);

/* 용량 추정 출력 (디버그/문서용) */
void mve_print_capacity(const MultiverseEngine *mve);

/* CVP에 MultiverseEngine 메타 직렬화 (SEC type=5 예약) */
int mve_save_meta(const MultiverseEngine *mve, const char *path);
int mve_load_meta(MultiverseEngine *mve, const char *path);

/* ----------------------------------------------------------------
 * 스켈레톤 구현 (mve.c에서 완성)
 * ---------------------------------------------------------------- */

/* GPU dispatch 준비 (Phase 6):
 * 각 Lane을 compute shader workgroup으로 매핑.
 *   workgroup_x = lane_id
 *   workgroup_y = tile_y 범위
 *   workgroup_z = universe_id (plane_mask)
 *
 * GLSL uniform:
 *   uniform uint u_lane_id;
 *   uniform uint u_plane_mask;
 *   layout(rgba8ui) uniform uimage2D canvas;
 */
typedef struct {
    uint32_t dispatch_x;  /* Lane 수 */
    uint32_t dispatch_y;  /* Tile 행 수 */
    uint32_t dispatch_z;  /* Universe 수 */
} GpuDispatchHint;

GpuDispatchHint mve_gpu_dispatch(const MultiverseEngine *mve);
