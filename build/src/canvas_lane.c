/*
 * canvas_lane.c — Phase 5: Lane 구현 (lane_tick 실제 필터링)
 *
 * 주요 변경:
 *   - lane_tick(): A[31:24]==lane_id 필터 실제 구현 (TODO_SPEC §4)
 *   - [DK-2] 정수 연산 전용
 *   - [DK-3] tile_id 오름차순 순회
 */
#include "../include/canvas_lane.h"
#include "../include/canvas_determinism.h"
#include "../include/engine_time.h"
#include <string.h>

void lane_table_init(LaneTable *lt) {
    memset(lt, 0, sizeof(*lt));
}

int lane_register(LaneTable *lt, const LaneDesc *desc) {
    uint8_t id = desc->lane_id;
    lt->lanes[id] = *desc;
    lt->active_mask[id / 32] |= (1u << (id % 32));
    if (desc->flags & LANE_F_ACTIVE) lt->count++;
    return 0;
}

void lane_activate(LaneTable *lt, uint8_t lane_id) {
    lt->lanes[lane_id].flags |= LANE_F_ACTIVE;
    lt->active_mask[lane_id / 32] |= (1u << (lane_id % 32));
}

void lane_deactivate(LaneTable *lt, uint8_t lane_id) {
    lt->lanes[lane_id].flags &= ~(uint32_t)LANE_F_ACTIVE;
    lt->active_mask[lane_id / 32] &= ~(1u << (lane_id % 32));
}

int lane_tick(EngineContext *ctx, LaneTable *lt, uint8_t lane_id) {
    const LaneDesc *ld = &lt->lanes[lane_id];
    if (!(ld->flags & LANE_F_ACTIVE)) return 0;

    int cells_executed = 0;

    /* [DK-3] tile_id 오름차순 순회 */
    for (uint32_t tile_id = 0; tile_id < TILE_COUNT; tile_id++) {
        if (ctx->gates[tile_id] != GATE_OPEN) continue;

        /* Lane gate 범위 필터 */
        if (ld->gate_count > 0) {
            uint32_t gend = (uint32_t)DK_INT(ld->gate_start)
                          + (uint32_t)DK_INT(ld->gate_count);
            if (tile_id < (uint32_t)DK_INT(ld->gate_start) ||
                tile_id >= gend) continue;
        }

        uint32_t tx = (tile_id % TILES_X) * TILE;
        uint32_t ty = (tile_id / TILES_X) * TILE;

        for (uint32_t dy = 0; dy < (uint32_t)TILE; dy++) {
            for (uint32_t dx = 0; dx < (uint32_t)TILE; dx++) {
                uint32_t cx = DK_INT(tx) + DK_INT(dx);
                uint32_t cy = DK_INT(ty) + DK_INT(dy);
                if (cx >= CANVAS_W || cy >= CANVAS_H) continue;

                const Cell *c = &ctx->cells[DK_INT(cy)*CANVAS_W + DK_INT(cx)];

                /* [DK-2] A 채널 상위 8비트 == lane_id (정수 비교) */
                uint8_t cell_lane = (uint8_t)(DK_INT(c->A) >> LANE_ID_SHIFT);
                if (cell_lane != DK_INT(lane_id)) continue;
                if (DK_INT(c->B) == 0u) continue;

                /*
                 * exec_cell stub — Phase 5에서 engine.c exec_cell() 연결:
                 *   WhRecord wr = { .tick_or_event=ctx->tick,
                 *     .opcode_index=c->B, .target_addr=dk_cell_index(cx,cy),
                 *     .target_kind=WH_TGT_CELL, .arg_state=c->G, .param0=c->R };
                 *   lane_tag_wh_record(&wr, lane_id);
                 *   wh_write_record(ctx, ctx->tick, &wr);
                 */
                cells_executed++;
            }
        }
    }
    return cells_executed;
}

int lane_tick_all(EngineContext *ctx, LaneTable *lt) {
    int total = 0;
    for (int i = 0; i < LANE_ID_MAX; i++)
        if (lt->active_mask[i/32] & (1u << (i%32)))
            total += lane_tick(ctx, lt, (uint8_t)i);
    return total;
}

void lane_gpu_dispatch_size(const LaneDesc *d,
                            uint32_t *out_x, uint32_t *out_y, uint32_t *out_z) {
    *out_x = (uint32_t)DK_INT(d->gate_count) ? (uint32_t)DK_INT(d->gate_count) : TILE_COUNT;
    *out_y = (uint32_t)(CANVAS_H / TILE);
    *out_z = 1u;
}
