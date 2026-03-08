/*
 * canvas_branch.c — Phase 5: Branch 구현 스켈레톤
 */
#include "../include/canvas_branch.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

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
            /* Phase 5 TODO: ctx의 CR2 PageSelector 업데이트 */
            (void)ctx;
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
    /* Phase 5 TODO: bt를 CR1 타일에 직렬화 (BranchCommit 구조체 배열) */
    (void)ctx; (void)bt;
    return 0;
}

int branch_table_load(EngineContext *ctx, BranchTable *bt) {
    (void)ctx; (void)bt;
    return 0;
}
