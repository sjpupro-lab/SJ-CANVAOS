/*
 * canvasos_cli.c — SJ CanvasOS Typewriter CLI
 *
 * 사용법: ./canvasos <command> [args...]
 *
 * 모든 명령은 DevDict의 옵코드 1개에 대응한다.
 * 실행하면 WH에 기록되고, 결과가 즉시 출력된다.
 * 상태는 session.cvp에 자동 저장/복구된다.
 *
 * 명령 목록:
 *   gate open  <gate_id>               — WH_GATE_OPEN  (0x10)
 *   gate close <gate_id>               — WH_GATE_CLOSE (0x11)
 *   gate info  <gate_id>               — 게이트 상태 조회
 *   gate list                          — 열린 게이트 목록
 *
 *   wh write   <op> <gate_id>          — WH_WRITE (0x20)
 *   wh log     [n]                     — WH 로그 최근 n개 출력
 *   wh replay  <from> <to>             — ENGCTX_REPLAY (0x51)
 *
 *   bh set     <pid> <energy>          — BH 에너지 설정
 *   bh decay   <pid> <amount>          — BH_DECAY (0x21)
 *   bh status  [pid]                   — BH 에너지 조회
 *
 *   spawn      <volh> <volt> <energy>  — 프로세스 생성
 *   ps                                 — 프로세스 목록
 *
 *   ipc send   <dst_pid> <src_pid> <slot> — WH_IPC_SEND (0x30)
 *
 *   cvp save   [file]                  — CVP_SAVE (0x40)
 *   cvp load   [file]                  — CVP_LOAD (0x41)
 *   cvp validate [file]                — CVP_VALIDATE (0x42)
 *   cvp replay [file] <from> <to>      — CVP_REPLAY (0x43)
 *
 *   tick [n]                           — ENGCTX_TICK (0x50) × n
 *   inspect    <x> <y>                 — ENGCTX_INSPECT (0x52)
 *   scan                               — 캔버스 스캔 (열린 타일 출력)
 *   canvas     <x> <y> <B> <G> <R>    — 셀 직접 쓰기
 *   info                               — 엔진 상태 요약
 *   help                               — 명령 목록
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvasos_gate_ops.h"
#include "canvasos_sched.h"
#include "engine_time.h"
#include "cvp_io.h"

/* ── 전역 상태 ── */
static Cell         g_cells[CANVAS_W * CANVAS_H];
static GateState    g_gates[TILEGATE_COUNT];
static uint8_t      g_active[TILE_COUNT];
static RuleTable    g_rules; /* 현재 미사용 (Phase 4+) */
static EngineContext g_ctx;
static ActiveSet     g_aset;
static Scheduler     g_sched;

#define SESSION_FILE "session.cvp"

/* ── 출력 스타일 ── */
#define COL_RESET  "\033[0m"
#define COL_TICK   "\033[36m"   /* cyan */
#define COL_OK     "\033[32m"   /* green */
#define COL_ERR    "\033[31m"   /* red */
#define COL_GATE   "\033[33m"   /* yellow */
#define COL_WH     "\033[96m"   /* bright cyan */
#define COL_BH     "\033[91m"   /* bright red */
#define COL_DIM    "\033[90m"   /* dark gray */
#define COL_BOLD   "\033[1m"

static void print_tick(void) {
    printf(COL_DIM "[" COL_TICK "tick=%06u" COL_DIM "]" COL_RESET " ",
           (unsigned)g_ctx.tick);
}

static void ok(const char *msg) {
    print_tick();
    printf(COL_OK "OK" COL_RESET "  %s\n", msg);
}

static void err(const char *msg) {
    print_tick();
    printf(COL_ERR "ERR" COL_RESET " %s\n", msg);
}

/* ── 세션 저장/복구 ── */
static void session_save(void) {
    cvp_save_ctx(&g_ctx, SESSION_FILE,
                 (uint32_t)g_aset.mode, 0,
                 CVP_CONTRACT_HASH_V1, 0);
}

