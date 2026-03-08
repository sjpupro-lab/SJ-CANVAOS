/*
 * canvas_multiverse.c — Phase 5 스켈레톤 구현
 *
 * 현재 상태:
 *   - mve_init, mve_add_lane, mve_print_capacity: 구현됨
 *   - mve_tick, mve_tick_lu: 스켈레톤 (Phase 5 완성 필요)
 *   - GPU dispatch: Phase 6 예약
 *
 * 빌드: src/Makefile에 canvas_multiverse.c 추가 필요
 */

#include "../include/canvas_multiverse.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

/* ---- 초기화 ---- */
void mve_init(MultiverseEngine *mve, EngineContext *ctx) {
    memset(mve, 0, sizeof(*mve));

    lane_table_init(&mve->lanes);
    branch_table_init(&mve->branches);

    /* 기본 Lane 0 등록 (system lane, Phase 3/4 호환 zone) */
    WH_BH_Zone z0 = zone_default();
    mve->zones[0] = z0;

    LaneDesc ld0 = {
        .lane_id     = LANE_ID_SYSTEM,
        .priority    = 0,
        .gate_start  = 0,
        .gate_count  = 64,   /* 첫 64개 gate */
        .tick_born   = (uint32_t)ctx->tick,
        .flags       = LANE_F_ACTIVE,
    };
    lane_register(&mve->lanes, &ld0);
    mve->lane_count = 1;

    /* 기본 Universe 0: 전 채널 */
    mve->universe_ps[0] = (PageSelector){
        .x_min=0, .x_max=1023,
        .y_min=0, .y_max=1023,
        .quadrant_mask=0x0F,
        .plane_mask=PLANE_ALL,
        .gate_policy=0,
    };
    mve->universe_count = 1;

    mve->y_is_time = false;
    mve->y_tick_stride = 1;
    mve->global_tick = ctx->tick;

    (void)ctx;
}

/* ---- Lane 추가 ---- */
int mve_add_lane(MultiverseEngine *mve, EngineContext *ctx,
                 uint8_t lane_id, const WH_BH_Zone *zone) {
    if (mve->lane_count >= LANE_ID_MAX) return -1;

    mve->zones[lane_id] = zone ? *zone : zone_default();

    LaneDesc ld = {
        .lane_id    = lane_id,
        .priority   = lane_id,
        .gate_start = (uint16_t)(lane_id * 16u),
        .gate_count = 16,
        .tick_born  = (uint32_t)ctx->tick,
        .flags      = LANE_F_ACTIVE,
    };
    int r = lane_register(&mve->lanes, &ld);
    if (r == 0) mve->lane_count++;
    return r;
}

/* ---- Universe 추가 ---- */
int mve_add_universe(MultiverseEngine *mve, EngineContext *ctx,
                     uint8_t plane_mask) {
    if (mve->universe_count >= UNIVERSE_MAX) return -1;
    uint8_t uid = mve->universe_count;
    mve->universe_ps[uid] = (PageSelector){
        .x_min=0, .x_max=1023,
        .y_min=0, .y_max=1023,
        .quadrant_mask=0x0F,
        .plane_mask=plane_mask,
        .gate_policy=0,
    };
    mve->universe_count++;
    (void)ctx;
    return 0;
}

/* ---- Y축 시간 모드 ---- */
void mve_enable_y_time(MultiverseEngine *mve, uint16_t y_tick_stride) {
    mve->y_is_time = true;
    mve->y_tick_stride = y_tick_stride ? y_tick_stride : 1;
}

/* ---- 전체 tick (실구현) ---- */
int mve_tick(MultiverseEngine *mve, EngineContext *ctx) {
    if (!mve || !ctx) return -1;

    int total = 0;

    /* Y축 시간 모드: y_tick_stride만큼만 행 단위 진행 */
    if (mve->y_is_time) {
        /* 현재 tick에 해당하는 y 범위 계산 */
        uint32_t y_start = (mve->global_tick * mve->y_tick_stride) % CANVAS_H;
        uint32_t y_end   = y_start + mve->y_tick_stride;
        if (y_end > CANVAS_H) y_end = CANVAS_H;
        (void)y_start; (void)y_end;
        /* Y-time scanning is a policy overlay; actual execution
         * still goes through lane_tick_all */
    }

    /* Execute all active lanes */
    total = lane_tick_all(ctx, &mve->lanes);

    /* Advance global tick */
    mve->global_tick++;

    return total;
}

