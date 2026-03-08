#include "../include/canvasos_sched.h"
#include "../include/engine_time.h"
#include "../include/canvasos_gate_ops.h"
#include <string.h>
#include <stdio.h>

/* =====================================================
 * Phase 3 Scheduler — 구현 (Always-ON + Gate Filtering)
 * ===================================================== */

static void gate_open_space_local(ActiveSet *as, GateSpace sp) {
    if (!as) return;
    if (sp.volh < TILE_COUNT) as->open[sp.volh] = 1;
    if (sp.volt < TILE_COUNT) as->open[sp.volt] = 1;
}
static void gate_close_space_local(ActiveSet *as, GateSpace sp) {
    if (!as) return;
    if (sp.volh < TILE_COUNT) as->open[sp.volh] = 0;
    if (sp.volt < TILE_COUNT) as->open[sp.volt] = 0;
}
static void gate_open_space_sched(Scheduler *sc, GateSpace sp) {
    if (sc && sc->ctx) gate_open_space_ctx(sc->ctx, sp);
    else gate_open_space_local(sc ? sc->aset : NULL, sp);
}
static void gate_close_space_sched(Scheduler *sc, GateSpace sp) {
    if (sc && sc->ctx) gate_close_space_ctx(sc->ctx, sp);
    else gate_close_space_local(sc ? sc->aset : NULL, sp);
}


void sched_init(Scheduler *sc, ActiveSet *aset) {
    memset(sc, 0, sizeof(*sc));
    sc->aset     = aset;
    sc->next_pid = 1;
}

void sched_bind_ctx(Scheduler *sc, EngineContext *ctx) {
    if (!sc) return;
    sc->ctx = ctx;
    /* Mirror ActiveSet bitmap into ctx fast path if available */
    if (sc->ctx && sc->aset) sc->ctx->active_open = sc->aset->open;
}

int sched_spawn(Scheduler *sc, GateSpace space,
                uint32_t energy, uint32_t energy_max) {
    if (sc->count >= PROC_MAX) return -1;
    Process *p = &sc->procs[sc->count++];
    memset(p, 0, sizeof(*p));
    p->pid          = sc->next_pid++;
    p->state        = PROC_RUNNING;
    p->space        = space;
    p->energy       = energy;
    p->energy_max   = energy_max ? energy_max : energy;
    p->tick_born    = sc->tick;
    p->tick_last    = sc->tick;
    p->ipc_cursor   = sc->tick;  /* BUG-4 fix: skip WH records before spawn */

    /* Formal ctx: initialize BH energy state */
    if (sc->ctx) {
        uint8_t e = (energy > 255u) ? 255u : (uint8_t)energy;
        uint8_t m = (energy_max > 255u) ? 255u : (uint8_t)energy_max;
        bh_set_energy(sc->ctx, (uint16_t)p->pid, e, m);
        /* WH record: spawn heartbeat */
        WhRecord wr = {0};
        wr.tick_or_event = sc->tick;
        wr.opcode_index  = WH_OP_TICK;
        wr.flags         = 0;
        wr.param0        = 0;
        wr.target_addr   = (uint32_t)p->pid;
        wr.target_kind   = WH_TGT_PROC;
        wr.arg_state     = (uint8_t)p->state;
        wr.param1        = 0;
        wh_write_record(sc->ctx, ((uint64_t)sc->tick<<8) | (uint64_t)(p->pid & 0xFFu), &wr);
    }
    p->cvp_section  = 0;
    p->mount_canvas = 0xFFFF;
    /* open gate space on spawn */
    gate_open_space_sched(sc, space);
    return (int)p->pid;
}

int sched_tick(Scheduler *sc) {
    sc->tick++;
    int ran = 0;
    for (uint32_t i = 0; i < sc->count; i++) {
        Process *p = &sc->procs[i];
        if (p->state != PROC_RUNNING) continue;
        ran++;
        p->tick_last = sc->tick;
        if (p->energy > 0) {
            p->energy--;
        }
        if (p->energy == 0) {
            p->state = PROC_SLEEPING;
        if (sc->ctx) {
            WhRecord wr = {0};
            wr.tick_or_event = sc->tick;
            wr.opcode_index  = WH_OP_SLEEP;
            wr.flags         = 0;
            wr.param0        = 0;
            wr.target_addr   = (uint32_t)p->pid;
            wr.target_kind   = WH_TGT_PROC;
            wr.arg_state     = (uint8_t)p->state;
            wr.param1        = 0;
            wh_write_record(sc->ctx, ((uint64_t)sc->tick<<8) | (uint64_t)(p->pid & 0xFFu), &wr);
        }
            gate_close_space_sched(sc, p->space);
        }
    }
    return ran;
}

void sched_recharge(Scheduler *sc, uint32_t pid, uint32_t amount) {
    for (uint32_t i = 0; i < sc->count; i++) {
        Process *p = &sc->procs[i];
        if (p->pid != pid) continue;
        p->energy += amount;
    if (sc->ctx) {
        uint8_t e = (p->energy > 255u) ? 255u : (uint8_t)p->energy;
        uint8_t m = (p->energy_max > 255u) ? 255u : (uint8_t)p->energy_max;
        bh_set_energy(sc->ctx, (uint16_t)p->pid, e, m);
    }
        if (p->energy > p->energy_max) p->energy = p->energy_max;
        if (p->state == PROC_SLEEPING && p->energy > 0) {
            p->state = PROC_RUNNING;
            gate_open_space_sched(sc, p->space);
        if (sc->ctx) {
            WhRecord wr = {0};
            wr.tick_or_event = sc->tick;
            wr.opcode_index  = WH_OP_WAKE;
            wr.flags         = 0;
            wr.param0        = 0;
            wr.target_addr   = (uint32_t)p->pid;
            wr.target_kind   = WH_TGT_PROC;
            wr.arg_state     = (uint8_t)p->state;
            wr.param1        = 0;
            wh_write_record(sc->ctx, ((uint64_t)sc->tick<<8) | (uint64_t)(p->pid & 0xFFu), &wr);
        }
        }
        return;
    }
}

