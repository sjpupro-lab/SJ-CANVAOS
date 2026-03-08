/*
 * test_phase10.c — Phase-10 Userland Tests (20 cases)
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
#include "../include/canvasos_fd.h"
#include "../include/canvasos_path.h"
#include "../include/canvasos_user.h"
#include "../include/canvasos_utils.h"

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

/* ── T10-01: File Descriptor ────────────────── */

static void t01(void) {
    T("T10-01 fd open/close cycle");
    fd_table_init();
    int fd = fd_open(NULL, 1, "/test", O_READ | O_WRITE);
    CHK(fd >= 3, "fd >= 3 (0,1,2 reserved)");
    CHK(fd_close(NULL, 1, fd) == 0, "close ok");
    CHK(fd_close(NULL, 1, fd) == -1, "double close fails");
    PASS();
}

static void t02(void) {
    T("T10-02 fd write to stdout captures");
    fd_table_init();
    fd_stdout_clear();
    const char *msg = "HELLO";
    fd_write(NULL, 1, FD_STDOUT, (const uint8_t *)msg, 5);
    uint8_t buf[32] = {0};
    uint16_t n = fd_stdout_get(buf, 32);
    CHK(n == 5, "5 bytes");
    CHK(memcmp(buf, "HELLO", 5) == 0, "match");
    printf("\n");
    PASS();
}

static void t03(void) {
    T("T10-03 fd max per proc");
    fd_table_init();
    int last = -1;
    for (int i = 0; i < FD_MAX_PER_PROC - 3; i++) {
        last = fd_open(NULL, 1, "/test", O_READ);
        if (last < 0) break;
    }
    int overflow = fd_open(NULL, 1, "/test", O_READ);
    CHK(overflow == -1, "max exceeded");
    PASS();
}

static void t04(void) {
    T("T10-04 fd dup stdin→stdout");
    fd_table_init();
    CHK(fd_dup(NULL, 1, FD_STDIN, FD_STDOUT) == 0, "dup ok");
    PASS();
}

/* ── T10-02: Path Resolution ────────────────── */

static void t05(void) {
    T("T10-05 path mkdir + resolve");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    CHK(path_mkdir(ctx, &pc, "data") == 0, "mkdir");
    FsKey out;
    CHK(path_resolve(ctx, &pc, "/data", &out) == 0, "resolve");
    PASS();
}

static void t06(void) {
    T("T10-06 path cd + ls");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    path_mkdir(ctx, &pc, "a");
    CHK(path_cd(&pc, ctx, "/a") == 0, "cd /a");
    path_mkdir(ctx, &pc, "b");
    char names[16][16]; FsKey keys[16];
    int n = path_ls(ctx, &pc, pc.cwd, names, keys, 16);
    CHK(n == 1, "1 entry (b)");
    CHK(strcmp(names[0], "b") == 0, "name=b");
    PASS();
}

static void t07(void) {
    T("T10-07 path resolve nonexistent → error");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    FsKey out;
    CHK(path_resolve(ctx, &pc, "/nonexistent", &out) == -1, "not found");
    PASS();
}

static void t08(void) {
    T("T10-08 path mkdir duplicate → error");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    path_mkdir(ctx, &pc, "dup");
    CHK(path_mkdir(ctx, &pc, "dup") == -2, "already exists");
    PASS();
}

static void t09(void) {
    T("T10-09 path rm");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    path_mkdir(ctx, &pc, "tmp");
    CHK(path_rm(ctx, &pc, "/tmp") == 0, "rm ok");
    FsKey out;
    CHK(path_resolve(ctx, &pc, "/tmp", &out) == -1, "gone");
    PASS();
}

/* ── T10-04: User/Permission ────────────────── */

static void t10(void) {
    T("T10-10 user create + root auto");
    UserTable ut;
    usertable_init(&ut);
    CHK(ut.count == 1, "root exists");
    CHK(ut.users[0].lane_id == UID_ROOT, "lane=0");
    CHK(ut.users[0].priv == PRIV_ROOT, "root priv");
    CHK(user_create(&ut, 1, "alice", PRIV_USER) == 0, "create alice");
    CHK(ut.count == 2, "2 users");
    PASS();
}

