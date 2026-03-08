/*
 * syscall_bindings.c — Phase-8/10: Syscall Extension Bindings
 *
 * Bridges the syscall dispatch table with Phase-10 subsystems:
 *   SYS_OPEN/READ/WRITE/CLOSE → fd.c
 *   SYS_MKDIR/RM             → path.c
 *   SYS_SPAWN/EXIT/WAIT/KILL → proc.c + signal.c
 *   SYS_PIPE/DUP             → pipe.c + fd.c
 *   SYS_TIMEWARP/DET_MODE    → timewarp.c + detmode.c
 *   SYS_GATE_OPEN/CLOSE      → gate_ops.c
 *   SYS_MPROTECT             → mprotect.c
 *   SYS_HASH/SNAPSHOT        → engine_ctx.c + cvp_io.c
 */
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_path.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_timewarp.h"
#include "../include/canvasos_detmode.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvasos_mprotect.h"
#include "../include/canvas_determinism.h"
#include <string.h>

/* ── Global context pointers (set once at boot) ──────── */
static ProcTable      *g_pt   = NULL;
static PipeTable      *g_pipe = NULL;
static PathContext    *g_pc   = NULL;
static TimeWarp       *g_tw   = NULL;
static DetMode        *g_dm   = NULL;
static TileProtection *g_tp   = NULL;

void syscall_bind_context(ProcTable *pt, PipeTable *pipe,
                          PathContext *pc, TimeWarp *tw,
                          DetMode *dm, TileProtection *tp) {
    g_pt   = pt;
    g_pipe = pipe;
    g_pc   = pc;
    g_tw   = tw;
    g_dm   = dm;
    g_tp   = tp;
}

/* ══════════════════════════════════════════════════════
 * File I/O syscalls
 * ══════════════════════════════════════════════════════ */

static int sc_open(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    return fd_open(ctx, pid, (const char *)(uintptr_t)a0, (uint8_t)a1);
}

static int sc_read(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    return fd_read(ctx, pid, (int)a0, (uint8_t *)(uintptr_t)a1, (uint16_t)a2);
}

static int sc_write(EngineContext *ctx, uint32_t pid,
                    uint32_t a0, uint32_t a1, uint32_t a2) {
    return fd_write(ctx, pid, (int)a0, (const uint8_t *)(uintptr_t)a1, (uint16_t)a2);
}

static int sc_close(EngineContext *ctx, uint32_t pid,
                    uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a1; (void)a2;
    return fd_close(ctx, pid, (int)a0);
}

static int sc_seek(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    return fd_seek(ctx, pid, (int)a0, (uint16_t)a1);
}

/* ══════════════════════════════════════════════════════
 * Directory syscalls
 * ══════════════════════════════════════════════════════ */

static int sc_mkdir(EngineContext *ctx, uint32_t pid,
                    uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a1; (void)a2;
    if (!g_pc) return -1;
    return path_mkdir(ctx, g_pc, (const char *)(uintptr_t)a0);
}

static int sc_rm(EngineContext *ctx, uint32_t pid,
                 uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a1; (void)a2;
    if (!g_pc) return -1;
    return path_rm(ctx, g_pc, (const char *)(uintptr_t)a0);
}

/* ══════════════════════════════════════════════════════
 * Process syscalls
 * ══════════════════════════════════════════════════════ */

static int sc_spawn(EngineContext *ctx, uint32_t pid,
                    uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx;
    if (!g_pt) return -1;
    return proc_spawn(g_pt, pid, (uint16_t)a0, a1, (uint8_t)a2);
}

static int sc_exit(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)a1; (void)a2;
    if (!g_pt) return -1;
    return proc_exit(g_pt, pid, (uint8_t)a0);
}

static int sc_wait(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)a0; (void)a2;
    if (!g_pt) return -1;
    uint8_t status = 0;
    int child = proc_wait(g_pt, pid, &status);
    /* Store status at a1 if provided */
    if (a1 && child >= 0)
        *(uint8_t *)(uintptr_t)a1 = status;
    return child;
}

static int sc_kill(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)pid; (void)a2;
    if (!g_pt) return -1;
    return sig_send(g_pt, a0, (uint8_t)a1);
}

static int sc_signal(EngineContext *ctx, uint32_t pid,
                     uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)a2;
    if (!g_pt) return -1;
    if (a0 == 0)
        sig_mask_set(g_pt, pid, (uint8_t)a1);
    else
        sig_mask_clear(g_pt, pid, (uint8_t)a1);
    return 0;
}

/* ══════════════════════════════════════════════════════
 * IPC syscalls
 * ══════════════════════════════════════════════════════ */