void sched_kill(Scheduler *sc, uint32_t pid) {
    for (uint32_t i = 0; i < sc->count; i++) {
        Process *p = &sc->procs[i];
        if (p->pid != pid) continue;
        p->state  = PROC_ZOMBIE;
        p->energy = 0;
        gate_close_space_sched(sc, p->space);
        return;
    }
}

int sched_owner(const Scheduler *sc, uint16_t x, uint16_t y) {
    /* VOLT tile의 x0..x0+15, y0..y0+15 범위를 체크 */
    for (uint32_t i = 0; i < sc->count; i++) {
        const Process *p = &sc->procs[i];
        if (p->state == PROC_ZOMBIE) continue;
        uint16_t volt = p->space.volt;
        uint16_t vx0  = (uint16_t)((volt % TILES_X) * TILE);
        uint16_t vy0  = (uint16_t)((volt / TILES_X) * TILE);
        if (x >= vx0 && x < (uint16_t)(vx0 + TILE) &&
            y >= vy0 && y < (uint16_t)(vy0 + TILE)) {
            return (int)p->pid;
        }
    }
    return -1;
}

void sched_dump(const Scheduler *sc) {
    static const char *snames[] = {"RUNNING","SLEEPING","BLOCKED","ZOMBIE"};
    printf("=== Scheduler tick=%u procs=%u ===\n", sc->tick, sc->count);
    for (uint32_t i = 0; i < sc->count; i++) {
        const Process *p = &sc->procs[i];
        printf("  pid=%u %-8s energy=%u/%u volh=%u volt=%u cvp=%u mount=%u\n",
               p->pid, snames[p->state & 3],
               p->energy, p->energy_max,
               p->space.volh, p->space.volt,
               p->cvp_section, p->mount_canvas);
    }
}

/* ---- Phase 4/5 stubs ---- */
void sched_set_cvp(Scheduler *sc, uint32_t pid, uint32_t cvp_section) {
    for (uint32_t i = 0; i < sc->count; i++) {
        if (sc->procs[i].pid == pid) {
            sc->procs[i].cvp_section = cvp_section;
            return;
        }
    }
}

/* Phase 5: IPC — WH relay implementation (deterministic)
 * - IPC is recorded as WH opcode (WH_IPC_SEND = 0x30)
 * - recv scans forward from proc.ipc_cursor+1 .. sc->tick
 * - consumption is deterministic via ipc_cursor update
 */
int sched_ipc_send(Scheduler *sc, const IpcMsg *msg) {
    if (!sc || !msg || !sc->ctx) return -1;

    /* WH IPC record — clean 2-cell layout:
     * C0: A=dst_pid(u32)  B=WH_IPC_SEND(0x30)  G=dst_canvas(u8)  R=slot(u8)
     * C1: A=src_pid(u32)  B=WH_TGT_PROC        G=gate_id[15:8]   R=gate_id[7:0]
     *
     * Fields use dedicated bytes → no aliasing conflicts.
     * src_pid stored as full u32 (BUG-5 fix, PROC_MAX=64 so pid fits easily).
     * gate_id(u16) split across C1.G + C1.R = 16 bits, covers full 4096 range.
     */
    uint16_t gid = msg->payload_key.gate_id;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    /* C0 */
    r.tick_or_event = msg->dst_pid;
    r.opcode_index  = 0x30;                           /* WH_IPC_SEND */
    r.flags         = (uint8_t)(msg->dst_canvas & 0xFFu);
    r.param0        = (uint8_t)(msg->payload_key.slot & 0xFFu);
    /* C1 */
    r.target_addr   = msg->src_pid;                   /* full 32-bit */
    r.target_kind   = WH_TGT_PROC;
    r.arg_state     = (uint8_t)((gid >> 8) & 0xFFu);  /* gate_id[15:8] */
    r.param1        = (uint8_t)(gid & 0xFFu);          /* gate_id[7:0]  */

    wh_write_record(sc->ctx, sc->tick, &r);
    return 0;
}

int sched_ipc_recv(Scheduler *sc, uint32_t pid, IpcMsg *out) {
    if (!sc || !out || !sc->ctx) return -1;

    Process *p = NULL;
    for (uint32_t i = 0; i < sc->count; i++) {
        if (sc->procs[i].pid == pid) { p = &sc->procs[i]; break; }
    }
    if (!p) return -1;

    /* BUG-4 fix: ipc_cursor initialized to spawn tick → skip old messages */
    uint32_t start = p->ipc_cursor + 1;
    if (start > sc->tick) return -1;

    for (uint32_t t = start; t <= sc->tick; t++) {
        WhRecord r;
        wh_read_record(sc->ctx, (uint64_t)t, &r);
        if (r.opcode_index != 0x30)      continue;
        if (r.target_kind  != WH_TGT_PROC) continue;
        if (r.tick_or_event != pid)       continue;

        /* decode matching the send layout:
         * C0: flags=dst_canvas, param0=slot
         * C1: target_addr=src_pid(32), arg_state=gate_id[15:8], param1=gate_id[7:0]
         */
        out->src_pid             = r.target_addr;        /* full u32 */
        out->dst_canvas          = (uint16_t)r.flags;
        out->dst_pid             = pid;
        out->payload_key.gate_id = ((uint16_t)r.arg_state << 8) | (uint16_t)r.param1;
        out->payload_key.slot    = r.param0;

        p->ipc_cursor = t;
        return 0;
    }
    return -1;
}
