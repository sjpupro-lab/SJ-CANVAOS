#include <stdio.h>
#include <assert.h>
#include "../include/canvasos_types.h"
#include <string.h>
#include <stdlib.h>
#include "../include/canvasos_sched.h"
#include "../include/engine_time.h"
#include "../include/canvasos_opcodes.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvasos_engine_ctx.h"


static void test_wh_opcode_gate_exec(void) {
    EngineContext ctx = {0};

    static GateState gates[TILEGATE_COUNT];
    static uint8_t   active[TILE_COUNT];
    static Cell      cells[CANVAS_W * CANVAS_H];

    ctx.cells = cells;
    ctx.cells_count = (uint32_t)(CANVAS_W * CANVAS_H);
    ctx.gates = gates;
    ctx.active_open = active;

    uint16_t volh = 123;
    uint16_t volt = 456;

    gate_close_tile(&ctx, volh);
    gate_close_tile(&ctx, volt);

    WhRecord r = {0};
    r.tick_or_event = 1;
    r.target_kind = WH_TGT_TILE;
    r.flags = 0;

    /* open volh */
    r.opcode_index = WH_OP_GATE_OPEN;
    r.target_addr  = (uint32_t)volh;
    wh_exec_record(&ctx, &r);

    /* open volt */
    r.tick_or_event = 2;
    r.target_addr  = (uint32_t)volt;
    wh_exec_record(&ctx, &r);

    assert(gate_is_open_tile(&ctx, volh) == 1);
    assert(gate_is_open_tile(&ctx, volt) == 1);

    /* close volh */
    r.tick_or_event = 3;
    r.opcode_index = WH_OP_GATE_CLOSE;
    r.target_addr  = (uint32_t)volh;
    wh_exec_record(&ctx, &r);

    /* close volt */
    r.tick_or_event = 4;
    r.target_addr  = (uint32_t)volt;
    wh_exec_record(&ctx, &r);

    assert(gate_is_open_tile(&ctx, volh) == 0);
    assert(gate_is_open_tile(&ctx, volt) == 0);
}


int main(void) {
    printf("=== TEST: Phase 3 Scheduler ===\n");

    ActiveSet aset; memset(&aset, 0, sizeof(aset));
    Scheduler sc;
    sched_init(&sc, &aset);
    /* Bind EngineContext (WH/BH + formal gate ops) */
    static Cell *cells = NULL;
    static GateState gates[TILEGATE_COUNT];
    static EngineContext ctx;
    if (!cells) {
        cells = (Cell*)calloc((size_t)CANVAS_W*CANVAS_H, sizeof(Cell));
    }
    memset(&ctx, 0, sizeof(ctx));
    memset(gates, 0, sizeof(gates));
    ctx.cells = cells;
    ctx.cells_count = (uint32_t)CANVAS_W*CANVAS_H;
    ctx.gates = gates;
    ctx.active_open = aset.open;
    ctx.rules = NULL;
    sched_bind_ctx(&sc, &ctx);


    /* spawn 2 processes with different gate spaces */
    GateSpace sp0 = {.volh=100, .volt=101};
    GateSpace sp1 = {.volh=200, .volt=201};

    int pid0 = sched_spawn(&sc, sp0, 5, 10);
    int pid1 = sched_spawn(&sc, sp1, 2, 10);
    assert(pid0 > 0 && pid1 > 0);
    printf("spawned pid0=%d pid1=%d\n", pid0, pid1);

    /* gates should be open after spawn */
    assert(aset.open[100] && aset.open[101]);
    assert(aset.open[200] && aset.open[201]);
    printf("gates open after spawn OK\n");

    /* tick until pid1 sleeps (energy=2) */
    sched_tick(&sc); /* tick1: pid0=4, pid1=1 */
    sched_tick(&sc); /* tick2: pid0=3, pid1=0→sleep */
    assert(sc.procs[1].state == PROC_SLEEPING);
    assert(!aset.open[200] && !aset.open[201]);  /* gates closed */
    printf("pid1 sleeping after 2 ticks, gates closed OK\n");

    /* pid0 still running */
    assert(sc.procs[0].state == PROC_RUNNING);
    assert(aset.open[100] && aset.open[101]);

    /* recharge pid1 */
    sched_recharge(&sc, (uint32_t)pid1, 5);
    assert(sc.procs[1].state == PROC_RUNNING);
    assert(aset.open[200] && aset.open[201]);
    printf("pid1 recharged and running, gates re-opened OK\n");

    /* sched_owner: x/y inside VOLT tile of sp1 */
    uint16_t vx = (uint16_t)((201 % TILES_X) * TILE);
    uint16_t vy = (uint16_t)((201 / TILES_X) * TILE);
    int owner = sched_owner(&sc, vx, vy);
    printf("owner at (%u,%u) = pid%d (expected pid1=%d)\n", vx, vy, owner, pid1);
    assert(owner == pid1);

    /* kill pid0 */
    sched_kill(&sc, (uint32_t)pid0);
    assert(sc.procs[0].state == PROC_ZOMBIE);
    assert(!aset.open[100] && !aset.open[101]);
    printf("pid0 killed, gates closed OK\n");

    /* Phase 4/5 stub smoke test */
    sched_set_cvp(&sc, (uint32_t)pid1, 0xDEAD);
    assert(sc.procs[1].cvp_section == 0xDEAD);
    IpcMsg msg = {0};
    msg.src_pid = (uint32_t)pid1;
    msg.dst_canvas = 0;
    msg.dst_pid = (uint32_t)pid1; /* self loopback */
    msg.payload_key.gate_id = 123;
    msg.payload_key.slot = 9;
    sc.tick = 999;
    assert(sched_ipc_send(&sc, &msg) == 0);
    IpcMsg got = {0};
    assert(sched_ipc_recv(&sc, (uint32_t)pid1, &got) == 0);
    assert(got.payload_key.gate_id == 123 && got.payload_key.slot == 9);
    printf("Phase 4/5: cvp_section OK, IPC WH relay OK\n");

    test_wh_opcode_gate_exec();
    sched_dump(&sc);
    printf("[PASS] Phase 3 Scheduler\n");
    return 0;
}


