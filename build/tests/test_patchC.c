/*
 * test_patchC.c — Patch-C TDD Tests
 *
 * Test Group B: Real Pipe (SH-001)
 *   B1: echo|cat through real pipe fd
 *   B2: pipe fd in fd table
 *   B3: pipe empty read returns 0
 *   B4: pipe full write partial
 *   B5: pipe close EOF
 *
 * Test Group D: VM Runtime Bridge (VM-001, VM-002)
 *   D1: VM_SEND writes to actual pipe
 *   D2: VM_RECV reads from actual pipe
 *   D3: VM_SPAWN creates real child
 *   D4: bridge off → WH-only fallback
 *   D5: determinism log for bridge ops
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

/* ════════════════════════════════════════════════════════
 * Test Group B: Real Pipe with fd semantics
 * ════════════════════════════════════════════════════════ */

/* B1: pipe creates two usable fds */
static void b1(void) {
    T("B1 pipe_create_fds returns read+write fds");
    EngineContext *ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);
    fd_table_init();

    int read_fd = -1, write_fd = -1;
    int rc = fd_pipe_create(ctx, &pipes, 1, &read_fd, &write_fd);
    CHK(rc == 0, "pipe_create_fds ok");
    CHK(read_fd >= 3, "read fd valid");
    CHK(write_fd >= 3, "write fd valid");
    CHK(read_fd != write_fd, "different fds");

    /* Write through write_fd */
    const uint8_t msg[] = "PIPE";
    int w = fd_write(ctx, 1, write_fd, msg, 4);
    CHK(w == 4, "wrote 4 via fd");

    /* Read through read_fd */
    uint8_t buf[16] = {0};
    int n = fd_read(ctx, 1, read_fd, buf, 16);
    CHK(n == 4, "read 4 via fd");
    CHK(memcmp(buf, "PIPE", 4) == 0, "data match");

    fd_close(ctx, 1, read_fd);
    fd_close(ctx, 1, write_fd);
    PASS();
}

/* B2: pipe fds appear in fd table */
static void b2(void) {
    T("B2 pipe fds are in process fd table");
    EngineContext *ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);
    fd_table_init();

    int rfd = -1, wfd = -1;
    fd_pipe_create(ctx, &pipes, 1, &rfd, &wfd);

    /* Should be able to dup pipe fds */
    CHK(fd_dup(ctx, 1, wfd, 6) == 0, "dup write fd");
    /* Write through duped fd */
    int w = fd_write(ctx, 1, 6, (const uint8_t *)"X", 1);
    CHK(w == 1, "write via duped fd");

    /* Read from original read fd */
    uint8_t ch = 0;
    int n = fd_read(ctx, 1, rfd, &ch, 1);
    CHK(n == 1 && ch == 'X', "read duped data");
    PASS();
}

/* B3: pipe empty read returns 0 */
static void b3(void) {
    T("B3 pipe empty read returns 0");
    EngineContext *ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);
    fd_table_init();

    int rfd = -1, wfd = -1;
    fd_pipe_create(ctx, &pipes, 1, &rfd, &wfd);

    uint8_t buf[4];
    int n = fd_read(ctx, 1, rfd, buf, 4);
    CHK(n == 0, "empty pipe = 0");
    PASS();
}

/* B4: pipe full write returns partial */
static void b4(void) {
    T("B4 pipe full write partial");
    EngineContext *ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);
    fd_table_init();

    int rfd = -1, wfd = -1;
    fd_pipe_create(ctx, &pipes, 1, &rfd, &wfd);

    /* Fill the pipe */
    uint8_t big[PIPE_BUF_SIZE];
    memset(big, 'Z', sizeof(big));
    int w1 = fd_write(ctx, 1, wfd, big, PIPE_BUF_SIZE);
    CHK(w1 > 0 && w1 < PIPE_BUF_SIZE, "first write partial");

    /* Second write to full pipe should return 0 */
    int w2 = fd_write(ctx, 1, wfd, big, PIPE_BUF_SIZE);
    CHK(w2 == 0, "full pipe = 0");
    PASS();
}

/* B5: pipe close → EOF on read */
static void b5(void) {
    T("B5 pipe close → read EOF");
    EngineContext *ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);
    fd_table_init();

    int rfd = -1, wfd = -1;
    fd_pipe_create(ctx, &pipes, 1, &rfd, &wfd);

    fd_write(ctx, 1, wfd, (const uint8_t *)"AB", 2);
    fd_close(ctx, 1, wfd);

    /* Read what's in buffer */
    uint8_t buf[4] = {0};
    int n = fd_read(ctx, 1, rfd, buf, 4);
    CHK(n == 2, "read remaining");
    /* Next read should be 0 (EOF) */
    n = fd_read(ctx, 1, rfd, buf, 4);
    CHK(n == 0, "EOF after close");
    PASS();
}