static void session_load(void) {
    cvp_load_ctx(&g_ctx, SESSION_FILE, false,
                 CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
}

/* ── 엔진 초기화 ── */
static void engine_init(void) {
    memset(g_cells,  0, sizeof(g_cells));
    memset(g_gates,  GATE_CLOSE, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    memset(&g_rules, 0, sizeof(g_rules));

    engctx_init(&g_ctx, g_cells, CANVAS_W * CANVAS_H,
                 g_gates, g_active, &g_rules);

    /* ActiveSet 초기화 */
    memset(&g_aset, 0, sizeof(g_aset));
    g_aset.mode = SCAN_RING_MH;

    /* Scheduler */
    sched_init(&g_sched, &g_aset);
    sched_bind_ctx(&g_sched, &g_ctx);

    /* 세션 복구 */
    session_load();
}

/* ══════════════════════════════════════════
   COMMANDS
══════════════════════════════════════════ */

/* ── gate open <id> ── */
static int cmd_gate_open(int argc, char **argv) {
    if (argc < 1) { err("gate open <gate_id>"); return 1; }
    uint16_t gid = (uint16_t)atoi(argv[0]);
    if (gid >= TILE_COUNT) { err("gate_id out of range (0..4095)"); return 1; }

    gate_open_tile(&g_ctx, gid);

    /* WH 기록 (WH_GATE_OPEN 0x10) */
    WhRecord r = {
        .tick_or_event = (uint32_t)g_ctx.tick,
        .opcode_index  = WH_OP_GATE_OPEN,
        .target_addr   = gid,
        .target_kind   = WH_TGT_TILE,
    };
    wh_write_record(&g_ctx, (uint64_t)g_ctx.tick, &r);
    engctx_tick(&g_ctx);

    char buf[64];
    snprintf(buf, sizeof(buf), "GATE_OPEN  gate=%u  (tile %u,%u)",
             gid, gid % TILES_X, gid / TILES_X);
    ok(buf);
    session_save();
    return 0;
}

/* ── gate close <id> ── */
static int cmd_gate_close(int argc, char **argv) {
    if (argc < 1) { err("gate close <gate_id>"); return 1; }
    uint16_t gid = (uint16_t)atoi(argv[0]);
    if (gid >= TILE_COUNT) { err("gate_id out of range"); return 1; }

    gate_close_tile(&g_ctx, gid);

    WhRecord r = {
        .tick_or_event = (uint32_t)g_ctx.tick,
        .opcode_index  = WH_OP_GATE_CLOSE,
        .target_addr   = gid,
        .target_kind   = WH_TGT_TILE,
    };
    wh_write_record(&g_ctx, (uint64_t)g_ctx.tick, &r);
    engctx_tick(&g_ctx);

    char buf[64];
    snprintf(buf, sizeof(buf), "GATE_CLOSE gate=%u  (tile %u,%u)",
             gid, gid % TILES_X, gid / TILES_X);
    ok(buf);
    session_save();
    return 0;
}

/* ── gate info <id> ── */
static int cmd_gate_info(int argc, char **argv) {
    if (argc < 1) { err("gate info <gate_id>"); return 1; }
    uint16_t gid = (uint16_t)atoi(argv[0]);
    if (gid >= TILE_COUNT) { err("gate_id out of range"); return 1; }

    int open = gate_is_open_tile(&g_ctx, gid);
    uint16_t tx = gid % TILES_X, ty = gid / TILES_X;
    uint16_t cx = (uint16_t)(tx * TILE), cy = (uint16_t)(ty * TILE);

    print_tick();
    printf(COL_GATE "GATE" COL_RESET
           "  id=%-4u  tile(%2u,%2u)  cell_origin(%4u,%4u)  %s%s%s\n",
           gid, tx, ty, cx, cy,
           open ? COL_OK : COL_DIM,
           open ? "OPEN  " : "CLOSE ",
           COL_RESET);
    return 0;
}

/* ── gate list ── */
static int cmd_gate_list(void) {
    int open_n = 0;
    print_tick();
    printf(COL_GATE "GATE LIST" COL_RESET "\n");
    for (int i = 0; i < TILE_COUNT; i++) {
        if (gate_is_open_tile(&g_ctx, (uint16_t)i)) {
            printf("  " COL_OK "OPEN" COL_RESET "  gate=%-4d  tile(%u,%u)\n",
                   i, i % TILES_X, i / TILES_X);
            open_n++;
        }
    }
    if (open_n == 0)
        printf("  " COL_DIM "(모든 게이트 CLOSE)" COL_RESET "\n");
    else
        printf("  %s%d gates OPEN%s / %d total\n",
               COL_OK, open_n, COL_RESET, TILE_COUNT);
    return 0;
}

/* ── wh write <op_hex> <gate_id> ── */
static int cmd_wh_write(int argc, char **argv) {
    if (argc < 2) { err("wh write <op_hex> <gate_id>"); return 1; }
    uint8_t  op  = (uint8_t)strtoul(argv[0], NULL, 16);
    uint32_t gid = (uint32_t)atoi(argv[1]);

    WhRecord r = {
        .tick_or_event = (uint32_t)g_ctx.tick,
        .opcode_index  = op,
        .target_addr   = gid,
        .target_kind   = WH_TGT_TILE,
    };
    wh_write_record(&g_ctx, (uint64_t)g_ctx.tick, &r);
    engctx_tick(&g_ctx);

    char buf[80];
    snprintf(buf, sizeof(buf),
             "WH_WRITE   op=0x%02X  gate=%u  @tick=%u",
             op, gid, (unsigned)g_ctx.tick - 1);
    ok(buf);
    session_save();
    return 0;
}

/* ── wh log [n] ── */
static int cmd_wh_log(int argc, char **argv) {
    int n = (argc >= 1) ? atoi(argv[0]) : 16;
    if (n <= 0 || n > (int)WH_CAP) n = 16;

    uint32_t cur = (uint32_t)g_ctx.tick;
    uint32_t lo  = (cur > (uint32_t)WH_CAP) ? cur - (uint32_t)WH_CAP : 0;

    print_tick();
    printf(COL_WH "WH LOG" COL_RESET "  (최근 %d개, window=%u..%u)\n",
           n, lo, cur);
    printf(COL_DIM "  %-6s  %-12s  %-6s  %-6s  %-4s\n" COL_RESET,
           "TICK", "OP", "TARGET", "KIND", "FLAGS");

    int printed = 0;
    for (int32_t t = (int32_t)cur - 1; t >= (int32_t)lo && printed < n; t--) {
        WhRecord r;
        wh_read_record(&g_ctx, (uint64_t)(uint32_t)t, &r);
        if (r.opcode_index == 0) continue;

        const char *opname;
        switch ((WhOpcode)r.opcode_index) {
            case WH_OP_TICK:       opname = "TICK";       break;
            case WH_OP_GATE_OPEN:  opname = "GATE_OPEN";  break;
            case WH_OP_GATE_CLOSE: opname = "GATE_CLOSE"; break;
            case WH_OP_DECAY:      opname = "BH_DECAY";   break;
            case WH_OP_SLEEP:      opname = "SLEEP";      break;
            case WH_OP_WAKE:       opname = "WAKE";       break;
            case WH_OP_IPC:        opname = "IPC_SEND";   break;
            default:               opname = "?";          break;
        }

        const char *col = (r.opcode_index == WH_OP_GATE_OPEN)  ? COL_OK :
                          (r.opcode_index == WH_OP_GATE_CLOSE) ? COL_ERR :
                          (r.opcode_index == WH_OP_TICK)       ? COL_DIM : COL_WH;

        printf("  %s%-6u  %-12s%s  %-6u  %-6u  0x%02X\n",
               col,
               (unsigned)t, opname, COL_RESET,
               r.target_addr, r.target_kind, r.flags);
        printed++;
    }
    if (printed == 0)
        printf("  " COL_DIM "(WH 비어있음)" COL_RESET "\n");
    return 0;
}

/* ── wh replay <from> <to> ── */
static int cmd_wh_replay(int argc, char **argv) {
    if (argc < 2) { err("wh replay <from_tick> <to_tick>"); return 1; }
    uint32_t from = (uint32_t)atoi(argv[0]);
    uint32_t to   = (uint32_t)atoi(argv[1]);

    int n = engctx_replay(&g_ctx, from, to);
    if (n < 0) { err("replay failed"); return 1; }

    char buf[80];
    snprintf(buf, sizeof(buf), "REPLAY  from=%u to=%u  replayed=%d records", from, to, n);
    ok(buf);
    session_save();
    return 0;
}

/* ── bh set <pid> <energy> ── */
static int cmd_bh_set(int argc, char **argv) {
    if (argc < 2) { err("bh set <pid> <energy>"); return 1; }
    uint16_t pid = (uint16_t)atoi(argv[0]);
    uint8_t  e   = (uint8_t)atoi(argv[1]);

    bh_set_energy(&g_ctx, pid, e, e);
    engctx_tick(&g_ctx);

    char buf[64];
    snprintf(buf, sizeof(buf), "BH_SET    pid=%u  energy=%u", pid, e);
    ok(buf);
    session_save();
    return 0;
}

/* ── bh decay <pid> <amount> ── */
static int cmd_bh_decay(int argc, char **argv) {
    if (argc < 2) { err("bh decay <pid> <amount>"); return 1; }
    uint16_t pid = (uint16_t)atoi(argv[0]);
    uint8_t  dec = (uint8_t)atoi(argv[1]);

    uint8_t remaining = bh_decay_energy(&g_ctx, pid, dec);

    WhRecord r = {
        .tick_or_event = (uint32_t)g_ctx.tick,
        .opcode_index  = WH_OP_DECAY,
        .target_addr   = pid,
        .target_kind   = WH_TGT_PROC,
        .arg_state     = remaining,
    };
    wh_write_record(&g_ctx, (uint64_t)g_ctx.tick, &r);
    engctx_tick(&g_ctx);

    char buf[80];
    const char *state = (remaining == 0) ? "  " COL_ERR "→ SLEEP" COL_RESET : "";
    snprintf(buf, sizeof(buf), "BH_DECAY  pid=%u  -%u  remaining=%u%s",
             pid, dec, remaining, state);
    ok(buf);
    session_save();
    return 0;
}

/* ── bh status [pid] ── */
static int cmd_bh_status(int argc, char **argv) {
    print_tick();
    printf(COL_BH "BH STATUS" COL_RESET "\n");

    if (argc >= 1) {
        uint16_t pid = (uint16_t)atoi(argv[0]);
        uint8_t e = bh_get_energy(&g_ctx, pid);
        BhAddr a = bh_addr_of_pid(pid);
        printf("  pid=%-4u  energy=%3u  cell(%u,%u)  %s\n",
               pid, e, a.x, a.y,
               e == 0 ? COL_ERR "SLEEP" COL_RESET
                      : COL_OK  "RUN  " COL_RESET);
    } else {
        /* 처음 8개 pid */
        printf(COL_DIM "  %-6s  %-8s  %-10s  %-6s\n" COL_RESET,
               "PID", "ENERGY", "CELL", "STATE");
        for (int i = 0; i < 8; i++) {
            uint16_t pid = (uint16_t)i;
            uint8_t  e   = bh_get_energy(&g_ctx, pid);
            BhAddr   a   = bh_addr_of_pid(pid);
            int bar_len  = (int)((e / 255.0f) * 20);
            char bar[24];
            for (int k = 0; k < 20; k++) bar[k] = (k < bar_len) ? '#' : '-';
            bar[20] = '\0';

            const char *col = e > 80 ? COL_OK : e > 30 ? COL_GATE : COL_ERR;
            printf("  pid=%-4u  %s%3u%s  (%4u,%4u)  %s  %s\n",
                   i, col, e, COL_RESET, a.x, a.y, bar,
                   e == 0 ? COL_ERR "SLEEP" COL_RESET
                          : COL_OK  "RUN  " COL_RESET);
        }
    }
    return 0;
}

/* ── spawn <volh> <volt> <energy> ── */
static int cmd_spawn(int argc, char **argv) {
    if (argc < 3) { err("spawn <volh> <volt> <energy>"); return 1; }
    GateSpace sp = {
        .volh = (uint16_t)atoi(argv[0]),
        .volt = (uint16_t)atoi(argv[1]),
    };
    uint32_t energy = (uint32_t)atoi(argv[2]);

    int pid = sched_spawn(&g_sched, sp, energy, energy);
    if (pid < 0) { err("spawn failed (PROC_MAX reached?)"); return 1; }

    engctx_tick(&g_ctx);

    char buf[80];
    snprintf(buf, sizeof(buf),
             "SPAWN  pid=%d  volh=%u volt=%u  energy=%u",
             pid, sp.volh, sp.volt, energy);
    ok(buf);
    session_save();
    return 0;
}

/* ── ps ── */
static int cmd_ps(void) {
    print_tick();
    printf(COL_BOLD "PROCESS LIST" COL_RESET "  (%d procs)\n", g_sched.count);
    printf(COL_DIM "  %-5s  %-8s  %-5s  %-5s  %-8s  %-6s\n" COL_RESET,
           "PID", "STATE", "VOLH", "VOLT", "ENERGY", "BORN");

    for (uint32_t i = 0; i < g_sched.count; i++) {
        const Process *p = &g_sched.procs[i];
        const char *st  = p->state == PROC_RUNNING  ? COL_OK  "RUN   " COL_RESET :
                          p->state == PROC_SLEEPING ? COL_DIM "SLEEP " COL_RESET :
                          p->state == PROC_BLOCKED  ? COL_ERR "BLOCK " COL_RESET :
                                                      COL_DIM "ZOMBIE" COL_RESET;
        printf("  %-5u  %s  %-5u  %-5u  %3u/%-3u  %-6u\n",
               p->pid, st,
               p->space.volh, p->space.volt,
               p->energy, p->energy_max,
               p->tick_born);
    }
    if (g_sched.count == 0)
        printf("  " COL_DIM "(프로세스 없음)" COL_RESET "\n");
    return 0;
}

/* ── ipc send <dst_pid> <src_pid> <slot> ── */
static int cmd_ipc_send(int argc, char **argv) {
    if (argc < 3) { err("ipc send <dst_pid> <src_pid> <slot>"); return 1; }
    uint32_t dst  = (uint32_t)atoi(argv[0]);
    uint32_t src  = (uint32_t)atoi(argv[1]);
    uint8_t  slot = (uint8_t)atoi(argv[2]);

    /* WH_IPC_SEND: C0.A=dst_pid, C1.A=src_pid, C0.R=slot */
    WhRecord r = {
        .tick_or_event = dst,
        .opcode_index  = WH_OP_IPC,
        .param0        = slot,
        .target_addr   = src,
        .target_kind   = WH_TGT_PROC,
    };
    wh_write_record(&g_ctx, (uint64_t)g_ctx.tick, &r);
    engctx_tick(&g_ctx);

    char buf[80];
    snprintf(buf, sizeof(buf),
             "IPC_SEND  dst=%u  src=%u  slot=%u", dst, src, slot);
    ok(buf);
    session_save();
    return 0;
}

/* ── cvp save [file] ── */
static int cmd_cvp_save(int argc, char **argv) {
    const char *path = (argc >= 1) ? argv[0] : SESSION_FILE;
    int r = (int)cvp_save_ctx(&g_ctx, path,
                 (uint32_t)g_aset.mode, 0,
                 CVP_CONTRACT_HASH_V1, 0);
    if (r != CVP_OK) {
        char buf[80]; snprintf(buf, sizeof(buf), "CVP_SAVE failed: %s", cvp_strerror(r));
        err(buf); return 1;
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "CVP_SAVE  → %s  tick=%u", path, (unsigned)g_ctx.tick);
    ok(buf);
    return 0;
}

/* ── cvp load [file] ── */
static int cmd_cvp_load(int argc, char **argv) {
    const char *path = (argc >= 1) ? argv[0] : SESSION_FILE;
    int r = (int)cvp_load_ctx(&g_ctx, path, false,
                 CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
    if (r != CVP_OK) {
        char buf[80]; snprintf(buf, sizeof(buf), "CVP_LOAD failed: %s", cvp_strerror(r));
        err(buf); return 1;
    }
    char buf[80];
    snprintf(buf, sizeof(buf), "CVP_LOAD  ← %s  tick=%u", path, (unsigned)g_ctx.tick);
    ok(buf);
    return 0;
}

/* ── cvp validate [file] ── */
static int cmd_cvp_validate(int argc, char **argv) {
    const char *path = (argc >= 1) ? argv[0] : SESSION_FILE;
    int r = (int)cvp_validate(path, CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
    if (r != CVP_OK) {
        char buf[80]; snprintf(buf, sizeof(buf), "CVP_VALIDATE failed: %s", cvp_strerror(r));
        err(buf); return 1;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "CVP_VALIDATE OK  %s", path);
    ok(buf);
    return 0;
}

/* ── cvp replay [file] <from> <to> ── */
static int cmd_cvp_replay(int argc, char **argv) {
    if (argc < 2) { err("cvp replay [file] <from_tick> <to_tick>"); return 1; }
    const char *path;
    uint32_t from, to;
    if (argc >= 3) {
        path = argv[0]; from = (uint32_t)atoi(argv[1]); to = (uint32_t)atoi(argv[2]);
    } else {
        path = SESSION_FILE; from = (uint32_t)atoi(argv[0]); to = (uint32_t)atoi(argv[1]);
    }

    int r = (int)cvp_replay_ctx(&g_ctx, path, from, to,
                 CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
    if (r < 0) {
        char buf[80]; snprintf(buf, sizeof(buf), "CVP_REPLAY failed (%d)", r);
        err(buf); return 1;
    }
    char buf[80];
    snprintf(buf, sizeof(buf),
             "CVP_REPLAY  %s  from=%u to=%u  replayed=%d",
             path, from, to, r);
    ok(buf);
    session_save();
    return 0;
}

/* ── tick [n] ── */
static int cmd_tick(int argc, char **argv) {
    int n = (argc >= 1) ? atoi(argv[0]) : 1;
    if (n <= 0) n = 1;
    if (n > 10000) n = 10000;

    for (int i = 0; i < n; i++) engctx_tick(&g_ctx);

    char buf[64];
    snprintf(buf, sizeof(buf), "TICK ×%d  → tick=%u", n, (unsigned)g_ctx.tick);
    ok(buf);
    session_save();
    return 0;
}

/* ── inspect <x> <y> ── */
static int cmd_inspect(int argc, char **argv) {
    if (argc < 2) { err("inspect <x> <y>"); return 1; }
    uint16_t x = (uint16_t)atoi(argv[0]);
    uint16_t y = (uint16_t)atoi(argv[1]);
    if (x >= CANVAS_W || y >= CANVAS_H) { err("좌표 범위 초과"); return 1; }

    engctx_inspect_cell(&g_ctx, x, y, g_ctx.tick);
    const Cell *c = &g_ctx.cells[(uint32_t)y * CANVAS_W + x];

    uint32_t gid = tile_id_of_xy(x, y);
    int open = gate_is_open_tile(&g_ctx, (uint16_t)gid);

    print_tick();
    printf(COL_BOLD "INSPECT (%u,%u)" COL_RESET
           "  gate=%u [%s%s%s]\n",
           x, y, gid,
           open ? COL_OK : COL_DIM,
           open ? "OPEN" : "CLOSE",
           COL_RESET);
    printf("  A=0x%08X  B=0x%02X  G=%3u  R=0x%02X('%c')\n",
           c->A, c->B, c->G,
           c->R, (c->R >= 0x20 && c->R < 0x7F) ? (char)c->R : '.');

    /* 채널 의미 */
    printf("  " COL_DIM
           "A=Where(addr)  B=What(op=0x%02X)  G=State(energy=%u)  R=Stream\n"
           COL_RESET, c->B, c->G);
    return 0;
}

/* ── scan ── */
static int cmd_scan(void) {
    int open_tiles = 0, total_cells = 0;

    print_tick();
    printf(COL_WH "SCAN" COL_RESET " (열린 타일만 출력)\n");
    printf(COL_DIM "  %-6s  %-10s  %-6s  %-4s  %-4s\n" COL_RESET,
           "GATE", "CELL_ORIGIN", "OPEN", "B", "G");

    for (uint32_t gid = 0; gid < TILE_COUNT; gid++) {
        if (!gate_is_open_tile(&g_ctx, (uint16_t)gid)) continue;
        uint16_t tx = (uint16_t)(gid % TILES_X);
        uint16_t ty = (uint16_t)(gid / TILES_X);
        uint16_t cx = (uint16_t)(tx * TILE);
        uint16_t cy = (uint16_t)(ty * TILE);

        /* 타일 내 non-zero 셀 수 */
        int nz = 0;
        for (int dy = 0; dy < TILE; dy++)
            for (int dx = 0; dx < TILE; dx++) {
                const Cell *c = &g_ctx.cells[((uint32_t)(cy+dy)) * CANVAS_W + (cx+dx)];
                if (c->A || c->B || c->G || c->R) nz++;
            }

        printf("  %-6u  (%4u,%4u)    " COL_OK "OPEN" COL_RESET "  cells=%d\n",
               gid, cx, cy, nz);
        open_tiles++;
        total_cells += nz;
    }
    printf("  %s%d tiles OPEN%s  %d non-zero cells\n",
           COL_OK, open_tiles, COL_RESET, total_cells);
    return 0;
}

/* ── canvas <x> <y> <B> <G> <R> ── */
static int cmd_canvas(int argc, char **argv) {
    if (argc < 5) { err("canvas <x> <y> <B_hex> <G> <R_hex>"); return 1; }
    uint16_t x = (uint16_t)atoi(argv[0]);
    uint16_t y = (uint16_t)atoi(argv[1]);
    uint8_t  B = (uint8_t)strtoul(argv[2], NULL, 0);
    uint8_t  G = (uint8_t)atoi(argv[3]);
    uint8_t  R = (uint8_t)strtoul(argv[4], NULL, 0);

    if (x >= CANVAS_W || y >= CANVAS_H) { err("좌표 범위 초과"); return 1; }

    Cell *c = &g_ctx.cells[(uint32_t)y * CANVAS_W + x];
    c->B = B; c->G = G; c->R = R;
    engctx_tick(&g_ctx);

    char buf[80];
    snprintf(buf, sizeof(buf),
             "CELL(%u,%u)  B=0x%02X  G=%u  R=0x%02X('%c')",
             x, y, B, G, R, (R >= 0x20 && R < 0x7F) ? (char)R : '.');
    ok(buf);
    session_save();
    return 0;
}

/* ── info ── */
static int cmd_info(void) {
    int open_n = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (gate_is_open_tile(&g_ctx, (uint16_t)i)) open_n++;

    uint32_t lo = (g_ctx.tick > WH_CAP) ? g_ctx.tick - WH_CAP : 0;

    printf("\n" COL_BOLD "  SJ CANVAS OS" COL_RESET COL_DIM " — engine info\n" COL_RESET);
    printf("  %-20s %u\n",       "tick",        (unsigned)g_ctx.tick);
    printf("  %-20s %dx%d = %d cells\n", "canvas", CANVAS_W, CANVAS_H, CANVAS_W*CANVAS_H);
    printf("  %-20s %d / %d\n",  "gates OPEN",  open_n, TILE_COUNT);
    printf("  %-20s [%u..%u] cap=%d\n", "WH window", lo, (unsigned)g_ctx.tick, WH_CAP);
    printf("  %-20s (%u,%u) %dx%d\n",   "WH zone", WH_X0, WH_Y0, WH_W, WH_H);
    printf("  %-20s (%u,%u) %dx%d\n",   "BH zone", BH_X0, BH_Y0, BH_W, BH_H);
    printf("  %-20s %d\n",       "procs",        g_sched.count);
    printf("  %-20s %s\n",       "session",      SESSION_FILE);
    printf("\n");
    return 0;
}

/* ── help ── */
static int cmd_help(void) {
    printf("\n" COL_BOLD "  SJ CANVAS OS — Typewriter CLI" COL_RESET "\n");
    printf(COL_DIM "  모든 명령은 WH에 기록되고 session.cvp에 자동 저장됩니다.\n\n" COL_RESET);

    const char *cmds[] = {
        COL_GATE "GATE" COL_RESET,
        "  gate open  <id>          — WH_GATE_OPEN  (0x10)",
        "  gate close <id>          — WH_GATE_CLOSE (0x11)",
        "  gate info  <id>          — 게이트 상태 조회",
        "  gate list                — 열린 게이트 목록",
        "",
        COL_WH "WH" COL_RESET,
        "  wh write <op_hex> <gid>  — WH_WRITE (0x20)",
        "  wh log   [n]             — WH 로그 최근 n개",
        "  wh replay <from> <to>    — ENGCTX_REPLAY (0x51)",
        "",
        COL_BH "BH" COL_RESET,
        "  bh set    <pid> <energy> — BH 에너지 설정",
        "  bh decay  <pid> <amount> — BH_DECAY (0x21)",
        "  bh status [pid]          — 에너지 조회",
        "",
        COL_BOLD "PROC" COL_RESET,
        "  spawn <volh> <volt> <e>  — 프로세스 생성",
        "  ps                       — 프로세스 목록",
        "  ipc send <dst> <src> <s> — WH_IPC_SEND (0x30)",
        "",
        COL_BOLD "CVP" COL_RESET,
        "  cvp save   [file]        — CVP_SAVE (0x40)",
        "  cvp load   [file]        — CVP_LOAD (0x41)",
        "  cvp validate [file]      — CVP_VALIDATE (0x42)",
        "  cvp replay [f] <f> <t>   — CVP_REPLAY (0x43)",
        "",
        COL_TICK "ENGINE" COL_RESET,
        "  tick [n]                 — ENGCTX_TICK × n (0x50)",
        "  inspect <x> <y>          — ENGCTX_INSPECT (0x52)",
        "  scan                     — 열린 타일 스캔",
        "  canvas <x> <y> B G R     — 셀 직접 쓰기",
        "  info                     — 엔진 상태 요약",
        "  help                     — 이 화면",
        NULL,
    };
    for (int i = 0; cmds[i]; i++)
        printf("  %s\n", cmds[i]);
    printf("\n");
    return 0;
}

/* ══════════════════════════════════════════
   MAIN — 명령 라우팅
══════════════════════════════════════════ */
int main(int argc, char **argv) {
    engine_init();

    if (argc < 2) {
        cmd_info();
        cmd_help();
        return 0;
    }

    const char *cmd = argv[1];
    char **rest = argv + 2;
    int  nrest  = argc - 2;

    /* gate */
    if (strcmp(cmd, "gate") == 0 && nrest >= 1) {
        if (strcmp(rest[0], "open")  == 0) return cmd_gate_open (nrest-1, rest+1);
        if (strcmp(rest[0], "close") == 0) return cmd_gate_close(nrest-1, rest+1);
        if (strcmp(rest[0], "info")  == 0) return cmd_gate_info (nrest-1, rest+1);
        if (strcmp(rest[0], "list")  == 0) return cmd_gate_list();
        err("gate: subcommand unknown"); return 1;
    }

    /* wh */
    if (strcmp(cmd, "wh") == 0 && nrest >= 1) {
        if (strcmp(rest[0], "write")  == 0) return cmd_wh_write (nrest-1, rest+1);
        if (strcmp(rest[0], "log")    == 0) return cmd_wh_log   (nrest-1, rest+1);
        if (strcmp(rest[0], "replay") == 0) return cmd_wh_replay(nrest-1, rest+1);
        err("wh: subcommand unknown"); return 1;
    }

    /* bh */
    if (strcmp(cmd, "bh") == 0 && nrest >= 1) {
        if (strcmp(rest[0], "set")    == 0) return cmd_bh_set   (nrest-1, rest+1);
        if (strcmp(rest[0], "decay")  == 0) return cmd_bh_decay (nrest-1, rest+1);
        if (strcmp(rest[0], "status") == 0) return cmd_bh_status(nrest-1, rest+1);
        err("bh: subcommand unknown"); return 1;
    }

    /* proc */
    if (strcmp(cmd, "spawn") == 0) return cmd_spawn(nrest, rest);
    if (strcmp(cmd, "ps")    == 0) return cmd_ps();

    /* ipc */
    if (strcmp(cmd, "ipc") == 0 && nrest >= 1) {
        if (strcmp(rest[0], "send") == 0) return cmd_ipc_send(nrest-1, rest+1);
        err("ipc: subcommand unknown"); return 1;
    }

    /* cvp */
    if (strcmp(cmd, "cvp") == 0 && nrest >= 1) {
        if (strcmp(rest[0], "save")     == 0) return cmd_cvp_save    (nrest-1, rest+1);
        if (strcmp(rest[0], "load")     == 0) return cmd_cvp_load    (nrest-1, rest+1);
        if (strcmp(rest[0], "validate") == 0) return cmd_cvp_validate(nrest-1, rest+1);
        if (strcmp(rest[0], "replay")   == 0) return cmd_cvp_replay  (nrest-1, rest+1);
        err("cvp: subcommand unknown"); return 1;
    }

    /* engine */
    if (strcmp(cmd, "tick")    == 0) return cmd_tick(nrest, rest);
    if (strcmp(cmd, "inspect") == 0) return cmd_inspect(nrest, rest);
    if (strcmp(cmd, "scan")    == 0) return cmd_scan();
    if (strcmp(cmd, "canvas")  == 0) return cmd_canvas(nrest, rest);
    if (strcmp(cmd, "info")    == 0) return cmd_info();
    if (strcmp(cmd, "help")    == 0) return cmd_help();

    char buf[64];
    snprintf(buf, sizeof(buf), "unknown command: '%s'  →  try: help", cmd);
    err(buf);
    return 1;
}
