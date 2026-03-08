/*
 * sjterm.c — SJTerm: 픽셀판 타자기 터미널 (REPL)
 *
 * 실행: ./canvasos sjterm
 * 또는: ./canvasos_cli sjterm
 *
 * 화면 구성:
 *   [상단]  타이틀 + tick + 커서 위치 + 레지스터
 *   [중단]  미니맵 (64×16 ASCII 픽셀 뷰)
 *   [하단]  프롬프트 + 입력
 *
 * 세션은 항상 session.cvp에 자동 저장된다.
 */
#include "../include/sjptl.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/cvp_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MINIMAP_W  64
#define MINIMAP_H  16
#define MAP_STEP_X (CANVAS_W / MINIMAP_W)
#define MAP_STEP_Y (CANVAS_H / MINIMAP_H)

/* ── 미니맵 출력 ── */
static void sjterm_draw_minimap(const EngineContext *ctx, const PtlState *st) {
    printf("\033[90m  ┌");
    for (int x = 0; x < MINIMAP_W; x++) printf("─");
    printf("┐\033[0m\n");

    for (int my = 0; my < MINIMAP_H; my++) {
        printf("\033[90m  │\033[0m");
        for (int mx = 0; mx < MINIMAP_W; mx++) {
            int cx = mx * MAP_STEP_X + MAP_STEP_X / 2;
            int cy = my * MAP_STEP_Y + MAP_STEP_Y / 2;

            /* 커서 위치 강조 */
            int cur_mx = st->cx / MAP_STEP_X;
            int cur_my = st->cy / MAP_STEP_Y;
            if (mx == cur_mx && my == cur_my) {
                printf("\033[1;36m+\033[0m");
                continue;
            }

            /* 타일 상태 확인 */
            uint32_t gid = tile_id_of_xy((uint16_t)cx, (uint16_t)cy);
            int open = gate_is_open_tile(ctx, (uint16_t)gid);

            /* WH 영역 */
            int is_wh = (cx >= WH_X0 && cy >= WH_Y0
                         && cy < WH_Y0 + WH_H);
            /* BH 영역 */
            int is_bh = (cx >= BH_X0 && cy >= BH_Y0
                         && cy < BH_Y0 + BH_H);

            if (is_wh)      printf("\033[96m·\033[0m");
            else if (is_bh) printf("\033[91m·\033[0m");
            else if (open) {
                /* 셀 B값으로 컬러 선택 */
                const Cell *c = &ctx->cells[(uint32_t)cy * CANVAS_W + (uint32_t)cx];
                if (c->B || c->G) printf("\033[32m█\033[0m");
                else              printf("\033[32m░\033[0m");
            } else {
                printf("\033[90m░\033[0m");
            }
        }
        printf("\033[90m│\033[0m\n");
    }

    printf("\033[90m  └");
    for (int x = 0; x < MINIMAP_W; x++) printf("─");
    printf("┘\033[0m\n");
}

/* ── 헤더 출력 ── */
static void sjterm_draw_header(const EngineContext *ctx, const PtlState *st) {
    int open_n = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (gate_is_open_tile(ctx, (uint16_t)i)) open_n++;

    printf("\n\033[1;32m  SJ CANVAS OS\033[0m\033[90m — SJTerm v0.1\033[0m\n");
    printf("  \033[36mtick=%06u\033[0m  "
           "\033[33mcursor=(%d,%d)\033[0m  "
           "gates=\033[32m%d\033[0m/%d  "
           "edits=\033[96m%u\033[0m\n",
           (unsigned)ctx->tick,
           st->cx, st->cy,
           open_n, TILE_COUNT,
           (unsigned)st->edit_count);
    printf("  \033[90mA=\033[0m%08X  "
           "\033[90mB=\033[0m%02X  "
           "\033[90mG=\033[0m%3u  "
           "\033[90mR=\033[0m%02X\033[90m('%c')\033[0m\n",
           st->reg_A, st->reg_B, st->reg_G, st->reg_R,
           (st->reg_R >= 0x20 && st->reg_R < 0x7F) ? (char)st->reg_R : '.');
    if (st->blk_active)
        printf("  \033[90mblock=(%d,%d)~(%d,%d)\033[0m\n",
               st->blk_x0, st->blk_y0, st->blk_x1, st->blk_y1);
    printf("\n");
}

/* ── REPL 메인 루프 ── */
void sjterm_run(EngineContext *ctx) {
    PtlState st;
    ptl_state_init(&st, ORIGIN_X, ORIGIN_Y);

    /* 세션 복구 시도 */
    cvp_load_ctx(ctx, "session.cvp", false,
                 CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);

    printf("\033[2J\033[H"); /* clear screen */
    sjterm_draw_header(ctx, &st);
    sjterm_draw_minimap(ctx, &st);

    printf("  \033[90mhelp  →  토큰 목록   q  →  종료\033[0m\n");
    printf("  \033[90m세션은 session.cvp 에 자동 저장됩니다.\033[0m\n\n");

    char line[512];
    while (1) {
        /* 프롬프트 */
        printf("\033[1;32msjterm\033[0m\033[90m:(\033[0m"
               "\033[33m%d,%d\033[0m"
               "\033[90m):\033[0m\033[36m%04u\033[0m\033[90m>\033[0m ",
               st.cx, st.cy, (unsigned)(ctx->tick & 0xFFFF));
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) break;

        /* 줄 끝 제거 */
        size_t len = strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';

        if (!len) continue;

        int ret = ptl_exec_line(ctx, &st, line);

        /* 커밋 후 자동 저장 */
        if (st.edit_count > 0 && st.edit_count % 10 == 0) {
            cvp_save_ctx(ctx, "session.cvp", 0, 0, CVP_CONTRACT_HASH_V1, 0);
        }

        /* 미니맵 재출력 (이동/커밋 후) */
        PtlToken first_tok;
        ptl_parse_line(line, &first_tok, 1);
        bool redraw = (first_tok.kind == TOK_COMMIT ||
                       first_tok.kind == TOK_COMMIT_N ||
                       first_tok.kind == TOK_COMMIT_BLOCK ||
                       first_tok.kind == TOK_MOVE_ABS ||
                       first_tok.kind == TOK_GATE_OPEN ||
                       first_tok.kind == TOK_GATE_CLOSE);
        if (redraw) {
            sjterm_draw_header(ctx, &st);
            sjterm_draw_minimap(ctx, &st);
        }

        if (ret == 1) break; /* quit */
    }

    /* 종료 시 저장 */
    cvp_save_ctx(ctx, "session.cvp", 0, 0, CVP_CONTRACT_HASH_V1, 0);
    printf("\n\033[90m  session saved → session.cvp\033[0m\n\n");
}
