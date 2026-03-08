/*
 * canvasos_launcher.c — CanvasOS Mobile Launcher
 *
 * "OS 위의 OS" — Android Termux에서 CanvasOS를 부팅하는 런처.
 *
 * 기능:
 *   - POST 부트 시퀀스 (캔버스 메모리 검증, 게이트 초기화)
 *   - 통합 셸 (SJTerm 편집 + Tervas 조회 + 시스템 명령)
 *   - 미니맵 + 상태바
 *   - 세션 자동 저장/복구
 *
 * 빌드:
 *   make launcher
 *
 * 실행:
 *   cp canvasos_launcher ~ && ~/canvasos_launcher
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvas_bh_compress.h"
#include "../include/cvp_io.h"
#include "../include/sjptl.h"
#include "../include/tervas/tervas_core.h"
#include "../include/tervas/tervas_bridge.h"
#include "../include/tervas/tervas_cli.h"
#include "../include/tervas/tervas_projection.h"
#include "../include/tervas/tervas_render_cell.h"
#include "../include/tervas/tervas_dispatch.h"

/* ═══════════════════════════════════════════════════════════
 *  CANVAS MEMORY
 * ═══════════════════════════════════════════════════════════ */
static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];
static EngineContext g_ctx;

/* Tervas instance */
static Tervas g_tv;

/* SJ-PTL state */
static PtlState g_ptl;

/* Session file */
#define SESSION_FILE "canvasos_session.cvp"

/* ═══════════════════════════════════════════════════════════
 *  ANSI HELPERS
 * ═══════════════════════════════════════════════════════════ */
#define CLR     "\033[0m"
#define DIM     "\033[90m"
#define BOLD    "\033[1m"
#define GRN     "\033[32m"
#define BGRN    "\033[1;32m"
#define CYN     "\033[36m"
#define BCYN    "\033[1;36m"
#define YEL     "\033[33m"
#define BYEL    "\033[1;33m"
#define RED     "\033[31m"
#define BRED    "\033[1;31m"
#define BLU     "\033[34m"
#define BBLU    "\033[1;34m"
#define MAG     "\033[35m"
#define WHT     "\033[97m"
#define CLRSCR  "\033[2J\033[H"

static void delay_ms(int ms) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ms * 1000000L };
    nanosleep(&ts, NULL);
}

static void boot_line(const char *tag, const char *msg, int ok) {
    printf("  " DIM "[" CLR "%s" DIM "]" CLR " %-42s ", tag, msg);
    fflush(stdout);
    delay_ms(80);
    if (ok)
        printf(GRN "OK" CLR "\n");
    else
        printf(RED "FAIL" CLR "\n");
    fflush(stdout);
}

/* ═══════════════════════════════════════════════════════════
 *  BOOT SEQUENCE
 * ═══════════════════════════════════════════════════════════ */
