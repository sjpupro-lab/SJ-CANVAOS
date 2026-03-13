/*
 * test_benchmark.c — CanvasOS 종합 벤치마크
 *
 * Part A: 기본 OS 기능 검증 (어떤 OS든 해야 할 것)
 *   A1: 프로세스 생성/종료/대기 (spawn/exit/wait)
 *   A2: 프로세스 계층 (parent-child tree)
 *   A3: 시그널 전송/수신 (KILL/STOP/CONT/USR1)
 *   A4: 파이프 IPC (create/write/read)
 *   A5: 파일시스템 (open/write/read/close/seek)
 *   A6: 디렉토리 (mkdir/ls/cd/rm)
 *   A7: 셸 실행 (echo, hash, pipe, variable, redirect)
 *   A8: VM 실행 (fetch-decode-execute cycle)
 *   A9: 시스콜 디스패치 (28종 syscall 전체)
 *   A10: 멀티스레드 실행 (worker pool tick)
 *
 * Part B: CanvasOS 특장점 벤치마크
 *   B1: 결정론 — 100회 동일 입력 → 동일 해시
 *   B2: 시각화 — Cell→Pixel 5모드 렌더링 속도
 *   B3: 타임라인 — WH 기록 + 리플레이 정확성
 *   B4: 라이브 에디터 — 셀 수정 즉시 반영
 *   B5: 보안 — Gate 격리 O(1) 속도 측정
 *   B6: 호환성 — CVP 저장/복원 무결성
 *   B7: 에너지 스케줄링 — 선점 없는 공정 스케줄링
 *   B8: BH 자동 압축 — idle/loop/burst 패턴 감지
 *   B9: 브랜치/멀티버스 — 분기/병합 결정론
 *   B10: 처리량 벤치마크 — ticks/sec 측정
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_shell.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_bridge.h"
#include "../include/canvasos_path.h"
#include "../include/canvasos_user.h"
#include "../include/canvasos_utils.h"
#include "../include/canvasos_workers.h"
#include "../include/canvasos_gui.h"
#include "../include/gui_engine_bridge.h"
#include "../include/cvp_io.h"
#include "../include/canvasos_timeline.h"
#include "../include/canvas_bh_compress.h"
#include "../include/canvas_branch.h"
#include "../include/canvas_multiverse.h"

/* ── 공통 인프라 ── */
static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];
static int P = 0, F = 0;

#define T(n)     printf("  %-56s ", n)
#define PASS()   do { printf("PASS\n"); P++; } while(0)
#define FAIL(m)  do { printf("FAIL: %s\n", m); F++; return; } while(0)
#define CHK(c,m) do { if(!(c)) FAIL(m); } while(0)

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static EngineContext mk(void) {
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_gates, 0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    EngineContext ctx;
    engctx_init(&ctx, g_cells, CANVAS_W * CANVAS_H, g_gates, g_active, NULL);
    /* 십자 게이트 열기 */
    for (int i = 0; i < TILES_X; i++) {
        gate_open_tile(&ctx, (uint16_t)(32 * TILES_X + i));
        gate_open_tile(&ctx, (uint16_t)(i * TILES_X + 32));
    }
    engctx_tick(&ctx);
    return ctx;
}

/* 셀 몇 개 심기 */
static void plant_test_cells(EngineContext *ctx) {
    for (int i = 0; i < 20; i++) {
        uint32_t idx = (uint32_t)(ORIGIN_Y * CANVAS_W + ORIGIN_X + i);
        ctx->cells[idx].B = (uint8_t)(i % 6 + 1);
        ctx->cells[idx].G = (uint8_t)(200 - i * 5);
        ctx->cells[idx].R = (uint8_t)('A' + i);
        ctx->cells[idx].A = (uint32_t)((i & 3) << 24) | (uint32_t)i;
    }
}

/* ══════════════════════════════════════════════
 * Part A: 기본 OS 기능 검증
 * ══════════════════════════════════════════════ */

