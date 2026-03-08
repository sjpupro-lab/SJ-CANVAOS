#pragma once
/*
 * EngineContext — refactor-ready interface (Phase 3+)
 *
 * Core responsibilities:
 *   Scan, Gate filtering, Interpretation, Commit, Replay
 * Policies:
 *   scan mode, page selector, bpage selection
 */
#include <stdint.h>
#include "canvasos_types.h"
#include "canvasos_ruletable.h"

typedef struct {
    uint16_t page_id;
    uint16_t gate_snapshot_id;
    BpageChain bpage_chain;
    uint32_t scan_state;   /* policy-defined cursor/ring index */
} TimeSlice;

typedef struct {
    /* Always-ON canvas memory */
    Cell *cells;               /* flat pointer (CANVAS_W*CANVAS_H) */
    uint32_t cells_count;

    /* Gate + active set */
    GateState *gates;          /* TILEGATE_COUNT */
    uint8_t   *active_open;    /* TILE_COUNT bitmap (optional fast path) */

    /* RuleTable */
    RuleTable *rules;

    /* TimeMachine */
    uint32_t tick;
    uint16_t timelane_volh;    /* optional: TimeLane as a volume header */
    uint16_t timelane_volt;

    /* Current world */
    TimeSlice now;
} EngineContext;

/* lifecycle */
void engctx_init(EngineContext *ctx, Cell *cells, uint32_t cells_count,
                 GateState *gates, uint8_t *active_open, RuleTable *rules);

/* tick */
int  engctx_tick(EngineContext *ctx);

/* time travel */
int  engctx_replay(EngineContext *ctx, uint32_t from_tick, uint32_t to_tick);
int  engctx_inspect_cell(const EngineContext *ctx, uint16_t x, uint16_t y, uint32_t at_tick);