/* ---- Lane+Universe 조합 tick (스켈레톤) ---- */
int mve_tick_lu(MultiverseEngine *mve, EngineContext *ctx,
                uint8_t lane_id, uint8_t plane_mask) {
    /* Phase 5 TODO:
     *   1. PageSelector(plane_mask)로 canvas에서 대상 셀 필터
     *   2. 필터된 셀 중 lane_id(A 채널 상위 8비트) 매칭만 실행
     *   3. 결과를 lane의 WH zone에 기록
     */
    (void)mve; (void)plane_mask;
    return lane_tick(ctx, &mve->lanes, lane_id);
}

/* ---- Branch 분기 tick (스켈레톤) ---- */
int mve_branch_fork_tick(MultiverseEngine *mve, EngineContext *ctx,
                         uint32_t branch_a, uint32_t branch_b) {
    /* Phase 5 TODO:
     *   branch_a → y 범위 [y_min_a, y_max_a] 실행
     *   branch_b → y 범위 [y_min_b, y_max_b] 실행
     *   각각 다른 Lane에 할당하면 "과거+현재 동시 처리"
     *   결과 Δ는 각자 WH zone에 기록
     *   merge는 branch_merge()로 나중에 수동 적용
     */
    int ra = branch_switch(ctx, &mve->branches, branch_a);
    int rb = branch_switch(ctx, &mve->branches, branch_b);
    (void)mve; (void)ra; (void)rb;
    return 0;
}

/* ---- GPU dispatch 힌트 ---- */
GpuDispatchHint mve_gpu_dispatch(const MultiverseEngine *mve) {
    /* Phase 6 TODO:
     *   각 workgroup = (lane_id, tile_y, universe_id)
     *   GLSL compute shader:
     *     gl_GlobalInvocationID.x = lane_id
     *     gl_GlobalInvocationID.y = tile_row
     *     gl_GlobalInvocationID.z = universe_id
     *
     *   canvas 텍스처를 rgba8ui로 바인딩:
     *     layout(rgba8ui) uniform uimage2D u_canvas;
     *
     *   lane 필터링:
     *     uvec4 cell = imageLoad(u_canvas, ivec2(x,y));
     *     uint lane_of_cell = cell.r >> 3; // A채널 상위 8비트
     *     if (lane_of_cell != u_lane_id) return;
     */
    uint32_t lane_cnt = mve->lane_count ? mve->lane_count : 1;
    uint32_t tile_rows = 1024 / 16;  /* 64 tile rows */
    uint32_t uni_cnt = mve->universe_count ? mve->universe_count : 1;
    return (GpuDispatchHint){ lane_cnt, tile_rows, uni_cnt };
}

/* ---- 용량 추정 출력 ---- */
void mve_print_capacity(const MultiverseEngine *mve) {
    uint64_t wh_recs_per_lane = 32768ULL;
    uint64_t total_wh = wh_recs_per_lane * mve->lane_count * mve->universe_count;

    printf("=== Multiverse Capacity Estimate ===\n");
    printf("  Lanes       : %u / %u\n", mve->lane_count, LANE_ID_MAX);
    printf("  Universes   : %u / %u  (plane_mask 조합)\n",
           mve->universe_count, UNIVERSE_MAX);
    printf("  WH records  : %llu  (lane × universe × 32768)\n",
           (unsigned long long)total_wh);
    printf("  Canvas mem  : 8 MB  (1024×1024 × 8B)\n");
    printf("  CanvasFS max: ~499,200 slots → 외부 파일 참조 무제한\n");
    printf("  Y-time mode : %s  (stride=%u tick/row)\n",
           mve->y_is_time ? "ON" : "OFF", mve->y_tick_stride);
    printf("  GPU dispatch: %u × %u × %u workgroups\n",
           mve->lane_count, 64u, mve->universe_count);
    printf("=====================================\n");
}

/* ---- CVP 확장 (메타 직렬화 스켈레톤) ---- */
int mve_save_meta(const MultiverseEngine *mve, const char *path) {
    /* Phase 5 TODO:
     *   CVP TLV에 SEC type=5 (MULTIVERSE_META) 추가
     *   LaneTable + BranchTable + ZoneTable 직렬화
     *   CRC32 포함
     */
    (void)mve; (void)path;
    return -1; /* not yet implemented */
}

int mve_load_meta(MultiverseEngine *mve, const char *path) {
    (void)mve; (void)path;
    return -1; /* not yet implemented */
}
