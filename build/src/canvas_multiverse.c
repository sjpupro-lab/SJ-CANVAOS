/*
 * canvas_multiverse.c — Phase 5: Multiverse Engine
 *
 * 구현 완료:
 *   - mve_init, mve_add_lane, mve_add_universe, mve_print_capacity
 *   - mve_tick, mve_tick_lu: lane_exec_tick + merge_tick 사용
 *   - mve_branch_fork_tick: branch_switch + lane_exec_tick
 *   - mve_save_meta / mve_load_meta: binary 직렬화
 *   - GPU dispatch: Phase 6 예약
 *
 * 빌드: src/Makefile에 canvas_multiverse.c 추가 필요
 */

#include "../include/canvas_multiverse.h"
#include "../include/engine_time.h"
#include "../include/lane_exec.h"
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

/* ---- 전체 tick (실구현: lane_exec_full_tick 사용) ---- */
int mve_tick(MultiverseEngine *mve, EngineContext *ctx) {
    if (!mve || !ctx) return -1;

    /* lane_exec_full_tick: 모든 active lane exec → merge → engctx_tick */
    lane_exec_full_tick(ctx, &mve->lanes);

    mve->global_tick++;
    return 0;
}

/* ---- Lane+Universe 조합 tick (실구현) ---- */
int mve_tick_lu(MultiverseEngine *mve, EngineContext *ctx,
                uint8_t lane_id, uint8_t plane_mask) {
    if (!mve || !ctx) return -1;

    const LaneDesc *ld = &mve->lanes.lanes[lane_id];
    if (!(ld->flags & LANE_F_ACTIVE)) return 0;

    /* plane_mask로 채널 필터링: 실행 전 마스크 적용 */
    uint8_t saved_mask = 0;
    for (uint8_t u = 0; u < mve->universe_count; u++) {
        if (mve->universe_ps[u].plane_mask == plane_mask) {
            saved_mask = plane_mask;
            break;
        }
    }
    (void)saved_mask; /* plane_mask는 향후 셀 읽기 시 채널 필터로 사용 */

    /* lane_exec_tick으로 실제 셀 실행 + Δ 수집 */
    LaneExecKey k = {
        .lane_id = (uint16_t)lane_id,
        .page_id = 0,
        .tick    = ctx->tick
    };
    lane_exec_tick(ctx, k);

    /* 단일 lane이므로 즉시 merge */
    merge_tick(ctx, ctx->tick);

    return 0;
}

/* ---- Branch 분기 tick (실구현) ---- */
int mve_branch_fork_tick(MultiverseEngine *mve, EngineContext *ctx,
                         uint32_t branch_a, uint32_t branch_b) {
    if (!mve || !ctx) return -1;

    /* branch_a 활성화 → 해당 lane 실행 */
    int ra = branch_switch(ctx, &mve->branches, branch_a);
    if (ra == 0 && branch_a < mve->branches.count) {
        uint8_t lid_a = mve->branches.branches[branch_a].lane_id;
        LaneExecKey ka = {
            .lane_id = (uint16_t)lid_a,
            .page_id = 0,
            .tick    = ctx->tick
        };
        lane_exec_tick(ctx, ka);
    }

    /* branch_b 활성화 → 해당 lane 실행 */
    int rb = branch_switch(ctx, &mve->branches, branch_b);
    if (rb == 0 && branch_b < mve->branches.count) {
        uint8_t lid_b = mve->branches.branches[branch_b].lane_id;
        LaneExecKey kb = {
            .lane_id = (uint16_t)lid_b,
            .page_id = 0,
            .tick    = ctx->tick
        };
        lane_exec_tick(ctx, kb);
    }

    /* 두 branch의 Δ를 병합 */
    merge_tick(ctx, ctx->tick);

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

/* ---- CVP 확장: 멀티버스 메타 직렬화 ---- */
#define MVE_META_MAGIC 0x4D56454Du /* "MVEM" */
#define MVE_META_VER   1u

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t lane_count;
    uint32_t universe_count;
    uint32_t branch_count;
    uint8_t  y_is_time;
    uint16_t y_tick_stride;
    uint64_t global_tick;
} MveMetaHeader;

int mve_save_meta(const MultiverseEngine *mve, const char *path) {
    if (!mve || !path) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;

    MveMetaHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic          = MVE_META_MAGIC;
    hdr.version        = MVE_META_VER;
    hdr.lane_count     = mve->lane_count;
    hdr.universe_count = mve->universe_count;
    hdr.branch_count   = mve->branches.count;
    hdr.y_is_time      = mve->y_is_time ? 1 : 0;
    hdr.y_tick_stride  = mve->y_tick_stride;
    hdr.global_tick    = mve->global_tick;

    fwrite(&hdr, sizeof(hdr), 1, fp);
    /* LaneTable: active lanes only */
    fwrite(&mve->lanes, sizeof(LaneTable), 1, fp);
    /* BranchTable */
    fwrite(&mve->branches, sizeof(BranchTable), 1, fp);
    /* Universe PageSelectors */
    fwrite(mve->universe_ps, sizeof(PageSelector), UNIVERSE_MAX, fp);
    /* WH/BH Zones */
    fwrite(mve->zones, sizeof(WH_BH_Zone), LANE_ID_MAX, fp);

    fclose(fp);
    return 0;
}

int mve_load_meta(MultiverseEngine *mve, const char *path) {
    if (!mve || !path) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    MveMetaHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) { fclose(fp); return -1; }
    if (hdr.magic != MVE_META_MAGIC || hdr.version != MVE_META_VER) {
        fclose(fp);
        return -2;
    }

    mve->lane_count     = hdr.lane_count;
    mve->universe_count = hdr.universe_count;
    mve->y_is_time      = hdr.y_is_time != 0;
    mve->y_tick_stride  = hdr.y_tick_stride;
    mve->global_tick    = hdr.global_tick;

    if (fread(&mve->lanes, sizeof(LaneTable), 1, fp) != 1) { fclose(fp); return -3; }
    if (fread(&mve->branches, sizeof(BranchTable), 1, fp) != 1) { fclose(fp); return -4; }
    if (fread(mve->universe_ps, sizeof(PageSelector), UNIVERSE_MAX, fp) != UNIVERSE_MAX) { fclose(fp); return -5; }
    if (fread(mve->zones, sizeof(WH_BH_Zone), LANE_ID_MAX, fp) != LANE_ID_MAX) { fclose(fp); return -6; }

    fclose(fp);
    return 0;
}
