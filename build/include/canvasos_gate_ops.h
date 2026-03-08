#pragma once
#include <stdint.h>
#include "canvasos_engine_ctx.h"
#include "canvasos_sched.h" /* GateSpace */

/* Gate ops (formal contract)
 *
 * - System is Always-ON.
 * - Control is by CLOSE/OPEN gating.
 * - GateState lives in ctx->gates (TILEGATE_COUNT).
 * - Optional fast-path bitmap ctx->active_open mirrors gate state (1=open).
 */

static inline int gate_valid_id(uint16_t id) { return id < TILEGATE_COUNT; }

void gate_open_tile(EngineContext *ctx, uint16_t gate_id);
void gate_close_tile(EngineContext *ctx, uint16_t gate_id);
int  gate_is_open_tile(const EngineContext *ctx, uint16_t gate_id);

void gate_open_space_ctx(EngineContext *ctx, GateSpace sp);
void gate_close_space_ctx(EngineContext *ctx, GateSpace sp);