static int boot_sequence(void) {
    printf(CLRSCR);
    printf("\n");
    printf(DIM "  ──────────────────────────────────────────────" CLR "\n");
    printf(BGRN "   ██████╗ █████╗ ███╗   ██╗██╗   ██╗ █████╗ ███████╗" CLR "\n");
    printf(BGRN "  ██╔════╝██╔══██╗████╗  ██║██║   ██║██╔══██╗██╔════╝" CLR "\n");
    printf(BGRN "  ██║     ███████║██╔██╗ ██║██║   ██║███████║███████╗" CLR "\n");
    printf(BGRN "  ██║     ██╔══██║██║╚██╗██║╚██╗ ██╔╝██╔══██║╚════██║" CLR "\n");
    printf(BGRN "  ╚██████╗██║  ██║██║ ╚████║ ╚████╔╝ ██║  ██║███████║" CLR "\n");
    printf(BGRN "   ╚═════╝╚═╝  ╚═╝╚═╝  ╚═══╝  ╚═══╝  ╚═╝  ╚═╝╚══════╝" CLR "\n");
    printf(DIM "                          O S  v1.0.1-p7" CLR "\n");
    printf(DIM "  ──────────────────────────────────────────────" CLR "\n");
    printf("\n");
    delay_ms(300);

    printf(DIM "  POST — Power-On Self Test" CLR "\n\n");

    /* Memory init */
    memset(g_cells,  0, sizeof(g_cells));
    memset(g_gates,  0, sizeof(g_gates));
    memset(g_active, 0, sizeof(g_active));
    boot_line(BCYN "MEM" CLR, "Canvas memory 8 MB clear", 1);

    /* Engine init */
    engctx_init(&g_ctx, g_cells, CANVAS_W * CANVAS_H,
                g_gates, g_active, NULL);
    boot_line(BCYN "ENG" CLR, "EngineContext initialized", 1);

    /* Gate check */
    int closed = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (!gate_is_open_tile(&g_ctx, (uint16_t)i)) closed++;
    char gbuf[64];
    snprintf(gbuf, sizeof(gbuf), "Gates: %d/%d CLOSE (default)", closed, TILE_COUNT);
    boot_line(BYEL "GAT" CLR, gbuf, closed == TILE_COUNT);

    /* WH/BH geometry */
    int wh_ok = (WH_X0 == 512 && WH_Y0 == 512 && WH_W == 512 && WH_H == 128);
    int bh_ok = (BH_X0 == 512 && BH_Y0 == 640 && BH_W == 512 && BH_H == 64);
    boot_line(BBLU "WHT" CLR, "WhiteHole (512,512) 512x128", wh_ok);
    boot_line(BRED "BLK" CLR, "BlackHole (512,640) 512x64", bh_ok);

    /* WH capacity */
    char whbuf[64];
    snprintf(whbuf, sizeof(whbuf), "WH_CAP = %d records (ring)", WH_CAP);
    boot_line(BBLU "CAP" CLR, whbuf, WH_CAP == 32768);

    /* Determinism */
    uint32_t h1 = dk_canvas_hash(g_ctx.cells, g_ctx.cells_count);
    uint32_t h2 = dk_canvas_hash(g_ctx.cells, g_ctx.cells_count);
    boot_line(MAG  "DET" CLR, "DK hash determinism", h1 == h2);

    /* Tervas init */
    int tv_ok = (tervas_init(&g_tv) == TV_OK);
    boot_line(BCYN "TRV" CLR, "Tervas terminal (READ-ONLY)", tv_ok);

    /* SJ-PTL init */
    ptl_state_init(&g_ptl, ORIGIN_X, ORIGIN_Y);
    boot_line(BGRN "PTL" CLR, "SJ-PTL parser ready", 1);

    /* Session restore */
    printf("\n");
    CvpStatus cvp_st = cvp_load_ctx(&g_ctx, SESSION_FILE, false,
                                     CVP_LOCK_SKIP, CVP_LOCK_SKIP,
                                     CVP_CONTRACT_HASH_V1);
    if (cvp_st == CVP_OK) {
        boot_line(GRN  "CVP" CLR, "Session restored from " SESSION_FILE, 1);
    } else {
        boot_line(YEL  "CVP" CLR, "No session found — fresh canvas", 1);
    }

    /* Tick */
    engctx_tick(&g_ctx);

    printf("\n");
    printf(DIM "  ──────────────────────────────────────────────" CLR "\n");
    printf(BGRN "  BOOT COMPLETE" CLR DIM " — tick %u" CLR "\n", g_ctx.tick);
    printf(DIM "  ──────────────────────────────────────────────" CLR "\n");
    printf("\n");
    delay_ms(200);
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 *  STATUS BAR
 * ═══════════════════════════════════════════════════════════ */
static void draw_status(void) {
    int open_n = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (gate_is_open_tile(&g_ctx, (uint16_t)i)) open_n++;

    uint32_t hash = dk_canvas_hash(g_ctx.cells, g_ctx.cells_count);

    printf(DIM "  ╭─────────────────────────────────────────────╮" CLR "\n");
    printf(DIM "  │" CLR
           " " BCYN "tick" CLR "=%05u"
           "  " BYEL "cur" CLR "=(%d,%d)"
           "  " GRN "gate" CLR "=%d"
           "  " MAG "hash" CLR "=%08X"
           DIM " │" CLR "\n",
           g_ctx.tick, g_ptl.cx, g_ptl.cy, open_n, hash);
    printf(DIM "  │" CLR
           " A=" WHT "%08X" CLR
           " B=" WHT "%02X" CLR
           " G=" WHT "%3u" CLR
           " R=" WHT "%02X" CLR "(" BCYN "%c" CLR ")"
           "  edits=" CYN "%u" CLR
           DIM "     │" CLR "\n",
           g_ptl.reg_A, g_ptl.reg_B, g_ptl.reg_G, g_ptl.reg_R,
           (g_ptl.reg_R >= 0x20 && g_ptl.reg_R < 0x7F) ? (char)g_ptl.reg_R : '.',
           g_ptl.edit_count);
    printf(DIM "  ╰─────────────────────────────────────────────╯" CLR "\n");
}

/* ═══════════════════════════════════════════════════════════
 *  MINIMAP (64x16)
 * ═══════════════════════════════════════════════════════════ */
#define MMAP_W 64
#define MMAP_H 16
#define MSTEP_X (CANVAS_W / MMAP_W)
#define MSTEP_Y (CANVAS_H / MMAP_H)

static void draw_minimap(void) {
    printf(DIM "  ┌");
    for (int x = 0; x < MMAP_W; x++) printf("─");
    printf("┐" CLR "\n");

    for (int my = 0; my < MMAP_H; my++) {
        printf(DIM "  │" CLR);
        for (int mx = 0; mx < MMAP_W; mx++) {
            int cx = mx * MSTEP_X + MSTEP_X / 2;
            int cy = my * MSTEP_Y + MSTEP_Y / 2;

            int cur_mx = g_ptl.cx / MSTEP_X;
            int cur_my = g_ptl.cy / MSTEP_Y;

            if (mx == cur_mx && my == cur_my) {
                printf(BCYN "@" CLR);
                continue;
            }

            int is_wh = (cx >= WH_X0 && cx < WH_X0 + WH_W &&
                         cy >= WH_Y0 && cy < WH_Y0 + WH_H);
            int is_bh = (cx >= BH_X0 && cx < BH_X0 + BH_W &&
                         cy >= BH_Y0 && cy < BH_Y0 + BH_H);

            uint32_t gid = tile_id_of_xy((uint16_t)cx, (uint16_t)cy);
            int open = gate_is_open_tile(&g_ctx, (uint16_t)gid);

            if (is_wh) {
                const Cell *c = &g_ctx.cells[(uint32_t)cy * CANVAS_W + (uint32_t)cx];
                printf(c->G > 0 ? BBLU "█" CLR : BLU "·" CLR);
            } else if (is_bh) {
                const Cell *c = &g_ctx.cells[(uint32_t)cy * CANVAS_W + (uint32_t)cx];
                printf(c->G > 0 ? BRED "█" CLR : RED "·" CLR);
            } else if (open) {
                const Cell *c = &g_ctx.cells[(uint32_t)cy * CANVAS_W + (uint32_t)cx];
                if (c->B || c->G)
                    printf(BGRN "█" CLR);
                else
                    printf(GRN "░" CLR);
            } else {
                printf(DIM "·" CLR);
            }
        }
        printf(DIM "│" CLR "\n");
    }

    printf(DIM "  └");
    for (int x = 0; x < MMAP_W; x++) printf("─");
    printf("┘" CLR "\n");
}

/* ═══════════════════════════════════════════════════════════
 *  HOME SCREEN
 * ═══════════════════════════════════════════════════════════ */
static void draw_home(void) {
    printf(CLRSCR);
    printf("\n");
    printf(BGRN "  ◆ CANVAS OS" CLR DIM " v1.0.1-p7" CLR "\n\n");
    draw_status();
    printf("\n");
    draw_minimap();
    printf("\n");
    printf(DIM "  ┌─ 명령 ─────────────────────────────────────┐" CLR "\n");
    printf(DIM "  │" CLR "  " BGRN "SJ-PTL" CLR " 편집:  B=01 G=64 R='A' !         " DIM "│" CLR "\n");
    printf(DIM "  │" CLR "  " BCYN "Tervas" CLR " 조회:  " YEL "tv " CLR "view all / inspect x y  " DIM "│" CLR "\n");
    printf(DIM "  │" CLR "  " BYEL "이동" CLR ":          :512,512  ^v<>  .N  ,N     " DIM "│" CLR "\n");
    printf(DIM "  │" CLR "  " MAG "게이트" CLR ":        go ID / gc ID               " DIM "│" CLR "\n");
    printf(DIM "  │" CLR "  " BLU "시스템" CLR ":        save / load / home / exit   " DIM "│" CLR "\n");
    printf(DIM "  │" CLR "  " DIM "help" CLR ":          전체 명령 목록              " DIM "│" CLR "\n");
    printf(DIM "  └─────────────────────────────────────────────┘" CLR "\n");
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════
 *  HELP
 * ═══════════════════════════════════════════════════════════ */
static void print_help(void) {
    printf("\n");
    printf(BGRN "  ═══ CanvasOS 통합 셸 명령 ═══" CLR "\n\n");

    printf(BYEL "  [SJ-PTL 편집]" CLR "\n");
    printf("    :X,Y          커서 절대 이동\n");
    printf("    ^ v < >       커서 상하좌우\n");
    printf("    .N  ,N        오른쪽/아래 N칸\n");
    printf("    A=HEX         A 채널 설정 (32bit hex)\n");
    printf("    B=HH          B 채널 설정 (8bit hex)\n");
    printf("    G=DDD         G 채널 설정 (decimal)\n");
    printf("    R=HH / R='C'  R 채널 설정\n");
    printf("    !             커밋 (셀 기록 + y+1)\n");
    printf("    !!N           N회 반복 커밋\n");
    printf("    go ID         게이트 OPEN\n");
    printf("    gc ID         게이트 CLOSE\n");
    printf("    be PID E      BH 에너지 설정\n");
    printf("    tk [N]        tick N회 진행\n");
    printf("    ?             현재 셀 조회\n");
    printf("\n");

    printf(BCYN "  [Tervas 조회] (tv 접두사)" CLR "\n");
    printf("    tv view all          전체 canvas\n");
    printf("    tv view wh / bh      WH/BH 영역\n");
    printf("    tv view a HEX        A 값 필터\n");
    printf("    tv inspect X Y       셀 상세\n");
    printf("    tv tick now          현재 tick\n");
    printf("    tv quick wh          = tv view wh\n");
    printf("    tv help              전체 Tervas 명령\n");
    printf("\n");

    printf(BLU "  [시스템]" CLR "\n");
    printf("    home          홈 화면 (미니맵 + 상태)\n");
    printf("    map           미니맵만 표시\n");
    printf("    stat          상태바만 표시\n");
    printf("    save          세션 저장 (CVP)\n");
    printf("    load          세션 복구 (CVP)\n");
    printf("    hash          캔버스 해시 출력\n");
    printf("    exit / quit   종료 (자동 저장)\n");
    printf("\n");
}

/* ═══════════════════════════════════════════════════════════
 *  COMMAND ROUTER
 * ═══════════════════════════════════════════════════════════ */
static int route_command(char *line) {

    /* trim */
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
        line[--len] = '\0';
    if (!len) return 0;

    /* ── System commands ─────────────────────────── */
    if (strcmp(line, "exit") == 0 || strcmp(line, "quit") == 0 ||
        strcmp(line, "q") == 0) {
        return 1; /* quit signal */
    }

    if (strcmp(line, "home") == 0) {
        draw_home();
        return 0;
    }

    if (strcmp(line, "map") == 0) {
        draw_minimap();
        return 0;
    }

    if (strcmp(line, "stat") == 0) {
        draw_status();
        return 0;
    }

    if (strcmp(line, "help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(line, "save") == 0) {
        CvpStatus st = cvp_save_ctx(&g_ctx, SESSION_FILE,
                                     0, 0, CVP_CONTRACT_HASH_V1, 0);
        printf("  " GRN "저장 완료" CLR ": %s (%s)\n", SESSION_FILE, cvp_strerror(st));
        return 0;
    }

    if (strcmp(line, "load") == 0) {
        CvpStatus st = cvp_load_ctx(&g_ctx, SESSION_FILE, false,
                                     CVP_LOCK_SKIP, CVP_LOCK_SKIP,
                                     CVP_CONTRACT_HASH_V1);
        printf("  " CYN "로드" CLR ": %s\n", cvp_strerror(st));
        return 0;
    }

    if (strcmp(line, "hash") == 0) {
        uint32_t h = dk_canvas_hash(g_ctx.cells, g_ctx.cells_count);
        printf("  " MAG "canvas hash" CLR " = 0x%08X  tick=%u\n", h, g_ctx.tick);
        return 0;
    }

    /* ── Tervas commands (tv prefix) ─────────────── */
    if (strncmp(line, "tv ", 3) == 0) {
        const char *tv_cmd = line + 3;

        /* Ensure snapshot is fresh */
        tervas_bridge_attach(&g_tv, &g_ctx);
        tervas_bridge_snapshot(&g_tv, &g_ctx, g_ctx.tick);
        g_tv.running = true;

        int rc = tv_cli_exec(&g_tv, &g_ctx, tv_cmd);

        if (rc != TV_OK && rc != TV_ERR_TICK_OOB) {
            printf("  " DIM "tervas: %s" CLR "\n", g_tv.status_msg);
        } else if (g_tv.status_msg[0]) {
            printf("  " CYN "%s" CLR "\n", g_tv.status_msg);
        }

        /* Show stats for view commands */
        if (strncmp(tv_cmd, "view", 4) == 0 || strncmp(tv_cmd, "quick", 5) == 0) {
            TvFrame fr;
            TvFilter flt;
            tv_filter_reset(&flt);
            flt.mode = g_tv.filter.mode;
            flt.a_count = g_tv.filter.a_count;
            flt.b_count = g_tv.filter.b_count;
            memcpy(flt.a_values, g_tv.filter.a_values, sizeof(flt.a_values));
            memcpy(flt.b_values, g_tv.filter.b_values, sizeof(flt.b_values));
            tv_build_frame(&fr, &g_tv.snapshot, &flt, 64, 32);
            printf("  " DIM "visible=%u  wh=%u  bh=%u  tick=%u" CLR "\n",
                   fr.total_visible, fr.wh_active, fr.bh_active, fr.tick);
        }
        return 0;
    }

    /* ── SJ-PTL commands (everything else) ───────── */
    int ret = ptl_exec_line(&g_ctx, &g_ptl, line);

    /* auto-save every 10 edits */
    if (g_ptl.edit_count > 0 && g_ptl.edit_count % 10 == 0) {
        cvp_save_ctx(&g_ctx, SESSION_FILE, 0, 0, CVP_CONTRACT_HASH_V1, 0);
    }

    /* redraw after significant operations */
    PtlToken first_tok;
    int ntok = ptl_parse_line(line, &first_tok, 1);
    if (ntok > 0 && (first_tok.kind == TOK_COMMIT ||
                      first_tok.kind == TOK_COMMIT_N ||
                      first_tok.kind == TOK_COMMIT_BLOCK ||
                      first_tok.kind == TOK_GATE_OPEN ||
                      first_tok.kind == TOK_GATE_CLOSE)) {
        draw_status();
    }

    if (ret == 1) return 1; /* quit from ptl */
    return 0;
}

/* ═══════════════════════════════════════════════════════════
 *  MAIN
 * ═══════════════════════════════════════════════════════════ */
int main(int argc, char **argv) {
    (void)argc; (void)argv;

    /* ── BOOT ── */
    boot_sequence();

    /* ── HOME SCREEN ── */
    delay_ms(500);
    draw_home();

    /* ── MAIN LOOP ── */
    char line[512];
    while (1) {
        /* prompt */
        printf(BGRN "canvas" CLR DIM ":" CLR
               BYEL "%d,%d" CLR DIM ":" CLR
               BCYN "%04u" CLR DIM ">" CLR " ",
               g_ptl.cx, g_ptl.cy,
               (unsigned)(g_ctx.tick & 0xFFFF));
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        int quit = route_command(line);
        if (quit) break;
    }

    /* ── SHUTDOWN ── */
    printf("\n");
    printf(DIM "  ──────────────────────────────────────────────" CLR "\n");
    printf(DIM "  Saving session..." CLR "\n");
    CvpStatus st = cvp_save_ctx(&g_ctx, SESSION_FILE,
                                 0, 0, CVP_CONTRACT_HASH_V1, 0);
    printf("  " GRN "%s" CLR " → %s\n", SESSION_FILE, cvp_strerror(st));

    uint32_t final_hash = dk_canvas_hash(g_ctx.cells, g_ctx.cells_count);
    printf("  " MAG "Final hash" CLR ": 0x%08X  tick=%u  edits=%u\n",
           final_hash, g_ctx.tick, g_ptl.edit_count);

    tervas_free(&g_tv);

    printf(DIM "  ──────────────────────────────────────────────" CLR "\n");
    printf(BGRN "  CanvasOS halted." CLR "\n\n");

    return 0;
}
