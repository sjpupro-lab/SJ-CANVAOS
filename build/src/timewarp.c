/* timewarp.c — Phase-8: Time Travel Debugger */
#include "../include/canvasos_timewarp.h"
#include "../include/cvp_io.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

#define TW_BACKUP_PATH "/tmp/canvasos_tw_backup.cvp"

void timewarp_init(TimeWarp *tw) {
    memset(tw, 0, sizeof(*tw));
    strncpy(tw->backup_path, TW_BACKUP_PATH, sizeof(tw->backup_path)-1);
}

int timewarp_goto(TimeWarp *tw, EngineContext *ctx, uint32_t target_tick) {
    if (!tw || !ctx) return -1;
    CvpStatus st = cvp_save_ctx(ctx, tw->backup_path, 0, 0, CVP_CONTRACT_HASH_V1, 0);
    if (st != CVP_OK) return (int)st;
    tw->saved_tick = ctx->tick;
    tw->target_tick = target_tick;
    if (target_tick < ctx->tick) {
        st = cvp_replay_ctx(ctx, tw->backup_path, target_tick, tw->saved_tick,
                            CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
        if (st != CVP_OK) return (int)st;
    } else if (target_tick > ctx->tick) {
        for (uint32_t t = ctx->tick; t < target_tick; t++) {
            if (engctx_tick(ctx) != 0) return -1;
        }
    }
    ctx->tick = target_tick;
    tw->active = true;
    return 0;
}

int timewarp_resume(TimeWarp *tw, EngineContext *ctx) {
    if (!tw || !ctx || !tw->active) return -1;
    CvpStatus st = cvp_load_ctx(ctx, tw->backup_path, false,
                                CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
    if (st != CVP_OK) return (int)st;
    tw->active = false;
    tw->target_tick = 0;
    return 0;
}

int timewarp_step(TimeWarp *tw, EngineContext *ctx, uint32_t n_ticks) {
    if (!tw || !ctx || !tw->active) return -1;
    for (uint32_t i = 0; i < n_ticks; i++) {
        if (engctx_tick(ctx) != 0) return -1;
    }
    tw->target_tick = ctx->tick;
    return 0;
}

uint32_t timewarp_diff(EngineContext *ctx, uint32_t tick_a, uint32_t tick_b) {
    if (!ctx) return 0;
    if (tick_a > tick_b) {
        uint32_t tmp = tick_a;
        tick_a = tick_b;
        tick_b = tmp;
    }
    uint32_t count = 0;
    for (uint32_t t = tick_a; t <= tick_b; t++) {
        WhRecord r;
        memset(&r, 0, sizeof(r));
        if (wh_read_record(ctx, t, &r) && r.opcode_index != WH_OP_NOP)
            count++;
        if (t == UINT32_MAX) break;
    }
    return count;
}
