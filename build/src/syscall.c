/* syscall.c — Phase-8: System Call Dispatch */
#include "../include/canvasos_syscall.h"
#include "../include/engine_time.h"
#include <string.h>

static SyscallHandler g_handlers[SYS_MAX];

static int handler_getpid(EngineContext *ctx, uint32_t pid,
                          uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)ctx; (void)a0; (void)a1; (void)a2;
    return (int)pid;
}

static int handler_time(EngineContext *ctx, uint32_t pid,
                        uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a0; (void)a1; (void)a2;
    return ctx ? (int)ctx->tick : -1;
}

static int handler_tick(EngineContext *ctx, uint32_t pid,
                        uint32_t a0, uint32_t a1, uint32_t a2) {
    (void)pid; (void)a0; (void)a1; (void)a2;
    return engctx_tick(ctx);
}

void syscall_init(void) {
    memset(g_handlers, 0, sizeof(g_handlers));
    syscall_register(SYS_GETPID, handler_getpid);
    syscall_register(SYS_TIME, handler_time);
    syscall_register(SYS_TICK, handler_tick);
}

int syscall_register(uint8_t nr, SyscallHandler handler) {
    if (nr >= SYS_MAX) return -1;
    g_handlers[nr] = handler;
    return 0;
}

static void syscall_wh(EngineContext *ctx, uint32_t pid, uint8_t nr, uint8_t ok) {
    if (!ctx) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index = WH_OP_SYSCALL;
    r.param0 = nr;
    r.target_addr = pid;
    r.target_kind = WH_TGT_PROC;
    r.flags = ok;
    wh_write_record(ctx, ctx->tick, &r);
}

int syscall_dispatch(EngineContext *ctx, uint32_t pid, uint8_t nr,
                     uint32_t a0, uint32_t a1, uint32_t a2) {
    if (nr >= SYS_MAX || !g_handlers[nr]) {
        syscall_wh(ctx, pid, nr, 0);
        return -38; /* -ENOSYS */
    }
    syscall_wh(ctx, pid, nr, 1);
    return g_handlers[nr](ctx, pid, a0, a1, a2);
}
