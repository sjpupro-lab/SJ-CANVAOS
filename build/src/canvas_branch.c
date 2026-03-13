/*
 * canvas_branch.c — Phase 5: Branch 구현
 */
#include "../include/canvas_branch.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include <string.h>

void branch_table_init(BranchTable *bt) {
    memset(bt, 0, sizeof(*bt));
    bt->active_branch = BRANCH_ROOT;
}

uint32_t branch_create(BranchTable *bt, uint32_t parent_id,
                       uint8_t plane_mask,
                       uint16_t x_min, uint16_t x_max,
                       uint16_t y_min, uint16_t y_max,
                       uint8_t lane_id) {
    if (bt->count >= BRANCH_MAX) return BRANCH_NONE;

    uint32_t new_id = bt->count + 1u;
    BranchDesc *b = &bt->branches[bt->count++];
    *b = (BranchDesc){
        .branch_id    = new_id,
        .parent_id    = parent_id,
        .x_min        = x_min,  .x_max = x_max,
        .y_min        = y_min,  .y_max = y_max,
        .quadrant_mask= 0x0F,
        .plane_mask   = plane_mask,
        .gate_policy  = 0,
        .lane_id      = lane_id,
        .flags        = LANE_F_ACTIVE,
    };
    return new_id;
}

int branch_switch(EngineContext *ctx, BranchTable *bt, uint32_t branch_id) {
    /* PageSelector를 branch의 설정으로 교체 (O(1)) */
    for (uint32_t i = 0; i < bt->count; i++) {
        if (bt->branches[i].branch_id == branch_id) {
            bt->active_branch = branch_id;
            BranchDesc *b = &bt->branches[i];

            /* CR2 PageSelector 업데이트:
             * now.page_id = branch_id
             * now.scan_state encodes the plane_mask + gate_policy
             *
             * Gate filtering: branch가 force_open이면 해당 영역의 gate를 전부 OPEN,
             * force_close면 전부 CLOSE. follow면 현재 상태 유지.
             */
            if (ctx) {
                ctx->now.page_id    = (uint16_t)(branch_id & 0xFFFF);
                ctx->now.scan_state = ((uint32_t)b->plane_mask << 8)
                                    | ((uint32_t)b->gate_policy);

                /* Gate policy 적용: 해당 branch 영역의 tile들 */
                if (b->gate_policy == 1 || b->gate_policy == 2) {
                    uint16_t tx0 = (uint16_t)(b->x_min / TILE);
                    uint16_t tx1 = (uint16_t)(b->x_max / TILE);
                    uint16_t ty0 = (uint16_t)(b->y_min / TILE);
                    uint16_t ty1 = (uint16_t)(b->y_max / TILE);
                    for (uint16_t ty = ty0; ty <= ty1 && ty < TILES_Y; ty++) {
                        for (uint16_t tx = tx0; tx <= tx1 && tx < TILES_X; tx++) {
                            uint16_t gid = (uint16_t)(ty * TILES_X + tx);
                            if (b->gate_policy == 1)
                                gate_open_tile(ctx, gid);
                            else
                                gate_close_tile(ctx, gid);
                        }
                    }
                }
            }
            return 0;
        }
    }
    return -1;
}

int branch_destroy(BranchTable *bt, uint32_t branch_id) {
    for (uint32_t i = 0; i < bt->count; i++) {
        if (bt->branches[i].branch_id == branch_id) {
            /* 마지막 항목으로 swap */
            bt->branches[i] = bt->branches[--bt->count];
            return 0;
        }
    }
    return -1;
}

int branch_commit_delta(EngineContext *ctx, const DeltaCommit *d) {
    if (!ctx || !d) return -1;

    /* Record delta commit to WH for replay support */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x20; /* WH_OP_DELTA */
    wr.param0        = (uint8_t)(d->branch_id & 0xFF);
    wr.target_addr   = (uint32_t)d->x + ((uint32_t)d->y << 16);
    wr.target_kind   = WH_TGT_CELL;
    wr.flags         = 0;
    wr.param1        = d->after.R;
    wh_write_record(ctx, ctx->tick, &wr);

    /* Apply delta to canvas */
    if (d->x < CANVAS_W && d->y < CANVAS_H) {
        uint32_t idx = d->y * CANVAS_W + d->x;
        ctx->cells[idx] = d->after;
    }

    return 0;
}

int branch_merge(EngineContext *ctx, BranchTable *bt,
                 uint32_t branch_id, MergePolicy policy) {
    if (!ctx || !bt) return -1;

    /* Find the branch */
    BranchDesc *b = NULL;
    for (uint32_t i = 0; i < bt->count; i++) {
        if (bt->branches[i].branch_id == branch_id) {
            b = &bt->branches[i];
            break;
        }
    }
    if (!b) return -1;

    /* Record merge event in WH */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x22; /* WH_OP_BRANCH_MERGE */
    wr.param0        = (uint8_t)(branch_id & 0xFF);
    wr.param1        = (uint8_t)policy;
    wr.target_addr   = b->parent_id;
    wr.target_kind   = WH_TGT_CELL;
    wh_write_record(ctx, ctx->tick, &wr);

    /* Switch back to parent after merge */
    if (b->parent_id != BRANCH_ROOT)
        bt->active_branch = b->parent_id;

    (void)policy;
    return 0;
}

