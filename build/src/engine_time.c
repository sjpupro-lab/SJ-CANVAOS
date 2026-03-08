#include "../include/engine_time.h"
#include "canvasos_opcodes.h"
#include "canvasos_gate_ops.h"

/* ===== internal helpers ===== */
static inline uint32_t pidx(uint16_t x, uint16_t y) {
    return (uint32_t)y * (uint32_t)CANVAS_W + (uint32_t)x;
}

WhAddr wh_addr_of_tick(uint64_t tick) {
    uint32_t idx = (uint32_t)(tick % (uint64_t)WH_CAP);
    uint16_t row = (uint16_t)(idx / WH_RECS_PER_ROW);
    uint16_t col = (uint16_t)(idx % WH_RECS_PER_ROW);

    WhAddr a;
    a.x = (uint16_t)(WH_X0 + col * WH_RECORD_CELLS);
    a.y = (uint16_t)(WH_Y0 + row);
    return a;
}

Cell* wh_cell0(EngineContext* ctx, WhAddr a) {
    return &ctx->cells[pidx(a.x, a.y)];
}

Cell* wh_cell1(EngineContext* ctx, WhAddr a) {
    return &ctx->cells[pidx((uint16_t)(a.x + 1), a.y)];
}

void wh_write_record(EngineContext* ctx, uint64_t tick, const WhRecord* r) {
    WhAddr a = wh_addr_of_tick(tick);
    Cell* c0 = wh_cell0(ctx, a);
    Cell* c1 = wh_cell1(ctx, a);

    /* C0 */
    c0->A = r->tick_or_event;
    c0->B = r->opcode_index;
    c0->G = r->flags;
    c0->R = r->param0;

    /* C1 */
    c1->A = r->target_addr;
    c1->B = r->target_kind;
    c1->G = r->arg_state;
    c1->R = r->param1;
}

bool wh_read_record(EngineContext* ctx, uint64_t tick, WhRecord* out) {
    if (!out) return false;
    WhAddr a = wh_addr_of_tick(tick);
    Cell* c0 = wh_cell0(ctx, a);
    Cell* c1 = wh_cell1(ctx, a);

    out->tick_or_event = c0->A;
    out->opcode_index  = c0->B;
    out->flags         = c0->G;
    out->param0        = c0->R;

    out->target_addr   = c1->A;
    out->target_kind   = c1->B;
    out->arg_state     = c1->G;
    out->param1        = c1->R;

    return true;
}

/* ===== BH energy mapping =====
 * BH is 512x64. One pid uses one cell. capacity = 32768 pids.
 * Policy:
 *   - energy stored in G (state layer)
 *   - energy_max stored in R (stream byte, 0..255)
 *   - B used as marker/type
 *   - A reserved for future links/space IDs
 */
BhAddr bh_addr_of_pid(uint16_t pid) {
    uint32_t idx = (uint32_t)pid;
    uint32_t cap = (uint32_t)(BH_W * BH_H);
    idx %= cap;

    BhAddr a;
    a.x = (uint16_t)(BH_X0 + (idx % BH_W));
    a.y = (uint16_t)(BH_Y0 + (idx / BH_W));
    return a;
}

static inline Cell* bh_cell(EngineContext* ctx, uint16_t pid) {
    BhAddr a = bh_addr_of_pid(pid);
    return &ctx->cells[pidx(a.x, a.y)];
}

uint8_t bh_get_energy(EngineContext* ctx, uint16_t pid) {
    return bh_cell(ctx, pid)->G;
}

void bh_set_energy(EngineContext* ctx, uint16_t pid, uint8_t energy, uint8_t energy_max) {
    Cell* c = bh_cell(ctx, pid);
    c->B = 1;          /* BH energy marker */
    c->G = energy;
    c->R = energy_max;
}

uint8_t bh_decay_energy(EngineContext* ctx, uint16_t pid, uint8_t dec) {
    Cell* c = bh_cell(ctx, pid);
    uint8_t e = c->G;
    if (e == 0) return 0;
    if (dec >= e) e = 0;
    else e = (uint8_t)(e - dec);
    c->G = e;
    return e;
}


/* Minimal WH opcode execution.
 * - Keeps core deterministic: opcode decides side effects.
 * - Gate ops route through EngineContext gate array + optional bitmap.
 * - For now, only tile gate open/close is executed; others are no-ops.
 */
int wh_exec_record(EngineContext* ctx, const WhRecord* r) {
    if (!ctx || !r) return -1;
    switch ((WhOpcode)r->opcode_index) {
        case WH_OP_GATE_OPEN:
            if (r->target_kind == WH_TGT_TILE) {
                uint16_t gid = (uint16_t)(r->target_addr & 0xFFFFu);
                if (gid < TILEGATE_COUNT) {
                    if (ctx->gates) ctx->gates[gid] = GATE_OPEN;
                    if (ctx->active_open) ctx->active_open[gid] = 1;
                    return 0;
                }
            }
            return -2;

        case WH_OP_GATE_CLOSE:
            if (r->target_kind == WH_TGT_TILE) {
                uint16_t gid = (uint16_t)(r->target_addr & 0xFFFFu);
                if (gid < TILEGATE_COUNT) {
                    if (ctx->gates) ctx->gates[gid] = GATE_CLOSE;
                    if (ctx->active_open) ctx->active_open[gid] = 0;
                    return 0;
                }
            }
            return -2;

        /* The following are intentionally no-ops in Phase 3:
         * they exist to be interpreted by higher layers (scheduler/FS/policies)
         * while still being replayable via WH records.
         */
        case WH_OP_NOP:
        case WH_OP_TICK:
        case WH_OP_DECAY:
        case WH_OP_SLEEP:
        case WH_OP_WAKE:
        case WH_OP_KILL:
        case WH_OP_IPC:
        default:
            return 0;
    }
}