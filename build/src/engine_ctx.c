#include "../include/canvasos_engine_ctx.h"
#include <string.h>

void engctx_init(EngineContext *ctx, Cell *cells, uint32_t cells_count,
                 GateState *gates, uint8_t *active_open, RuleTable *rules) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->cells = cells;
    ctx->cells_count = cells_count;
    ctx->gates = gates;
    ctx->active_open = active_open;
    ctx->rules = rules;
    ctx->tick = 0;
}

#include "../include/engine_time.h"
#include "../include/canvasos_gate_ops.h"

/* ---- engctx_tick ----
 * Advance engine by one tick.
 * Phase 4: increments ctx->tick, records a WH_OP_TICK heartbeat.
 * Returns 0 on success, -1 if ctx is invalid.
 */
int engctx_tick(EngineContext *ctx) {
    if (!ctx || !ctx->cells) return -1;
    ctx->tick++;

    /* write WH heartbeat record */
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index  = WH_OP_TICK;
    r.target_kind   = WH_TGT_NONE;
    wh_write_record(ctx, (uint64_t)ctx->tick, &r);
    return 0;
}

/* ---- engctx_replay ----
 * Re-execute WH records in [from_tick, to_tick] as deterministic side-effects.
 * Gate open/close opcodes are applied; others are no-ops (interpreted by Scheduler).
 * Clamps to valid WH window automatically.
 * Returns number of records replayed, or -1 on error.
 */
int engctx_replay(EngineContext *ctx, uint32_t from_tick, uint32_t to_tick) {
    if (!ctx || !ctx->cells) return -1;

    uint32_t save  = ctx->tick;
    uint32_t win   = (save > (uint32_t)WH_CAP) ? (uint32_t)WH_CAP : save;
    uint32_t lo    = save - win;
    if (from_tick < lo)    from_tick = lo;
    if (to_tick   > save)  to_tick   = save;
    if (from_tick > to_tick) return 0;

    int count = 0;
    for (uint32_t t = from_tick; t <= to_tick; t++) {
        WhRecord r;
        wh_read_record(ctx, (uint64_t)t, &r);
        if (r.opcode_index == 0) continue; /* NOP / empty slot */
        wh_exec_record(ctx, &r);
        count++;
    }
    return count;
}

/* ---- engctx_inspect_cell ----
 * Return the Cell value at (x,y) at the given logical tick by replaying
 * from the current snapshot backward. Phase 4 minimal: reads directly
 * from the canvas (snapshot must already cover at_tick).
 * Returns 0 on success, -1 if out of bounds.
 */
int engctx_inspect_cell(const EngineContext *ctx,
                        uint16_t x, uint16_t y, uint32_t at_tick) {
    (void)at_tick;  /* Phase4: snapshot-based only; at_tick used in Phase5 sparse diff */
    if (!ctx || !ctx->cells) return -1;
    if (x >= CANVAS_W || y >= CANVAS_H) return -1;
    /* Result accessible via ctx->cells[y*CANVAS_W + x] after this call.
     * Callers read the cell directly; return value is status only. */
    return 0;
}