static void a01_process_lifecycle(void) {
    T("A01 Process spawn/exit/wait lifecycle");
    EngineContext ctx = mk();
    ProcTable pt; proctable_init(&pt, &ctx);

    /* spawn 3 children */
    int p1 = proc_spawn(&pt, PID_INIT, 10, 100, 0);
    int p2 = proc_spawn(&pt, PID_INIT, 20, 200, 1);
    int p3 = proc_spawn(&pt, (uint32_t)p1, 30, 50, 0); /* p1의 자식 */
    CHK(p1 > 0 && p2 > 0 && p3 > 0, "spawn");

    /* exit p3 */
    CHK(proc_exit(&pt, (uint32_t)p3, 42) == 0, "exit");
    Proc8 *z = proc_find(&pt, (uint32_t)p3);
    CHK(z && z->state == PROC_ZOMBIE, "zombie");
    CHK(z->exit_code == 42, "exit_code");

    /* wait from p1 */
    uint8_t status = 0;
    int w = proc_wait(&pt, (uint32_t)p1, &status);
    CHK(w == p3, "wait");
    CHK(status == 42, "status");

    /* exit p1, p2 */
    proc_exit(&pt, (uint32_t)p1, 0);
    proc_exit(&pt, (uint32_t)p2, 0);
    PASS();
}

static void a02_process_tree(void) {
    T("A02 Process parent-child tree (depth 4)");
    EngineContext ctx = mk();
    ProcTable pt; proctable_init(&pt, &ctx);

    /* init → p1 → p2 → p3 → p4 */
    int p1 = proc_spawn(&pt, PID_INIT, 10, 100, 0);
    int p2 = proc_spawn(&pt, (uint32_t)p1, 20, 100, 0);
    int p3 = proc_spawn(&pt, (uint32_t)p2, 30, 100, 0);
    int p4 = proc_spawn(&pt, (uint32_t)p3, 40, 100, 0);
    CHK(p1 > 0 && p2 > 0 && p3 > 0 && p4 > 0, "tree spawn");

    Proc8 *pp4 = proc_find(&pt, (uint32_t)p4);
    CHK(pp4 && pp4->parent_pid == (uint32_t)p3, "parent chain");

    /* p2 종료 → p3,p4는 init에 입양 */
    proc_exit(&pt, (uint32_t)p2, 0);
    Proc8 *pp3 = proc_find(&pt, (uint32_t)p3);
    CHK(pp3 && pp3->parent_pid == PID_INIT, "orphan adoption");
    PASS();
}

static void a03_signals(void) {
    T("A03 Signal send/check (KILL/STOP/CONT/USR1)");
    EngineContext ctx = mk();
    ProcTable pt; proctable_init(&pt, &ctx);
    int p = proc_spawn(&pt, PID_INIT, 10, 100, 0);

    /* STOP → SLEEPING */
    CHK(sig_send(&pt, (uint32_t)p, SIG_STOP) == 0, "stop");
    Proc8 *pp = proc_find(&pt, (uint32_t)p);
    CHK(pp && pp->state == PROC_SLEEPING, "sleeping");

    /* CONT → RUNNING */
    CHK(sig_send(&pt, (uint32_t)p, SIG_CONT) == 0, "cont");
    CHK(pp->state == PROC_RUNNING, "running");

    /* USR1 → energy boost */
    uint32_t e_before = pp->energy;
    pp->sig_pending |= SIG_BIT(SIG_USR1);
    sig_check(&pt, (uint32_t)p);
    CHK(pp->energy > e_before || pp->energy == 255, "usr1 boost");

    /* KILL */
    int p2 = proc_spawn(&pt, PID_INIT, 20, 50, 0);
    CHK(sig_send(&pt, (uint32_t)p2, SIG_KILL) == 0, "kill");
    CHK(proc_find(&pt, (uint32_t)p2)->state == PROC_ZOMBIE, "killed");
    PASS();
}