int branch_scan_y_range(EngineContext *ctx, const BranchDesc *b) {
    if (!ctx || !b) return -1;

    int cells_processed = 0;

    /* Scan Y range: y_min to y_max, executing cells in branch's gate space */
    for (uint32_t y = b->y_min; y <= b->y_max && y < CANVAS_H; y++) {
        for (uint32_t x = b->x_min; x <= b->x_max && x < CANVAS_W; x++) {
            uint32_t idx = y * CANVAS_W + x;
            Cell *c = &ctx->cells[idx];

            /* Filter by plane_mask */
            if (b->plane_mask != PLANE_ALL) {
                /* Only process if cell's lane matches branch lane */
                uint8_t cell_lane = (uint8_t)(c->A >> 24);
                if (cell_lane != b->lane_id) continue;
            }

            /* Skip NOP cells */
            if (c->B == 0) continue;

            cells_processed++;
        }
    }

    return cells_processed;
}

int branch_parallel_tick(EngineContext *ctx, BranchTable *bt,
                         uint32_t branch_a, uint32_t branch_b) {
    /* 순차 실행 (Phase 5): a 실행 후 b 실행
     * Phase 6에서 pthread_create 또는 OpenCL dispatch로 교체
     */
    branch_switch(ctx, bt, branch_a);
    branch_scan_y_range(ctx, &bt->branches[branch_a]);
    branch_switch(ctx, bt, branch_b);
    branch_scan_y_range(ctx, &bt->branches[branch_b]);
    return 0;
}

int branch_table_flush(EngineContext *ctx, BranchTable *bt) {
    if (!ctx || !bt) return -1;

    /* CR1 타일(Control Region)에 BranchTable을 직렬화.
     * CR1 = tile 2080 (FS_RESERVED_LO).
     * 각 BranchDesc를 4셀에 인코딩:
     *   C0.A = branch_id, C0.B = plane_mask, C0.G = gate_policy, C0.R = lane_id
     *   C1.A = parent_id
     *   C2.A = (x_min<<16)|x_max
     *   C3.A = (y_min<<16)|y_max
     */
    uint16_t cr1_tile = 2080; /* FS_RESERVED_LO */
    uint16_t tx0 = (uint16_t)((cr1_tile % TILES_X) * TILE);
    uint16_t ty0 = (uint16_t)((cr1_tile / TILES_X) * TILE);

    /* Row 0: magic + count */
    uint32_t idx0 = (uint32_t)ty0 * CANVAS_W + tx0;
    if (idx0 < ctx->cells_count) {
        ctx->cells[idx0].A = 0x42523031u; /* "BR01" magic */
        ctx->cells[idx0].B = (uint8_t)(bt->count & 0xFF);
        ctx->cells[idx0].G = (uint8_t)((bt->count >> 8) & 0xFF);
    }

    /* 각 branch: 4셀씩 row 1부터 */
    for (uint32_t i = 0; i < bt->count && i < 60; i++) {
        const BranchDesc *b = &bt->branches[i];
        uint32_t base = idx0 + (i * 4 + TILE); /* row 1 + i*4 cells */
        if (base + 3 >= ctx->cells_count) break;
        ctx->cells[base + 0].A = b->branch_id;
        ctx->cells[base + 0].B = b->plane_mask;
        ctx->cells[base + 0].G = b->gate_policy;
        ctx->cells[base + 0].R = b->lane_id;
        ctx->cells[base + 1].A = b->parent_id;
        ctx->cells[base + 2].A = ((uint32_t)b->x_min << 16) | b->x_max;
        ctx->cells[base + 3].A = ((uint32_t)b->y_min << 16) | b->y_max;
    }
    return 0;
}

int branch_table_load(EngineContext *ctx, BranchTable *bt) {
    if (!ctx || !bt) return -1;

    uint16_t cr1_tile = 2080;
    uint16_t tx0 = (uint16_t)((cr1_tile % TILES_X) * TILE);
    uint16_t ty0 = (uint16_t)((cr1_tile / TILES_X) * TILE);
    uint32_t idx0 = (uint32_t)ty0 * CANVAS_W + tx0;
    if (idx0 >= ctx->cells_count) return -1;

    /* magic 검증 */
    if (ctx->cells[idx0].A != 0x42523031u) return -1;

    uint32_t count = (uint32_t)ctx->cells[idx0].B |
                     ((uint32_t)ctx->cells[idx0].G << 8);
    if (count > BRANCH_MAX) count = BRANCH_MAX;

    branch_table_init(bt);
    for (uint32_t i = 0; i < count && i < 60; i++) {
        uint32_t base = idx0 + (i * 4 + TILE);
        if (base + 3 >= ctx->cells_count) break;
        BranchDesc *b = &bt->branches[bt->count++];
        b->branch_id   = ctx->cells[base + 0].A;
        b->plane_mask  = ctx->cells[base + 0].B;
        b->gate_policy = ctx->cells[base + 0].G;
        b->lane_id     = ctx->cells[base + 0].R;
        b->parent_id   = ctx->cells[base + 1].A;
        b->x_min       = (uint16_t)(ctx->cells[base + 2].A >> 16);
        b->x_max       = (uint16_t)(ctx->cells[base + 2].A & 0xFFFF);
        b->y_min       = (uint16_t)(ctx->cells[base + 3].A >> 16);
        b->y_max       = (uint16_t)(ctx->cells[base + 3].A & 0xFFFF);
        b->quadrant_mask = 0x0F;
        b->flags = LANE_F_ACTIVE;
    }
    return 0;
}
