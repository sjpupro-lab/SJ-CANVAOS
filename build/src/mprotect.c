/* mprotect.c — Phase-8: Tile Memory Protection */
#include "../include/canvasos_mprotect.h"
#include "../include/engine_time.h"
#include "../include/canvasos_gate_ops.h"
#include <string.h>

static void mprotect_wh(EngineContext *ctx, uint8_t opcode,
                        uint16_t pid, uint16_t tile_id, uint8_t arg) {
    if (!ctx) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index = opcode;
    r.param0 = arg;
    r.target_addr = tile_id;
    r.target_kind = WH_TGT_TILE;
    r.arg_state = (uint8_t)(pid & 0xFFu);
    wh_write_record(ctx, ctx->tick, &r);
}

void tprot_init(TileProtection *tp) {
    memset(tp->owner, 0xFF, sizeof(tp->owner));
    memset(tp->perm,  0,    sizeof(tp->perm));
}

int tile_alloc(TileProtection *tp, EngineContext *ctx,
               uint16_t pid, uint16_t count) {
    if (!tp || count == 0 || count > TILE_COUNT) return -1;
    for (uint16_t start = 0; start <= (uint16_t)(TILE_COUNT - count); start++) {
        uint16_t i;
        for (i = 0; i < count; i++) {
            if (tp->owner[start + i] != TILE_OWNER_FREE) break;
        }
        if (i != count) {
            start = (uint16_t)(start + i);
            continue;
        }
        for (i = 0; i < count; i++) {
            tp->owner[start + i] = pid;
            tp->perm[start + i] = PERM_RWX;
            gate_open_tile(ctx, (uint16_t)(start + i));
        }
        mprotect_wh(ctx, WH_OP_MPROTECT, pid, start, (uint8_t)(count & 0xFFu));
        return start;
    }
    return -1;
}

void tile_free(TileProtection *tp, EngineContext *ctx,
               uint16_t pid, uint16_t start, uint16_t count) {
    if (!tp || start >= TILE_COUNT) return;
    uint16_t end = (uint16_t)(start + count);
    if (end > TILE_COUNT) end = TILE_COUNT;
    for (uint16_t i = start; i < end; i++) {
        if (tp->owner[i] == pid) {
            tp->owner[i] = TILE_OWNER_FREE;
            tp->perm[i] = PERM_NONE;
            gate_close_tile(ctx, i);
        }
    }
    mprotect_wh(ctx, WH_OP_MPROTECT, pid, start, 0);
}

int tile_check(const TileProtection *tp, uint16_t pid,
               uint16_t tile_id, uint8_t perm) {
    if (!tp || tile_id >= TILE_COUNT) return -1;
    uint16_t owner = tp->owner[tile_id];
    uint8_t have = tp->perm[tile_id];
    if (pid == 0) return 0;
    if (owner == pid && ((have & perm) == perm)) return 0;
    if ((have & PERM_SHARED) && ((have & perm) == perm)) return 0;
    return -1;
}

void tile_set_perm(TileProtection *tp, EngineContext *ctx,
                   uint16_t pid, uint16_t tile_id, uint8_t perm) {
    if (!tp || tile_id >= TILE_COUNT) return;
    if (tp->owner[tile_id] == pid) {
        tp->perm[tile_id] = perm;
        mprotect_wh(ctx, WH_OP_MPROTECT, pid, tile_id, perm);
    }
}
