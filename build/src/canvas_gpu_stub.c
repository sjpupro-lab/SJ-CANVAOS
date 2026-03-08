/*
 * canvas_gpu_stub.c — GPU CPU fallback (Phase 6)
 *
 * SJ_GPU=0 일 때 모든 GPU 함수는 CPU 경로로 실행.
 * 동일 입력 → 동일 결과 보장 (결정론 테스트 통과 기준).
 */
#include "../include/canvas_gpu.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_lane.h"
#include "../include/canvas_bh_compress.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ── 내부 GpuCtx 구조체 (stub) ── */
struct GpuCtx {
    GpuCaps caps;
};

/* ══════════════════════════════════════════ */
GpuCtx *gpu_init(void) {
    GpuCtx *g = calloc(1, sizeof(GpuCtx));
    if (!g) return NULL;
    g->caps.available     = false;   /* stub: GPU 없음 */
    g->caps.integer_texel = true;    /* CPU도 정수 처리 */
    g->caps.max_tiles     = TILE_COUNT;
    strncpy(g->caps.backend, "stub", sizeof(g->caps.backend)-1);
    return g;
}

void gpu_destroy(GpuCtx *g) { free(g); }

GpuCaps gpu_get_caps(const GpuCtx *g) {
    return g ? g->caps : (GpuCaps){0};
}

/* ══════════════════════════════════════════
   gpu_upload_tiles — stub: CPU에 이미 있음, no-op
══════════════════════════════════════════ */
int gpu_upload_tiles(GpuCtx *g, const EngineContext *ctx,
                     const uint16_t *open_tiles, uint32_t n) {
    (void)g; (void)ctx; (void)open_tiles; (void)n;
    return 0;
}

/* ══════════════════════════════════════════
   gpu_scan_active_set — CPU 경로: [DK-3] tile_id 오름차순
══════════════════════════════════════════ */
int gpu_scan_active_set(GpuCtx *g, EngineContext *ctx,
                        const uint16_t *open_tiles, uint32_t n,
                        Delta *delta_out, uint32_t *delta_count) {
    (void)g;
    if (!ctx || !open_tiles || !delta_out || !delta_count) return -1;

    *delta_count = 0;
    uint32_t max_d = MERGE_MAX_DELTAS;

    /* [DK-3] tile_id 오름차순으로 처리 */
    for (uint32_t ti = 0; ti < n && *delta_count < max_d; ti++) {
        uint16_t gid = open_tiles[ti];
        if (!gate_is_open_tile(ctx, gid)) continue;

        uint32_t tx = (uint32_t)(gid % TILES_X) * TILE;
        uint32_t ty = (uint32_t)(gid / TILES_X) * TILE;

        for (uint32_t dy = 0; dy < (uint32_t)TILE && *delta_count < max_d; dy++) {
            for (uint32_t dx = 0; dx < (uint32_t)TILE && *delta_count < max_d; dx++) {
                uint32_t cx = DK_INT(tx) + DK_INT(dx);
                uint32_t cy = DK_INT(ty) + DK_INT(dy);
                if (cx >= CANVAS_W || cy >= CANVAS_H) continue;

                const Cell *c = &ctx->cells[DK_INT(cy)*CANVAS_W + DK_INT(cx)];
                if (!c->B && !c->G) continue; /* 빈 셀 스킵 */

                /* [DK-2] 정수 연산: G 감쇠 1 */
                if (c->G > 0) {
                    Delta *d = &delta_out[(*delta_count)++];
                    d->tick     = ctx->tick;
                    d->x        = (uint16_t)cx;
                    d->y        = (uint16_t)cy;
                    d->lane_id  = (uint8_t)(DK_INT(c->A) >> LANE_ID_SHIFT);
                    d->before_G = c->G;
                    d->after_G  = DK_CLAMP_U8((uint32_t)DK_INT(c->G) - 1u);
                    d->before_B = c->B;
                    d->after_B  = c->B;
                    d->before_R = c->R;
                    d->after_R  = c->R;
                    d->before_A = c->A;
                    d->after_A  = c->A;
                    d->flags    = 0;
                }
            }
        }
    }
    return 0;
}

/* ══════════════════════════════════════════
   gpu_bh_summarize_idle — CPU fallback
══════════════════════════════════════════ */
int gpu_bh_summarize_idle(GpuCtx *g, EngineContext *ctx,
                           uint32_t from_tick, uint32_t to_tick,
                           uint16_t gate_id) {
    (void)g;
    /* BH-IDLE 감지: WH 구간에서 해당 gate_id 이벤트 없으면 요약 */
    uint32_t idle_count = 0;
    for (uint32_t t = from_tick; t < to_tick; t++) {
        WhRecord r;
        if (!wh_read_record(ctx, (uint64_t)t, &r)) continue;
        if ((uint16_t)(r.target_addr & 0xFFFFu) == gate_id &&
            r.opcode_index != 0 && r.opcode_index != WH_OP_NOP &&
            r.opcode_index != WH_OP_TICK) {
            idle_count = 0; /* 이벤트 있음 */
        } else {
            idle_count++;
        }
    }

    if (idle_count < BH_IDLE_MIN_TICKS) return 0;

    /* BH_SUMMARY 기록 */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = DK_INT(from_tick);
    wr.opcode_index  = WH_OP_BH_SUMMARY;
    wr.flags         = (uint8_t)BH_RULE_IDLE;
    wr.param0        = (uint8_t)(DK_INT(gate_id) & 0xFFu);
    wr.target_addr   = DK_INT(to_tick);
    wr.target_kind   = (uint8_t)DK_INT(idle_count & 0xFFu);
    wh_write_record(ctx, (uint64_t)ctx->tick, &wr);
    return 1;
}

/* ══════════════════════════════════════════
   gpu_merge_delta_tiles — CPU 경로 (merge_run 위임)
══════════════════════════════════════════ */
int gpu_merge_delta_tiles(GpuCtx *g, EngineContext *ctx,
                           const Delta *deltas, uint32_t count) {
    (void)g;
    if (!deltas || count == 0) return 0;
    MergeConfig cfg = merge_config_default();
    return merge_run(ctx, deltas, count, cfg);
}
