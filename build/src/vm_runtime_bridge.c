/*
 * vm_runtime_bridge.c — Phase-9/10: VM ↔ System Runtime Bridge
 *
 * Replaces WH-only stubs in vm.c with actual system calls:
 *   VM_SEND  → pipe_write (real IPC)
 *   VM_RECV  → pipe_read  (real IPC)
 *   VM_SPAWN → proc_spawn (real process creation)
 *   VM_SYSCALL → syscall_dispatch (already connected, enhanced)
 *
 * Usage: Call vm_bridge_init() at boot time with system tables,
 * then the VM opcodes will use real system resources.
 */
#include "../include/canvasos_vm.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_syscall.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

/* ── Global bridge state ─────────────────────────────── */
static ProcTable *g_vm_pt   = NULL;
static PipeTable *g_vm_pipe = NULL;
static bool       g_vm_bridge_active = false;

void vm_bridge_init(ProcTable *pt, PipeTable *pipe) {
    g_vm_pt   = pt;
    g_vm_pipe = pipe;
    g_vm_bridge_active = (pt != NULL && pipe != NULL);
}

bool vm_bridge_is_active(void) {
    return g_vm_bridge_active;
}

/* ── VM_SEND: write byte to pipe ─────────────────────── */
int vm_exec_send(PipeTable *pipes, EngineContext *ctx, VmState *vm) {
    if (!vm || !ctx) return -1;

    PipeTable *pt = pipes ? pipes : g_vm_pipe;
    if (!pt) return -1;

    int pipe_id = (int)(vm->reg_A & 0xFFu);
    uint8_t ch = vm->reg_R;

    int rc = pipe_write(pt, ctx, pipe_id, &ch, 1);

    /* Also log to WH for determinism replay */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x73; /* WH_OP_PIPE_WRITE */
    wr.param0        = ch;
    wr.target_addr   = vm->reg_A;
    wr.target_kind   = WH_TGT_FS_SLOT;
    wr.flags         = (uint8_t)(rc > 0 ? 1 : 0);
    wh_write_record(ctx, ctx->tick, &wr);

    /* Set R to success/failure indicator */
    vm->reg_R = (rc > 0) ? 1 : 0;
    return rc;
}

/* ── VM_RECV: read byte from pipe ────────────────────── */
int vm_exec_recv(PipeTable *pipes, EngineContext *ctx, VmState *vm) {
    if (!vm || !ctx) return -1;

    PipeTable *pt = pipes ? pipes : g_vm_pipe;
    if (!pt) return -1;

    int pipe_id = (int)(vm->reg_A & 0xFFu);
    uint8_t ch = 0;

    int n = pipe_read(pt, ctx, pipe_id, &ch, 1);

    /* Log to WH */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x74; /* WH_OP_PIPE_READ */
    wr.param0        = ch;
    wr.target_addr   = vm->reg_A;
    wr.target_kind   = WH_TGT_FS_SLOT;
    wr.flags         = (uint8_t)(n > 0 ? 1 : 0);
    wh_write_record(ctx, ctx->tick, &wr);

    if (n > 0) vm->reg_R = ch;
    else       vm->reg_R = 0;

    return n;
}

/* ── VM_SPAWN: create a real process ─────────────────── */
int vm_exec_spawn(ProcTable *pt, EngineContext *ctx, VmState *vm) {
    if (!vm || !ctx) return -1;

    ProcTable *proc = pt ? pt : g_vm_pt;
    if (!proc) return -1;

    uint16_t code_tile = (uint16_t)(vm->reg_A & 0xFFFF);
    uint32_t energy    = (uint32_t)vm->reg_G;
    uint8_t  lane_id   = 1; /* default lane */

    int pid = proc_spawn(proc, vm->pid, code_tile, energy ? energy : 100, lane_id);

    /* Log to WH */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = WH_OP_PROC_SPAWN;
    wr.param0        = (uint8_t)(pid >= 0 ? pid : 0);
    wr.target_addr   = vm->reg_A;
    wr.target_kind   = WH_TGT_CELL;
    wr.flags         = (uint8_t)(pid >= 0 ? 1 : 0);
    wh_write_record(ctx, ctx->tick, &wr);

    if (pid >= 0)
        vm->reg_R = (uint8_t)pid;
    else
        vm->reg_R = 0;

    return pid;
}

/* ── VM_EXIT: terminate current process ──────────────── */
int vm_exec_exit(ProcTable *pt, EngineContext *ctx, VmState *vm) {
    if (!vm || !ctx) return -1;

    ProcTable *proc = pt ? pt : g_vm_pt;
    if (!proc) return -1;

    uint8_t exit_code = vm->reg_R;
    vm->running = false;

    return proc_exit(proc, vm->pid, exit_code);
}

/* ── VM_SYSCALL enhanced: with full dispatch ─────────── */
int vm_exec_syscall(EngineContext *ctx, VmState *vm) {
    if (!vm || !ctx) return -1;

    uint8_t  nr = (uint8_t)(vm->reg_A & 0xFF);
    uint32_t a0 = (uint32_t)vm->reg_G;
    uint32_t a1 = (uint32_t)vm->reg_R;

    int result = syscall_dispatch(ctx, vm->pid, nr, a0, a1, 0);
    vm->reg_R = (uint8_t)(result & 0xFF);

    return result;
}

/* ── Pipe creation for VM IPC ────────────────────────── */
int vm_create_pipe(EngineContext *ctx, VmState *sender, VmState *receiver) {
    if (!ctx || !sender || !receiver) return -1;
    if (!g_vm_pipe) return -1;

    return pipe_create(g_vm_pipe, ctx, sender->pid, receiver->pid);
}

/* ── Run VM with bridge (enhanced vm_run) ────────────── */
int vm_run_bridged(EngineContext *ctx, VmState *vm,
                   ProcTable *pt, PipeTable *pipes) {
    if (!ctx || !vm) return -1;

    /* Temporarily set bridge pointers if provided */
    ProcTable *old_pt   = g_vm_pt;
    PipeTable *old_pipe = g_vm_pipe;

    if (pt) g_vm_pt = pt;
    if (pipes) g_vm_pipe = pipes;
    g_vm_bridge_active = (g_vm_pt != NULL && g_vm_pipe != NULL);

    int rc = vm_run(ctx, vm);

    /* Restore */
    g_vm_pt   = old_pt;
    g_vm_pipe = old_pipe;
    g_vm_bridge_active = (g_vm_pt != NULL && g_vm_pipe != NULL);

    return rc;
}
