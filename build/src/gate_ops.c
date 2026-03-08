#include "../include/canvasos_gate_ops.h"

void gate_open_tile(EngineContext *ctx, uint16_t gate_id) {
    if (!ctx || !gate_valid_id(gate_id)) return;
    if (ctx->gates) ctx->gates[gate_id] = GATE_OPEN;
    if (ctx->active_open) ctx->active_open[gate_id] = 1;
}

void gate_close_tile(EngineContext *ctx, uint16_t gate_id) {
    if (!ctx || !gate_valid_id(gate_id)) return;
    if (ctx->gates) ctx->gates[gate_id] = GATE_CLOSE;
    if (ctx->active_open) ctx->active_open[gate_id] = 0;
}

int gate_is_open_tile(const EngineContext *ctx, uint16_t gate_id) {
    if (!ctx || !gate_valid_id(gate_id)) return 0;
    if (ctx->active_open) return ctx->active_open[gate_id] ? 1 : 0;
    if (ctx->gates) return ctx->gates[gate_id] == GATE_OPEN;
    return 0;
}

void gate_open_space_ctx(EngineContext *ctx, GateSpace sp) {
    gate_open_tile(ctx, sp.volh);
    gate_open_tile(ctx, sp.volt);
}

void gate_close_space_ctx(EngineContext *ctx, GateSpace sp) {
    gate_close_tile(ctx, sp.volh);
    gate_close_tile(ctx, sp.volt);
}
