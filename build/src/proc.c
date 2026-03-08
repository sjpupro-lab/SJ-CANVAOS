/*
 * proc.c — Phase-8: Process Model Implementation
 */
#include "../include/canvasos_proc.h"
#include "../include/canvasos_signal.h"
#include "../include/engine_time.h"
#include "../include/canvasos_gate_ops.h"
#include <string.h>
#include <stdio.h>

#define PROC_SLOT_FREE_PID  0xFFFFFFFFu

static int proc_slot_alloc(ProcTable *pt) {
    if (!pt || pt->free_count == 0) return -1;
    pt->free_count--;
    return (int)pt->freelist[pt->free_count];
}

static void proc_slot_free(ProcTable *pt, int slot) {
    if (!pt || slot < 0 || slot >= PROC8_MAX) return;
    if (pt->free_count >= PROC8_MAX) return;
    pt->freelist[pt->free_count] = (uint16_t)slot;
    pt->free_count++;
}

static GateSpace proc_space_for_slot(int slot) {
    GateSpace sp;
    sp.volh = (uint16_t)(slot % TILEGATE_COUNT);
    sp.volt = (uint16_t)((slot + PROC8_MAX) % TILEGATE_COUNT);
    return sp;
}

static void proc_write_wh(EngineContext *ctx, uint8_t opcode, const Proc8 *p,
                          uint8_t param0, uint8_t param1) {
    if (!ctx || !p) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index = opcode;
    r.param0 = param0;
    r.target_addr = p->pid;
    r.target_kind = WH_TGT_PROC;
    r.param1 = param1;
    wh_write_record(ctx, ctx->tick, &r);
}

void proctable_init(ProcTable *pt, EngineContext *ctx) {
    memset(pt, 0, sizeof(*pt));
    pt->ctx = ctx;
    pt->next_pid = 1;

    for (int i = 0; i < PROC8_MAX; i++) {
        pt->procs[i].pid = PROC_SLOT_FREE_PID;
        pt->procs[i].parent_pid = PROC_SLOT_FREE_PID;
        pt->procs[i].state = PROC_ZOMBIE;
    }

    pt->procs[0].pid = PID_INIT;
    pt->procs[0].parent_pid = PID_INIT;
    pt->procs[0].state = PROC_RUNNING;
    pt->procs[0].space = proc_space_for_slot(0);
    pt->procs[0].energy = 255;
    pt->procs[0].energy_max = 255;
    pt->procs[0].tick_born = ctx ? ctx->tick : 0;
    pt->procs[0].tick_last = ctx ? ctx->tick : 0;
    gate_open_space_ctx(ctx, pt->procs[0].space);
    if (ctx) bh_set_energy(ctx, PID_INIT, 255, 255);

    pt->free_head = 0;
    pt->free_count = PROC8_MAX - 1;
    for (int i = 1; i < PROC8_MAX; i++) {
        pt->freelist[i - 1] = (uint16_t)i;
    }
    pt->count = 1;
}

Proc8 *proc_find(ProcTable *pt, uint32_t pid) {
    if (!pt) return NULL;
    for (int i = 0; i < PROC8_MAX; i++) {
        if (pt->procs[i].pid == pid && pt->procs[i].pid != PROC_SLOT_FREE_PID)
            return &pt->procs[i];
    }
    return NULL;
}

int proc_spawn(ProcTable *pt, uint32_t parent_pid,
               uint16_t code_tile, uint32_t energy, uint8_t lane_id) {
    if (!pt) return -1;
    int slot = proc_slot_alloc(pt);
    if (slot < 0) return -1;

    Proc8 *parent = proc_find(pt, parent_pid);
    Proc8 *p = &pt->procs[slot];
    memset(p, 0, sizeof(*p));
    p->pid = pt->next_pid++;
    p->parent_pid = parent ? parent_pid : PID_INIT;
    p->state = PROC_RUNNING;
    p->space = proc_space_for_slot(slot);
    p->code_tile = code_tile;
    p->code_tiles = 1;
    p->stack_tile = (uint16_t)((code_tile + 1u) % TILE_COUNT);
    p->energy = energy;
    p->energy_max = energy;
    p->tick_born = pt->ctx ? pt->ctx->tick : 0;
    p->tick_last = p->tick_born;
    p->lane_id = lane_id;

    gate_open_space_ctx(pt->ctx, p->space);
    if (pt->ctx) {
        bh_set_energy(pt->ctx, (uint16_t)p->pid,
                      (uint8_t)(energy > 255 ? 255 : energy),
                      (uint8_t)(energy > 255 ? 255 : energy));
        proc_write_wh(pt->ctx, WH_OP_PROC_SPAWN, p,
                      lane_id, (uint8_t)(code_tile & 0xFFu));
    }
    pt->count++;
    return (int)p->pid;
}

