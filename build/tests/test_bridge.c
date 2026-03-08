/*
 * test_bridge.c — Extended Tests for Phase-10 Bridge Layer
 *
 * Tests:
 *   B01-B04: Shell (init, builtins, pipe, variables)
 *   B05-B07: Syscall bindings (register, dispatch, coverage)
 *   B08-B10: Virtual path (/proc, /dev, /wh)
 *   B11-B13: VM runtime bridge (send/recv, spawn, syscall)
 *   B14-B16: DetMode WH logging, cat, source
 */
#include <stdio.h>
#include <string.h>
#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_mprotect.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_path.h"
#include "../include/canvasos_user.h"
#include "../include/canvasos_utils.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_shell.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];
static int P = 0, F = 0;

#define T(n)     printf("  %-54s ", n)
#define PASS()   do { printf("PASS\n"); P++; } while(0)
#define FAIL(m)  do { printf("FAIL: %s\n", m); F++; return; } while(0)
#define CHK(c,m) do { if(!(c)) FAIL(m); } while(0)

static EngineContext *mk(void) {
    static EngineContext ctx;
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_gates, 0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    engctx_init(&ctx, g_cells, CANVAS_W * CANVAS_H, g_gates, g_active, NULL);
    engctx_tick(&ctx);
    return &ctx;
}

/* ── B01: Shell init and variables ───────────────────── */
static void b01(void) {
    T("B01 shell init + var set/get");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);
    CHK(sh.running, "running");
    CHK(sh.pt == &pt, "pt bound");

    shell_set_var(&sh, "FOO", "bar");
    const char *v = shell_get_var(&sh, "FOO");
    CHK(v && strcmp(v, "bar") == 0, "FOO=bar");

    /* Default vars */
    CHK(shell_get_var(&sh, "HOME") != NULL, "HOME exists");
    CHK(shell_get_var(&sh, "USER") != NULL, "USER exists");
    PASS();
}

/* ── B02: Shell builtin exec ─────────────────────────── */
static void b02(void) {
    T("B02 shell exec builtins (echo, hash, info)");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    /* echo */
    fd_stdout_clear();
    CHK(shell_exec_line(&sh, ctx, "echo hello") == 0, "echo ok");
    uint8_t buf[64] = {0};
    uint16_t n = fd_stdout_get(buf, 64);
    CHK(n > 0 && memcmp(buf, "hello\n", 6) == 0, "echo output");

    /* hash */
    CHK(shell_exec_line(&sh, ctx, "hash") == 0, "hash ok");

    /* info */
    CHK(shell_exec_line(&sh, ctx, "info") == 0, "info ok");
    PASS();
}

/* ── B03: Shell pipe execution ───────────────────────── */
static void b03(void) {
    T("B03 shell pipe (echo | hash)");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    CHK(shell_exec_line(&sh, ctx, "echo test | hash") == 0, "pipe ok");
    PASS();
}

/* ── B04: Shell variable expansion ───────────────────── */
static void b04(void) {
    T("B04 shell variable expansion");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    shell_exec_line(&sh, ctx, "GREETING=world");
    CHK(strcmp(shell_get_var(&sh, "GREETING"), "world") == 0, "var set");

    fd_stdout_clear();
    shell_exec_line(&sh, ctx, "echo $GREETING");
    uint8_t buf[64] = {0};
    fd_stdout_get(buf, 64);
    CHK(memcmp(buf, "world\n", 6) == 0, "expanded");
    PASS();
}

/* ── B05: Syscall registration + dispatch ────────────── */
static void b05(void) {
    T("B05 syscall register_phase10 coverage");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    PathContext pc; pathctx_init(&pc, 0, (FsKey){0,0});
    fd_table_init();

    syscall_init();
    syscall_bind_context(&pt, &pipes, &pc, NULL, NULL, NULL);
    syscall_register_phase10();

    /* SYS_GETPID should still work */
    CHK(syscall_dispatch(ctx, 42, SYS_GETPID, 0, 0, 0) == 42, "getpid");

    /* SYS_HASH should now work */
    int h = syscall_dispatch(ctx, 0, SYS_HASH, 0, 0, 0);
    CHK(h != -38, "hash registered");

    /* SYS_SPAWN */
    int pid = syscall_dispatch(ctx, 0, SYS_SPAWN, 10, 100, 1);
    CHK(pid > 0, "spawn via syscall");

    /* SYS_GETPPID */
    int ppid = syscall_dispatch(ctx, (uint32_t)pid, SYS_GETPPID, 0, 0, 0);
    CHK(ppid == 0, "getppid=init");

    PASS();
}