static int sc_pipe(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    if (!g_pipe) return -1;
    return pipe_create(g_pipe, ctx, pid, a0 ? a0 : a1);
}

static int sc_dup(EngineContext *ctx, uint32_t pid,
                  uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)a2;
    return fd_dup(ctx, pid, (int)a0, (int)a1);
}

/* ══════════════════════════════════════════════════════
 * Gate/Protection syscalls
 * ══════════════════════════════════════════════════════ */

static int sc_gate_open(EngineContext *ctx, uint32_t pid,
                        uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a1; (void)a2;
    if (a0 >= TILE_COUNT) return -1;
    gate_open_tile(ctx, (uint16_t)a0);
    return 0;
}

static int sc_gate_close(EngineContext *ctx, uint32_t pid,
                         uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a1; (void)a2;
    if (a0 >= TILE_COUNT) return -1;
    gate_close_tile(ctx, (uint16_t)a0);
    return 0;
}

static int sc_mprotect(EngineContext *ctx, uint32_t pid,
                       uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)pid;
    if (!g_tp) return -1;
    /* a0 = uid, a1 = tile_id, a2 = perm */
    return tile_check(g_tp, (uint8_t)a0, (uint16_t)a1, (uint8_t)a2);
}

/* ══════════════════════════════════════════════════════
 * Info syscalls (already registered in syscall_init,
 * we add GETPPID and HASH)
 * ══════════════════════════════════════════════════════ */

static int sc_getppid(EngineContext *ctx, uint32_t pid,
                      uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)a0; (void)a1; (void)a2;
    if (!g_pt) return -1;
    Proc8 *p = proc_find(g_pt, pid);
    return p ? (int)p->parent_pid : -1;
}

static int sc_hash(EngineContext *ctx, uint32_t pid,
                   uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a0; (void)a1; (void)a2;
    if (!ctx) return -1;
    return (int)dk_canvas_hash(ctx->cells, ctx->cells_count);
}

/* ══════════════════════════════════════════════════════
 * CanvasOS Special syscalls
 * ══════════════════════════════════════════════════════ */

static int sc_timewarp(EngineContext *ctx, uint32_t pid,
                       uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a1; (void)a2;
    if (!g_tw) return -1;
    return timewarp_goto(g_tw, ctx, a0);
}

static int sc_det_mode(EngineContext *ctx, uint32_t pid,
                       uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a1; (void)a2;
    if (!g_dm) return -1;
    det_set_all(g_dm, a0 != 0);
    det_log_change(ctx, g_dm);
    return det_is_deterministic(g_dm) ? 1 : 0;
}

static int sc_snapshot(EngineContext *ctx, uint32_t pid,
                       uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a0; (void)a1; (void)a2;
    if (!ctx) return -1;
    return (int)dk_canvas_hash(ctx->cells, ctx->cells_count);
}

/* ══════════════════════════════════════════════════════
 * Registration
 * ══════════════════════════════════════════════════════ */

void syscall_register_phase10(void) {
    /* File I/O */
    syscall_register(SYS_OPEN,    sc_open);
    syscall_register(SYS_READ,    sc_read);
    syscall_register(SYS_WRITE,   sc_write);
    syscall_register(SYS_CLOSE,   sc_close);
    syscall_register(SYS_SEEK,    sc_seek);
    syscall_register(SYS_MKDIR,   sc_mkdir);
    syscall_register(SYS_RM,      sc_rm);

    /* Process */
    syscall_register(SYS_SPAWN,   sc_spawn);
    syscall_register(SYS_EXIT,    sc_exit);
    syscall_register(SYS_WAIT,    sc_wait);
    syscall_register(SYS_KILL,    sc_kill);
    syscall_register(SYS_SIGNAL,  sc_signal);

    /* IPC */
    syscall_register(SYS_PIPE,    sc_pipe);
    syscall_register(SYS_DUP,     sc_dup);

    /* Gate/Protection */
    syscall_register(SYS_GATE_OPEN,  sc_gate_open);
    syscall_register(SYS_GATE_CLOSE, sc_gate_close);
    syscall_register(SYS_MPROTECT,   sc_mprotect);

    /* Info */
    syscall_register(SYS_GETPPID, sc_getppid);
    syscall_register(SYS_HASH,    sc_hash);

    /* CanvasOS Special */
    syscall_register(SYS_TIMEWARP,  sc_timewarp);
    syscall_register(SYS_DET_MODE,  sc_det_mode);
    syscall_register(SYS_SNAPSHOT,  sc_snapshot);
}