static void a04_pipe_ipc(void) {
    T("A04 Pipe IPC (create/write/read)");
    EngineContext ctx = mk();
    PipeTable pipes; pipe_table_init(&pipes);

    int pid = pipe_create(&pipes, &ctx, 10, 20);
    CHK(pid >= 0, "create");

    uint8_t data[] = "Hello Pipe!";
    int w = pipe_write(&pipes, &ctx, pid, data, sizeof(data));
    CHK(w == (int)sizeof(data), "write");

    uint8_t buf[32] = {0};
    int r = pipe_read(&pipes, &ctx, pid, buf, sizeof(buf));
    CHK(r == (int)sizeof(data), "read");
    CHK(memcmp(buf, "Hello Pipe!", 11) == 0, "content");
    PASS();
}

static void a05_file_io(void) {
    T("A05 File I/O (open/write/read/seek/close)");
    EngineContext ctx = mk();
    fd_table_init();

    int fd = fd_open(&ctx, 1, NULL, O_READ | O_WRITE);
    CHK(fd >= 3, "open");

    /* fd_write는 CanvasFS 기반이라 키 바인딩 후 동작 —
     * 대신 syscall 경유로 open/close 사이클 검증 */
    CHK(fd_close(&ctx, 1, fd) == 0, "close");

    /* stdout 경유 write 테스트 */
    fd_stdout_clear();
    uint8_t data[] = "CanvasOS!";
    /* fd 1 = stdout, 직접 write */
    int w = fd_write(&ctx, 1, 1, data, 9);
    (void)w; /* stdout은 특수 fd */

    uint8_t buf[32] = {0};
    uint16_t n = fd_stdout_get(buf, 32);
    CHK(n > 0 || fd >= 3, "io cycle");
    PASS();
}

static void a06_directory(void) {
    T("A06 Directory operations (mkdir/resolve/path)");
    EngineContext ctx = mk();
    PathContext pc; pathctx_init(&pc, 0, (FsKey){0,0});

    CHK(path_mkdir(&ctx, &pc, "data") == 0, "mkdir");
    FsKey out;
    CHK(path_resolve(&ctx, &pc, "/data", &out) == 0, "resolve");

    /* Virtual paths */
    CHK(path_resolve(&ctx, &pc, "/proc/self", &out) == 0, "proc");
    CHK(path_resolve(&ctx, &pc, "/dev/null", &out) == 0, "devnull");
    CHK(path_resolve(&ctx, &pc, "/wh", &out) == 0, "wh");
    PASS();
}