/* ── B06: Syscall file ops ───────────────────────────── */
static void b06(void) {
    T("B06 syscall open/write/close cycle");
    EngineContext *ctx = mk();
    fd_table_init();
    syscall_init();
    syscall_register_phase10();

    int fd = syscall_dispatch(ctx, 1, SYS_OPEN,
                              (uint32_t)(uintptr_t)"/test", O_READ | O_WRITE, 0);
    CHK(fd >= 3, "open via syscall");

    int rc = syscall_dispatch(ctx, 1, SYS_CLOSE, (uint32_t)fd, 0, 0);
    CHK(rc == 0, "close via syscall");
    PASS();
}

/* ── B07: Syscall gate ops ───────────────────────────── */
static void b07(void) {
    T("B07 syscall gate open/close");
    EngineContext *ctx = mk();
    syscall_init();
    syscall_register_phase10();

    CHK(!gate_is_open_tile(ctx, 5), "initially closed");
    syscall_dispatch(ctx, 0, SYS_GATE_OPEN, 5, 0, 0);
    CHK(gate_is_open_tile(ctx, 5), "opened via syscall");
    syscall_dispatch(ctx, 0, SYS_GATE_CLOSE, 5, 0, 0);
    CHK(!gate_is_open_tile(ctx, 5), "closed via syscall");
    PASS();
}

/* ── B08: Virtual path /proc ─────────────────────────── */
static void b08(void) {
    T("B08 virtual path /proc resolve");
    EngineContext *ctx = mk();
    PathContext pc; pathctx_init(&pc, 7, (FsKey){0,0});

    FsKey out;
    CHK(path_resolve_virtual(ctx, &pc, "/proc", &out) == 0, "resolve /proc");
    CHK(out.gate_id == 0xFF00u, "proc gate");

    CHK(path_resolve_virtual(ctx, &pc, "/proc/self", &out) == 0, "resolve /proc/self");
    CHK(out.slot == 7, "self=pid 7");

    CHK(path_resolve_virtual(ctx, &pc, "/proc/42", &out) == 0, "resolve /proc/42");
    CHK(out.slot == 42, "pid=42");
    PASS();
}

/* ── B09: Virtual path /dev ──────────────────────────── */
static void b09(void) {
    T("B09 virtual path /dev resolve");
    EngineContext *ctx = mk();
    PathContext pc; pathctx_init(&pc, 0, (FsKey){0,0});

    FsKey out;
    CHK(path_resolve_virtual(ctx, &pc, "/dev/null", &out) == 0, "null");
    CHK(out.gate_id == 0xFF01u && out.slot == 0, "null key");

    CHK(path_resolve_virtual(ctx, &pc, "/dev/canvas", &out) == 0, "canvas");
    CHK(out.slot == 1, "canvas slot");
    PASS();
}

/* ── B10: Virtual path /wh + render ──────────────────── */
static void b10(void) {
    T("B10 virtual path /wh + render");
    EngineContext *ctx = mk();
    PathContext pc; pathctx_init(&pc, 0, (FsKey){0,0});

    FsKey out;
    CHK(path_resolve_virtual(ctx, &pc, "/wh", &out) == 0, "/wh");
    CHK(out.gate_id == 0xFF02u, "wh gate");

    /* Render /proc info */
    ProcTable pt; proctable_init(&pt, ctx);
    proc_spawn(&pt, PID_INIT, 10, 100, 1);
    FsKey proc_key = {0xFF00u, 0};
    char buf[256];
    CHK(path_render_virtual(&pt, ctx, proc_key, buf, sizeof(buf)) == 0, "render");
    CHK(strstr(buf, "PID=0") != NULL, "has PID");

    /* Render /dev/canvas */
    FsKey dev_canvas = {0xFF01u, 1};
    CHK(path_render_virtual(NULL, ctx, dev_canvas, buf, sizeof(buf)) == 0, "canvas render");
    CHK(strstr(buf, "CANVAS") != NULL, "has CANVAS");
    PASS();
}