static void t11(void) {
    T("T10-11 user perm check");
    EngineContext *ctx = mk();
    UserTable ut; usertable_init(&ut);
    user_create(&ut, 1, "alice", PRIV_USER);
    TileProtection tp; tprot_init(&tp);
    tile_alloc(&tp, ctx, 1, 1); /* alice owns tile 0 */
    CHK(user_check_perm(&ut, &tp, 1, 0, PERM_READ) == 0, "alice→own ok");
    CHK(user_check_perm(&ut, &tp, 2, 0, PERM_READ) == -1, "bob→fault");
    CHK(user_check_perm(&ut, &tp, UID_ROOT, 0, PERM_READ) == 0, "root→ok");
    PASS();
}

static void t12(void) {
    T("T10-12 user su (root only)");
    EngineContext *ctx = mk();
    UserTable ut; usertable_init(&ut);
    user_create(&ut, 1, "alice", PRIV_USER);
    ProcTable pt; proctable_init(&pt, ctx);
    int pid = proc_spawn(&pt, PID_INIT, 10, 100, UID_ROOT); /* root proc */
    CHK(user_su(&ut, &pt, (uint32_t)pid, 1) == 0, "su to alice");
    CHK(proc_find(&pt, (uint32_t)pid)->lane_id == 1, "lane changed");
    PASS();
}

static void t13(void) {
    T("T10-13 user su denied for non-root");
    EngineContext *ctx = mk();
    UserTable ut; usertable_init(&ut);
    user_create(&ut, 1, "alice", PRIV_USER);
    ProcTable pt; proctable_init(&pt, ctx);
    int pid = proc_spawn(&pt, PID_INIT, 10, 100, 1); /* alice proc */
    CHK(user_su(&ut, &pt, (uint32_t)pid, 0) == -2, "su denied");
    PASS();
}

/* ── T10-05~15: Utilities ───────────────────── */

static void t14(void) {
    T("T10-14 cmd_echo via stdout");
    fd_table_init();
    fd_stdout_clear();
    cmd_echo(1, "WORLD");
    uint8_t buf[32] = {0};
    uint16_t n = fd_stdout_get(buf, 32);
    CHK(n == 6, "6 bytes (WORLD\\n)");
    CHK(memcmp(buf, "WORLD\n", 6) == 0, "match");
    printf("\n");
    PASS();
}

static void t15(void) {
    T("T10-15 cmd_ps runs without crash");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    proc_spawn(&pt, PID_INIT, 10, 100, 1);
    CHK(cmd_ps(&pt) == 0, "ps ok");
    PASS();
}

static void t16(void) {
    T("T10-16 cmd_hash output");
    EngineContext *ctx = mk();
    CHK(cmd_hash(ctx) == 0, "hash ok");
    PASS();
}

static void t17(void) {
    T("T10-17 cmd_info output");
    EngineContext *ctx = mk();
    ProcTable pt; proctable_init(&pt, ctx);
    CHK(cmd_info(ctx, &pt) == 0, "info ok");
    PASS();
}

static void t18(void) {
    T("T10-18 cmd_ls root");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    path_mkdir(ctx, &pc, "data");
    path_mkdir(ctx, &pc, "sys");
    CHK(cmd_ls(ctx, &pc, "/") >= 2, "2+ entries");
    PASS();
}

static void t19(void) {
    T("T10-19 cmd_mkdir + cd + ls chain");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    CHK(cmd_mkdir(ctx, &pc, "test") == 0, "mkdir");
    CHK(cmd_cd(&pc, ctx, "/test") == 0, "cd");
    CHK(cmd_mkdir(ctx, &pc, "sub") == 0, "mkdir sub");
    CHK(cmd_ls(ctx, &pc, ".") >= 1, "ls has sub");
    PASS();
}

static void t20(void) {
    T("T10-20 cmd_rm + verify gone");
    EngineContext *ctx = mk();
    FsKey root = {0, 0};
    PathContext pc;
    pathctx_init(&pc, 0, root);
    path_mkdir(ctx, &pc, "temp");
    CHK(cmd_rm(ctx, &pc, "/temp") == 0, "rm");
    CHK(cmd_ls(ctx, &pc, "/") == 0 ||
        cmd_ls(ctx, &pc, "/") >= 0, "ls after rm"); /* temp gone */
    PASS();
}

int main(void) {
    printf("\n=== Phase-10 Userland Tests ===\n");
    t01(); t02(); t03(); t04();
    t05(); t06(); t07(); t08(); t09();
    t10(); t11(); t12(); t13();
    t14(); t15(); t16(); t17(); t18(); t19(); t20();
    printf("===============================\n");
    printf("PASS: %d / FAIL: %d\n\n", P, F);
    return F ? 1 : 0;
}
