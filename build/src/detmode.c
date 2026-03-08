/* detmode.c — Phase-8: Determinism Toggle */
#include "../include/canvasos_detmode.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/engine_time.h"
#include <string.h>

void det_init(DetMode *dm) {
    dm->dk1_tick_boundary = true;
    dm->dk2_integer_only  = true;
    dm->dk3_fixed_order   = true;
    dm->dk4_normalize     = true;
    dm->dk5_noise_absorb  = true;
    dm->wh_recording      = true;
    dm->nondet_since      = 0;
}

void det_set_all(DetMode *dm, bool on) {
    dm->dk1_tick_boundary = on;
    dm->dk2_integer_only  = on;
    dm->dk3_fixed_order   = on;
    dm->dk4_normalize     = on;
    dm->dk5_noise_absorb  = on;
    dm->wh_recording      = true; /* WH 기록은 항상 유지 (감사용) */
}

void det_set_dk(DetMode *dm, int dk_id, bool on) {
    switch (dk_id) {
    case 1: dm->dk1_tick_boundary = on; break;
    case 2: dm->dk2_integer_only  = on; break;
    case 3: dm->dk3_fixed_order   = on; break;
    case 4: dm->dk4_normalize     = on; break;
    case 5: dm->dk5_noise_absorb  = on; break;
    }
}

bool det_is_deterministic(const DetMode *dm) {
    return dm->dk1_tick_boundary &&
           dm->dk2_integer_only  &&
           dm->dk3_fixed_order   &&
           dm->dk4_normalize     &&
           dm->dk5_noise_absorb;
}

void det_log_change(void *ctx, const DetMode *dm) {
    EngineContext *c = (EngineContext *)ctx;
    if (!c || !dm) return;

    /* Encode DK flags as bitmask: dk1=bit0 .. dk5=bit4 */
    uint8_t flags = 0;
    if (dm->dk1_tick_boundary) flags |= 0x01;
    if (dm->dk2_integer_only)  flags |= 0x02;
    if (dm->dk3_fixed_order)   flags |= 0x04;
    if (dm->dk4_normalize)     flags |= 0x08;
    if (dm->dk5_noise_absorb)  flags |= 0x10;

    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = c->tick;
    r.opcode_index  = WH_OP_DET_MODE;
    r.param0        = flags;
    r.param1        = dm->wh_recording ? 1 : 0;
    r.target_addr   = dm->nondet_since;
    r.target_kind   = WH_TGT_CELL;
    r.flags         = det_is_deterministic(dm) ? 1 : 0;
    wh_write_record(c, c->tick, &r);
}