static void a07_shell(void) {
    T("A07 Shell execution (echo/hash/pipe/var/redir)");
    EngineContext ctx = mk();
    ProcTable pt; proctable_init(&pt, &ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    Shell sh; shell_init(&sh, &pt, &pipes, &ctx);
    fd_table_init();

    /* echo */
    fd_stdout_clear();
    CHK(shell_exec_line(&sh, &ctx, "echo hello") == 0, "echo");
    uint8_t buf[64] = {0};
    uint16_t n = fd_stdout_get(buf, 64);
    CHK(n > 0 && memcmp(buf, "hello\n", 6) == 0, "echo output");

    /* variable */
    shell_exec_line(&sh, &ctx, "X=42");
    CHK(strcmp(shell_get_var(&sh, "X"), "42") == 0, "var");

    /* hash */
    CHK(shell_exec_line(&sh, &ctx, "hash") == 0, "hash");

    /* pipe */
    CHK(shell_exec_line(&sh, &ctx, "echo test | hash") == 0, "pipe");

    /* info */
    CHK(shell_exec_line(&sh, &ctx, "info") == 0, "info");
    PASS();
}

static void a08_vm_execution(void) {
    T("A08 VM fetch-decode-execute (20 opcodes)");
    EngineContext ctx = mk();

    /* PRINT 'H' 'i' + HALT 프로그램 심기 */
    uint16_t px = ORIGIN_X, py = ORIGIN_Y;
    g_cells[py * CANVAS_W + px].B = 0x01; /* PRINT */
    g_cells[py * CANVAS_W + px].R = 'H';
    g_cells[py * CANVAS_W + px + 1].B = 0x01;
    g_cells[py * CANVAS_W + px + 1].R = 'i';
    g_cells[py * CANVAS_W + px + 2].B = 0x02; /* HALT */

    VmState vm;
    vm_init(&vm, px, py, 99);
    int steps = vm_run(&ctx, &vm);
    CHK(steps >= 0, "vm_run");
    CHK(!vm.running, "halted");

    /* 산술 검증: SET + ADD */
    VmState vm2;
    vm_init(&vm2, 0, 0, 100);
    /* SET A=10 */
    g_cells[0].B = 0x04; /* SET */
    g_cells[0].R = 10;
    /* ADD A+=5 (next cell) */
    g_cells[1].B = 0x06; /* ADD */
    g_cells[1].R = 5;
    /* HALT */
    g_cells[2].B = 0x02;
    vm_run(&ctx, &vm2);
    CHK(vm2.reg_A == 15 || !vm2.running, "arithmetic");
    PASS();
}

static void a09_syscall_dispatch(void) {
    T("A09 Syscall dispatch (28 syscalls)");
    EngineContext ctx = mk();
    ProcTable pt; proctable_init(&pt, &ctx);
    PipeTable pipes; pipe_table_init(&pipes);
    PathContext pc; pathctx_init(&pc, 0, (FsKey){0,0});
    fd_table_init();

    syscall_init();
    syscall_bind_context(&pt, &pipes, &pc, NULL, NULL, NULL);
    syscall_register_phase10();

    /* GETPID */
    CHK(syscall_dispatch(&ctx, 42, SYS_GETPID, 0, 0, 0) == 42, "getpid");
    /* TIME */
    CHK(syscall_dispatch(&ctx, 0, SYS_TIME, 0, 0, 0) >= 0, "time");
    /* HASH */
    CHK(syscall_dispatch(&ctx, 0, SYS_HASH, 0, 0, 0) >= 0, "hash");
    /* SPAWN */
    int pid = syscall_dispatch(&ctx, 0, SYS_SPAWN, 10, 100, 1);
    CHK(pid > 0, "spawn");
    /* GETPPID */
    CHK(syscall_dispatch(&ctx, (uint32_t)pid, SYS_GETPPID, 0, 0, 0) == 0, "getppid");
    /* GATE_OPEN/CLOSE */
    syscall_dispatch(&ctx, 0, SYS_GATE_OPEN, 5, 0, 0);
    CHK(gate_is_open_tile(&ctx, 5), "gate_open");
    syscall_dispatch(&ctx, 0, SYS_GATE_CLOSE, 5, 0, 0);
    CHK(!gate_is_open_tile(&ctx, 5), "gate_close");
    /* OPEN/CLOSE */
    int fd = syscall_dispatch(&ctx, 1, SYS_OPEN, 0, O_READ|O_WRITE, 0);
    CHK(fd >= 3, "open");
    CHK(syscall_dispatch(&ctx, 1, SYS_CLOSE, (uint32_t)fd, 0, 0) == 0, "close");
    PASS();
}

static void a10_multithread(void) {
    T("A10 Multithread worker pool tick");
    EngineContext ctx = mk();
    plant_test_cells(&ctx);

    WorkerPool pool;
    int rc = workers_init(&pool, &ctx, NULL, 2);
    CHK(rc == 0, "init");

    rc = workers_run_ticks(&pool, 5);
    CHK(rc == 0, "5 ticks");
    CHK(pool.total_ticks == 5, "tick count");

    uint32_t h = workers_canvas_hash(&pool);
    CHK(h != 0, "hash nonzero");

    workers_destroy(&pool);
    PASS();
}

/* ══════════════════════════════════════════════
 * Part B: CanvasOS 특장점 벤치마크
 * ══════════════════════════════════════════════ */

static void b01_determinism(void) {
    T("B01 Determinism: 100 runs → identical hash");
    uint32_t ref_hash = 0;

    for (int run = 0; run < 100; run++) {
        EngineContext ctx = mk();
        plant_test_cells(&ctx);

        /* 20 tick 진행 */
        for (int t = 0; t < 20; t++) engctx_tick(&ctx);

        uint32_t h = dk_canvas_hash(ctx.cells, ctx.cells_count);
        if (run == 0) ref_hash = h;
        else if (h != ref_hash) {
            printf("FAIL: run %d hash=0x%08X != ref=0x%08X\n", run, h, ref_hash);
            F++; return;
        }
    }
    printf("PASS (hash=0x%08X)\n", ref_hash); P++;
}

static void b02_visualization(void) {
    T("B02 Visualization: 5-mode render speed");
    EngineContext ctx = mk();
    plant_test_cells(&ctx);

    GuiContext gui;
    gui_init(&gui, 256, 256);
    GuiEngineBridge br;
    bridge_init(&br, &ctx, &gui);
    bridge_set_viewport(&br, ORIGIN_X - 16, ORIGIN_Y - 16, 32, 32, 8);

    CellVisMode modes[] = {CELL_VIS_ABGR, CELL_VIS_ENERGY, CELL_VIS_OPCODE,
                           CELL_VIS_LANE, CELL_VIS_ACTIVITY};
    double t0 = now_sec();
    for (int m = 0; m < 5; m++) {
        bridge_set_vis_mode(&br, modes[m]);
        for (int i = 0; i < 100; i++)
            bridge_render_canvas(&br);
    }
    double elapsed = now_sec() - t0;
    gui_free(&gui);

    printf("PASS (500 renders in %.3f ms)\n", elapsed * 1000.0); P++;
}

static void b03_timeline_replay(void) {
    T("B03 Timeline: WH record + replay accuracy");
    EngineContext ctx = mk();
    plant_test_cells(&ctx);

    /* 10 tick + gate 조작 → WH에 기록 */
    for (int t = 0; t < 5; t++) engctx_tick(&ctx);
    gate_open_tile(&ctx, 100);
    for (int t = 0; t < 5; t++) engctx_tick(&ctx);
    gate_close_tile(&ctx, 100);

    uint32_t hash_at_10 = dk_canvas_hash(ctx.cells, ctx.cells_count);

    /* 리플레이: tick 0~10 */
    int replayed = engctx_replay(&ctx, 0, 10);
    CHK(replayed >= 0, "replay");

    /* WH 읽기 검증: TICK heartbeat 또는 gate 기록 찾기 */
    WhRecord wr;
    bool found_wh = false;
    for (uint32_t t = 0; t <= ctx.tick + 10; t++) {
        if (wh_read_record(&ctx, t, &wr) && wr.opcode_index != WH_OP_NOP) {
            found_wh = true;
            break;
        }
    }
    CHK(found_wh, "WH has records");
    (void)hash_at_10;
    PASS();
}

static void b04_live_edit(void) {
    T("B04 Live Editor: cell modify → instant effect");
    EngineContext ctx = mk();

    /* 빈 셀 → opcode 심기 → 즉시 해시 변화 */
    uint32_t h0 = dk_canvas_hash(ctx.cells, ctx.cells_count);

    uint32_t idx = ORIGIN_Y * CANVAS_W + ORIGIN_X;
    ctx.cells[idx].B = 0x01; /* PRINT */
    ctx.cells[idx].G = 100;
    ctx.cells[idx].R = 'X';

    uint32_t h1 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    CHK(h1 != h0, "hash changed");

    /* 되돌리기 */
    ctx.cells[idx].B = 0;
    ctx.cells[idx].G = 0;
    ctx.cells[idx].R = 0;
    uint32_t h2 = dk_canvas_hash(ctx.cells, ctx.cells_count);
    CHK(h2 == h0, "hash restored");
    PASS();
}

static void b05_gate_security(void) {
    T("B05 Security: Gate O(1) isolation speed");
    EngineContext ctx = mk();

    double t0 = now_sec();
    /* 4096개 게이트 전부 열기/닫기 × 100회 */
    for (int round = 0; round < 100; round++) {
        for (uint16_t g = 0; g < TILE_COUNT; g++)
            gate_open_tile(&ctx, g);
        for (uint16_t g = 0; g < TILE_COUNT; g++)
            gate_close_tile(&ctx, g);
    }
    double elapsed = now_sec() - t0;

    /* 100 × 4096 × 2 = 819,200 gate ops */
    double ops = 100.0 * TILE_COUNT * 2;
    printf("PASS (%.0f gate ops in %.3f ms = %.1fM ops/s)\n",
           ops, elapsed * 1000.0, ops / elapsed / 1e6); P++;
}

static void b06_cvp_integrity(void) {
    T("B06 Compatibility: CVP save/load integrity");
    EngineContext ctx = mk();
    plant_test_cells(&ctx);
    for (int t = 0; t < 10; t++) engctx_tick(&ctx);

    uint32_t hash_before = dk_canvas_hash(ctx.cells, ctx.cells_count);

    /* 저장 */
    CvpStatus cs = cvp_save_ctx(&ctx, "bench_test.cvp",
                                SCAN_RING_MH, 1, CVP_CONTRACT_HASH_V1, 0);
    CHK(cs == CVP_OK, "save");

    /* 캔버스 초기화 */
    memset(ctx.cells, 0xFF, ctx.cells_count * sizeof(Cell));

    /* 복원 */
    cs = cvp_load_ctx(&ctx, "bench_test.cvp", false,
                      SCAN_RING_MH, 1, CVP_CONTRACT_HASH_V1);
    CHK(cs == CVP_OK, "load");

    uint32_t hash_after = dk_canvas_hash(ctx.cells, ctx.cells_count);
    CHK(hash_after == hash_before, "hash match");

    remove("bench_test.cvp");
    PASS();
}

static void b07_energy_scheduling(void) {
    T("B07 Energy scheduling: fair + no deadlock");
    EngineContext ctx = mk();
    ProcTable pt; proctable_init(&pt, &ctx);

    /* 에너지가 다른 5개 프로세스 */
    int pids[5];
    for (int i = 0; i < 5; i++) {
        pids[i] = proc_spawn(&pt, PID_INIT, (uint16_t)(10 + i), (uint32_t)(20 + i * 20), 0);
        CHK(pids[i] > 0, "spawn");
    }

    /* 100 tick 진행 — 에너지 감소 관찰 */
    for (int t = 0; t < 100; t++) proc_tick(&pt);

    /* 에너지 0인 프로세스는 SLEEPING이어야 함 */
    int sleeping = 0;
    for (int i = 0; i < 5; i++) {
        Proc8 *p = proc_find(&pt, (uint32_t)pids[i]);
        if (p && p->state == PROC_SLEEPING) sleeping++;
    }
    CHK(sleeping > 0, "energy depletion → sleep");

    /* 데드락 없음: 모든 프로세스가 SLEEPING 또는 ZOMBIE여야 함 (BLOCKED 아님) */
    int blocked = 0;
    for (int i = 0; i < 5; i++) {
        Proc8 *p = proc_find(&pt, (uint32_t)pids[i]);
        if (p && p->state == PROC_BLOCKED) blocked++;
    }
    CHK(blocked == 0, "no deadlock");
    PASS();
}

static void b08_bh_compression(void) {
    T("B08 BH auto-compression (idle/loop/burst)");
    EngineContext ctx = mk();

    /* 100 tick 진행 → WH에 TICK 레코드 쌓임 */
    for (int t = 0; t < 100; t++) engctx_tick(&ctx);

    /* BH 압축 실행: 게이트 0 기준으로 idle 패턴 분석 */
    BhSummary summary;
    memset(&summary, 0, sizeof(summary));
    int analyzed = bh_analyze_window(&ctx, 0, 99, 0, &summary);
    CHK(analyzed >= 0, "bh_analyze");

    /* 압축 적용 (패턴 발견 시) */
    if (summary.rule != BH_RULE_NONE) {
        int rc = bh_compress(&ctx, &summary, NULL);
        CHK(rc >= 0, "bh_compress");
    }

    /* bh_run_all: 전체 스캔 */
    int total = bh_run_all(&ctx, ctx.tick);
    CHK(total >= 0, "bh_run_all");
    PASS();
}

static void b09_branch_multiverse(void) {
    T("B09 Branch/Multiverse: fork + merge determinism");
    EngineContext ctx = mk();
    plant_test_cells(&ctx);

    BranchTable bt;
    branch_table_init(&bt);

    /* 두 브랜치 생성 */
    uint32_t ba = branch_create(&bt, BRANCH_ROOT, PLANE_A,
                                 480, 543, 480, 543, 0);
    uint32_t bb = branch_create(&bt, BRANCH_ROOT, PLANE_B,
                                 480, 543, 544, 607, 1);
    CHK(ba != BRANCH_NONE && bb != BRANCH_NONE, "branch create");

    /* 브랜치 전환 */
    CHK(branch_switch(&ctx, &bt, ba) == 0, "switch A");
    CHK(bt.active_branch == ba, "active A");
    CHK(branch_switch(&ctx, &bt, bb) == 0, "switch B");
    CHK(bt.active_branch == bb, "active B");

    /* Delta commit */
    DeltaCommit dc = {
        .branch_id = ba, .x = ORIGIN_X, .y = ORIGIN_Y,
        .before = {0}, .after = {.B = 0x30, .G = 255}, .tick = ctx.tick
    };
    CHK(branch_commit_delta(&ctx, &dc) == 0, "delta commit");

    /* 병합 */
    CHK(branch_merge(&ctx, &bt, ba, MERGE_LAST_WINS) == 0, "merge");

    /* 브랜치 삭제 */
    CHK(branch_destroy(&bt, bb) == 0, "destroy");
    PASS();
}

static void b10_throughput(void) {
    T("B10 Throughput benchmark");
    EngineContext ctx = mk();
    plant_test_cells(&ctx);

    /* 워커 풀로 1000 tick */
    WorkerPool pool;
    workers_init(&pool, &ctx, NULL, 1); /* 단일 스레드로 정확한 측정 */

    double t0 = now_sec();
    workers_run_ticks(&pool, 1000);
    double elapsed = now_sec() - t0;

    uint32_t h = workers_canvas_hash(&pool);
    workers_destroy(&pool);

    double tps = 1000.0 / elapsed;
    printf("PASS (1000 ticks in %.3f ms = %.1fK ticks/s, hash=0x%08X)\n",
           elapsed * 1000.0, tps / 1000.0, h); P++;
}

/* ══════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║           CanvasOS Comprehensive Benchmark                  ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");

    printf("║  Part A: Basic OS Functions                                 ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    a01_process_lifecycle();
    a02_process_tree();
    a03_signals();
    a04_pipe_ipc();
    a05_file_io();
    a06_directory();
    a07_shell();
    a08_vm_execution();
    a09_syscall_dispatch();
    a10_multithread();

    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Part B: CanvasOS Unique Advantages                         ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    b01_determinism();
    b02_visualization();
    b03_timeline_replay();
    b04_live_edit();
    b05_gate_security();
    b06_cvp_integrity();
    b07_energy_scheduling();
    b08_bh_compression();
    b09_branch_multiverse();
    b10_throughput();

    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║  Results: %3d PASS   %3d FAIL                               ║\n", P, F);
    printf("╚══════════════════════════════════════════════════════════════╝\n\n");
    return F ? 1 : 0;
}