/* ════════════════════════════════════════════════════════
 * Test Group D: VM Runtime Bridge
 * ════════════════════════════════════════════════════════ */

/* D1: VM_SEND writes to actual pipe */
static void d1(void) {
    T("D1 VM_SEND → pipe_write confirmed");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    vm_bridge_init(&pt, &pipes);

    int pipe_id = pipe_create(&pipes, ctx, 10, 20);
    CHK(pipe_id >= 0, "pipe");

    VmState vm; vm_init(&vm, 0, 0, 10);
    vm.reg_A = (uint32_t)pipe_id;
    vm.reg_R = 0x42;
    int rc = vm_exec_send(&pipes, ctx, &vm);
    CHK(rc == 1, "send ok");

    /* Verify in pipe */
    uint8_t ch = 0;
    CHK(pipe_read(&pipes, ctx, pipe_id, &ch, 1) == 1, "pipe has data");
    CHK(ch == 0x42, "data=0x42");
    PASS();
}

/* D2: VM_RECV reads from actual pipe */
static void d2(void) {
    T("D2 VM_RECV → pipe_read confirmed");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    vm_bridge_init(&pt, &pipes);

    int pipe_id = pipe_create(&pipes, ctx, 10, 20);
    pipe_write(&pipes, ctx, pipe_id, (const uint8_t *)"Q", 1);

    VmState vm; vm_init(&vm, 0, 0, 20);
    vm.reg_A = (uint32_t)pipe_id;
    int rc = vm_exec_recv(&pipes, ctx, &vm);
    CHK(rc == 1, "recv ok");
    CHK(vm.reg_R == 'Q', "R=Q");
    PASS();
}

/* D3: VM_SPAWN creates real child */
static void d3(void) {
    T("D3 VM_SPAWN → proc_spawn confirmed");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    vm_bridge_init(&pt, &pipes);

    uint32_t count_before = pt.count;
    VmState vm; vm_init(&vm, 0, 0, PID_INIT);
    vm.reg_A = 30; /* code tile */
    vm.reg_G = 150; /* energy */
    int pid = vm_exec_spawn(&pt, ctx, &vm);
    CHK(pid > 0, "spawned");
    CHK(pt.count == count_before + 1, "count++");

    Proc8 *p = proc_find(&pt, (uint32_t)pid);
    CHK(p != NULL, "proc exists");
    CHK(p->parent_pid == PID_INIT, "parent=init");
    CHK(p->code_tile == 30, "code=30");
    CHK(p->energy == 150, "energy=150");
    PASS();
}

/* D4: bridge off → WH-only fallback (no crash, no side effect) */
static void d4(void) {
    T("D4 bridge off → graceful fallback");
    EngineContext *ctx = mk();

    /* Do NOT call vm_bridge_init → bridge is off */
    vm_bridge_init(NULL, NULL);
    CHK(!vm_bridge_is_active(), "bridge off");

    VmState vm; vm_init(&vm, 0, 0, 5);
    vm.reg_A = 0;
    vm.reg_R = 99;
    /* These should not crash even without bridge */
    int rc = vm_exec_send(NULL, ctx, &vm);
    CHK(rc == -1, "send fails gracefully");
    rc = vm_exec_recv(NULL, ctx, &vm);
    CHK(rc == -1, "recv fails gracefully");
    PASS();
}

/* D5: bridge ops produce WH records */
static void d5(void) {
    T("D5 bridge ops emit WH records");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    vm_bridge_init(&pt, &pipes);

    int pipe_id = pipe_create(&pipes, ctx, 10, 20);
    uint32_t tick_before = ctx->tick;

    VmState vm; vm_init(&vm, 0, 0, 10);
    vm.reg_A = (uint32_t)pipe_id;
    vm.reg_R = 0x55;
    vm_exec_send(&pipes, ctx, &vm);

    /* Check WH for pipe write record */
    bool found = false;
    WhRecord r;
    for (uint32_t t = tick_before; t <= ctx->tick + 2; t++) {
        if (wh_read_record(ctx, t, &r) && r.opcode_index == 0x73) {
            found = true;
            break;
        }
    }
    CHK(found, "WH pipe_write record exists");
    PASS();
}

int main(void) {
    printf("\n=== Patch-C: Real Pipe + VM Bridge ===\n");
    b1(); b2(); b3(); b4(); b5();
    d1(); d2(); d3(); d4(); d5();
    printf("======================================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