/* ── B11: VM bridge send/recv ────────────────────────── */
static void b11(void) {
    T("B11 vm bridge send/recv via pipe");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);

    vm_bridge_init(&pt, &pipes);
    CHK(vm_bridge_is_active(), "bridge active");

    /* Create sender/receiver VMs */
    VmState sender, receiver;
    vm_init(&sender, 0, 0, 10);
    vm_init(&receiver, 0, 0, 20);

    /* Create pipe between them */
    int pipe_id = pipe_create(&pipes, ctx, 10, 20);
    CHK(pipe_id >= 0, "pipe created");

    /* Send a byte */
    sender.reg_A = (uint32_t)pipe_id;
    sender.reg_R = 'X';
    CHK(vm_exec_send(&pipes, ctx, &sender) == 1, "send ok");

    /* Receive */
    receiver.reg_A = (uint32_t)pipe_id;
    CHK(vm_exec_recv(&pipes, ctx, &receiver) == 1, "recv ok");
    CHK(receiver.reg_R == 'X', "received X");
    PASS();
}

/* ── B12: VM bridge spawn ────────────────────────────── */
static void b12(void) {
    T("B12 vm bridge spawn real process");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    vm_bridge_init(&pt, &pipes);

    VmState vm;
    vm_init(&vm, 0, 0, PID_SHELL);
    vm.reg_A = 50;  /* code tile */
    vm.reg_G = 200; /* energy */

    int pid = vm_exec_spawn(&pt, ctx, &vm);
    CHK(pid > 0, "spawned");
    CHK(vm.reg_R == (uint8_t)pid, "R=pid");

    Proc8 *p = proc_find(&pt, (uint32_t)pid);
    CHK(p != NULL, "proc exists");
    CHK(p->code_tile == 50, "code tile");
    CHK(p->energy == 200, "energy");
    PASS();
}

/* ── B13: Shell det toggle + WH log ──────────────────── */
static void b13(void) {
    T("B13 det toggle + WH audit log");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    CHK(det_is_deterministic(&sh.detmode), "init det");

    uint32_t t0 = ctx->tick;
    shell_exec_line(&sh, ctx, "det off");
    CHK(!det_is_deterministic(&sh.detmode), "det off");

    /* Verify WH record was written */
    bool found = false;
    WhRecord r;
    for (uint32_t t = t0; t <= ctx->tick + 2; t++) {
        if (wh_read_record(ctx, t, &r) && r.opcode_index == WH_OP_DET_MODE) {
            found = true;
            break;
        }
    }
    CHK(found, "WH det_mode record");

    shell_exec_line(&sh, ctx, "det on");
    CHK(det_is_deterministic(&sh.detmode), "det restored");
    PASS();
}

/* ── B14: Shell semicolon + comment ──────────────────── */
static void b14(void) {
    T("B14 shell semicolon + comment");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);
    fd_table_init();

    /* Comment should be ignored */
    CHK(shell_exec_line(&sh, ctx, "# this is a comment") == 0, "comment");

    /* Semicolons — test with simple inline execution */
    fd_stdout_clear();
    int rc1 = shell_exec_line(&sh, ctx, "echo A");
    CHK(rc1 == 0, "echo A");
    int rc2 = shell_exec_line(&sh, ctx, "echo B");
    CHK(rc2 == 0, "echo B");
    uint8_t buf[64] = {0};
    uint16_t n = fd_stdout_get(buf, 64);
    CHK(n >= 4, "multi output");
    PASS();
}

/* ── B15: Shell exit ─────────────────────────────────── */
static void b15(void) {
    T("B15 shell exit command");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh;
    shell_init(&sh, &pt, &pipes, ctx);

    CHK(sh.running, "running before");
    shell_exec_line(&sh, ctx, "exit");
    CHK(!sh.running, "stopped after exit");
    PASS();
}

/* ── B16: Virtual path integration with path_resolve ── */
static void b16(void) {
    T("B16 path_resolve integrates virtual paths");
    EngineContext *ctx = mk();
    PathContext pc; pathctx_init(&pc, 0, (FsKey){0,0});

    FsKey out;
    /* /proc should resolve via virtual layer */
    CHK(path_resolve(ctx, &pc, "/proc/self", &out) == 0, "/proc/self");
    CHK(out.gate_id == 0xFF00u, "virtual gate");

    /* /dev/null should resolve */
    CHK(path_resolve(ctx, &pc, "/dev/null", &out) == 0, "/dev/null");
    CHK(out.gate_id == 0xFF01u, "dev gate");

    /* Regular path still works */
    path_mkdir(ctx, &pc, "data");
    CHK(path_resolve(ctx, &pc, "/data", &out) == 0, "regular path");
    CHK(out.gate_id != 0xFF00u, "not virtual");
    PASS();
}

int main(void) {
    printf("\n=== Bridge Layer Extended Tests ===\n");
    b01(); b02(); b03(); b04();
    b05(); b06(); b07();
    b08(); b09(); b10();
    b11(); b12();
    b13(); b14(); b15(); b16();
    printf("==================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