int proc_exec(ProcTable *pt, uint32_t pid, uint16_t new_code_tile) {
    Proc8 *p = proc_find(pt, pid);
    if (!p) return -1;
    uint16_t old = p->code_tile;
    p->code_tile = new_code_tile;
    p->code_tiles = 1;
    if (pt->ctx) proc_write_wh(pt->ctx, WH_OP_PROC_EXEC, p,
                               (uint8_t)(old & 0xFFu),
                               (uint8_t)(new_code_tile & 0xFFu));
    return 0;
}

int proc_exit(ProcTable *pt, uint32_t pid, uint8_t exit_code) {
    if (!pt || pid == PID_INIT) return -1;
    Proc8 *p = proc_find(pt, pid);
    if (!p) return -1;

    p->state = PROC_ZOMBIE;
    p->exit_code = exit_code;
    p->energy = 0;
    p->tick_last = pt->ctx ? pt->ctx->tick : p->tick_last;

    for (int i = 0; i < PROC8_MAX; i++) {
        if (pt->procs[i].pid != PROC_SLOT_FREE_PID && pt->procs[i].parent_pid == pid)
            pt->procs[i].parent_pid = PID_INIT;
    }

    gate_close_space_ctx(pt->ctx, p->space);
    if (pt->ctx) {
        bh_set_energy(pt->ctx, (uint16_t)p->pid, 0, (uint8_t)(p->energy_max > 255 ? 255 : p->energy_max));
        proc_write_wh(pt->ctx, WH_OP_PROC_EXIT, p, exit_code, 0);
    }
    if (p->parent_pid != PROC_SLOT_FREE_PID && p->parent_pid != pid)
        (void)sig_send(pt, p->parent_pid, SIG_CHILD);
    return 0;
}

int proc_wait(ProcTable *pt, uint32_t parent_pid, uint8_t *status) {
    if (!pt) return -1;
    for (int i = 0; i < PROC8_MAX; i++) {
        Proc8 *p = &pt->procs[i];
        if (p->pid != PROC_SLOT_FREE_PID && p->parent_pid == parent_pid && p->state == PROC_ZOMBIE) {
            uint32_t pid = p->pid;
            if (status) *status = p->exit_code;
            memset(p, 0, sizeof(*p));
            p->pid = PROC_SLOT_FREE_PID;
            p->parent_pid = PROC_SLOT_FREE_PID;
            p->state = PROC_ZOMBIE;
            proc_slot_free(pt, i);
            if (pt->count > 0) pt->count--;
            return (int)pid;
        }
    }
    return -1;
}

int proc_tick(ProcTable *pt) {
    if (!pt) return 0;
    int ran = 0;
    for (int i = 0; i < PROC8_MAX; i++) {
        Proc8 *p = &pt->procs[i];
        if (p->pid == PROC_SLOT_FREE_PID || p->state == PROC_ZOMBIE) continue;

        (void)sig_check(pt, p->pid);
        if (p->state != PROC_RUNNING) continue;

        ran++;
        if (p->energy > 0) p->energy--;
        p->tick_last = pt->ctx ? pt->ctx->tick : p->tick_last;

        if (pt->ctx) bh_set_energy(pt->ctx, (uint16_t)p->pid,
                                   (uint8_t)(p->energy > 255 ? 255 : p->energy),
                                   (uint8_t)(p->energy_max > 255 ? 255 : p->energy_max));

        if (p->energy == 0) {
            p->state = PROC_SLEEPING;
            gate_close_space_ctx(pt->ctx, p->space);
            if (pt->ctx) proc_write_wh(pt->ctx, WH_OP_SLEEP, p, 0, 0);
        }
    }
    return ran;
}

int proc_count_children(ProcTable *pt, uint32_t parent_pid) {
    int count = 0;
    for (int i = 0; i < PROC8_MAX; i++)
        if (pt->procs[i].pid != PROC_SLOT_FREE_PID &&
            pt->procs[i].parent_pid == parent_pid)
            count++;
    return count;
}

void proc_dump(const ProcTable *pt) {
    printf("  PID  STATE      ENERGY  LANE  PARENT  CODE_TILE\n");
    printf("  ───  ─────      ──────  ────  ──────  ─────────\n");
    for (int i = 0; i < PROC8_MAX; i++) {
        const Proc8 *p = &pt->procs[i];
        if (p->pid == PROC_SLOT_FREE_PID) continue;
        const char *st = p->state == PROC_RUNNING  ? "RUNNING" :
                         p->state == PROC_SLEEPING ? "SLEEP  " :
                         p->state == PROC_BLOCKED  ? "BLOCKED" :
                                                     "ZOMBIE ";
        printf("  %3u  %s  %6u  %4u  %6u  %5u\n",
               p->pid, st, p->energy, p->lane_id,
               p->parent_pid, p->code_tile);
    }
    printf("  active: %u / %u\n", pt->count, PROC8_MAX);
}
