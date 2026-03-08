/*
 * sjptl_parser.c — SJ-PTL 토큰 파서 + 실행기
 */
#include "../include/sjptl.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvasos_sched.h"
#include "../include/cvp_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ── 출력 매크로 ── */
#define P_OK(fmt,...)  printf("\033[90m[tick=%06u]\033[0m \033[32mOK\033[0m  " fmt "\n", (unsigned)ctx->tick, ##__VA_ARGS__)
#define P_ERR(fmt,...) printf("\033[90m[tick=%06u]\033[0m \033[31mERR\033[0m " fmt "\n", (unsigned)ctx->tick, ##__VA_ARGS__)
#define P_INF(fmt,...) printf("\033[90m[tick=%06u]\033[0m \033[36m···\033[0m " fmt "\n", (unsigned)ctx->tick, ##__VA_ARGS__)

/* ══════════════════════════════════════════
   ptl_state_init
══════════════════════════════════════════ */
void ptl_state_init(PtlState *st, int32_t start_x, int32_t start_y) {
    memset(st, 0, sizeof(*st));
    st->cx = start_x;
    st->cy = start_y;
    st->y_auto_step = true;
    st->auto_step_dy = 1;
    st->auto_step_dx = 0;
}

/* ══════════════════════════════════════════
   ptl_parse_line — 한 줄 → 토큰 배열
══════════════════════════════════════════ */

/* 공백/탭 건너뛰기 */
static const char *skip_ws(const char *p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* 십진수 파싱 */
static const char *parse_dec(const char *p, int32_t *out) {
    int sign = 1;
    if (*p == '-') { sign = -1; p++; }
    int32_t v = 0;
    while (isdigit((unsigned char)*p)) { v = v * 10 + (*p - '0'); p++; }
    *out = sign * v;
    return p;
}

/* 16진수 파싱 (N자리) */
static const char *parse_hex(const char *p, uint32_t *out, int digits) {
    uint32_t v = 0;
    for (int i = 0; i < digits; i++) {
        char c = *p;
        if      (c >= '0' && c <= '9') v = (v << 4) | (uint32_t)(c - '0');
        else if (c >= 'a' && c <= 'f') v = (v << 4) | (uint32_t)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v = (v << 4) | (uint32_t)(c - 'A' + 10);
        else break;
        p++;
    }
    *out = v;
    return p;
}

int ptl_parse_line(const char *line, PtlToken *out, int max_toks) {
    const char *p = line;
    int n = 0;

    while (n < max_toks) {
        p = skip_ws(p);
        if (*p == '\0' || *p == '\n' || *p == '#') break;

        PtlToken tok;
        memset(&tok, 0, sizeof(tok));

        /* ── 이동 ── */
        if (*p == '^') { tok.kind = TOK_MOVE_UP; p++; }
        else if (*p == 'v' && (p[1]==' '||p[1]=='\0'||p[1]=='\n'||p[1]=='\t'))
            { tok.kind = TOK_MOVE_DN; p++; }
        else if (*p == '<') { tok.kind = TOK_MOVE_LT; p++; }
        else if (*p == '>') { tok.kind = TOK_MOVE_RT; p++; }
        else if (*p == '.' && isdigit((unsigned char)p[1])) {
            tok.kind = TOK_MOVE_RT_N; p++;
            int32_t v; p = parse_dec(p, &v); tok.i = v;
        }
        else if (*p == ',' && isdigit((unsigned char)p[1])) {
            tok.kind = TOK_MOVE_DN_N; p++;
            int32_t v; p = parse_dec(p, &v); tok.i = v;
        }
        else if (*p == ':') {
            p++;
            int32_t x, y;
            p = parse_dec(p, &x);
            if (*p == ',') p++;
            p = parse_dec(p, &y);
            tok.kind = TOK_MOVE_ABS; tok.pos.x = x; tok.pos.y = y;
        }

        /* ── 레지스터 ── */
        else if (p[0]=='A' && p[1]=='=') {
            p += 2; uint32_t v; p = parse_hex(p, &v, 8);
            tok.kind = TOK_SET_A; tok.u = v;
        }
        else if (p[0]=='A' && p[1]=='+') {
            p += 2; uint32_t v; p = parse_hex(p, &v, 8);
            tok.kind = TOK_ADD_A; tok.u = v;
        }
        else if (p[0]=='B' && p[1]=='=') {
            p += 2; uint32_t v; p = parse_hex(p, &v, 2);
            tok.kind = TOK_SET_B; tok.u = v;
        }
        else if (p[0]=='G' && p[1]=='=') {
            p += 2; int32_t v; p = parse_dec(p, &v);
            tok.kind = TOK_SET_G; tok.u = (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
        else if (p[0]=='R' && p[1]=='=' && p[2]=='\'') {
            /* R='X' 형식 */
            tok.kind = TOK_SET_R; tok.u = (uint32_t)(unsigned char)p[3]; p += 5;
        }
        else if (p[0]=='R' && p[1]=='=') {
            p += 2; uint32_t v; p = parse_hex(p, &v, 2);
            tok.kind = TOK_SET_R; tok.u = v;
        }

        /* ── 블록 ── */
        else if (p[0]=='b' && p[1]=='L') { tok.kind = TOK_BLOCK_L; p += 2; }
        else if (p[0]=='b' && p[1]=='R') { tok.kind = TOK_BLOCK_R; p += 2; }
        else if (p[0]=='b' && p[1]=='T') { tok.kind = TOK_BLOCK_T; p += 2; }
        else if (p[0]=='b' && p[1]=='B') { tok.kind = TOK_BLOCK_B; p += 2; }
        else if (p[0]=='b' && p[1]=='W') {
            p += 2; int32_t v; p = skip_ws(p); p = parse_dec(p, &v);
            tok.kind = TOK_BLOCK_W; tok.i = v;
        }
        else if (p[0]=='b' && p[1]=='H') {
            p += 2; int32_t v; p = skip_ws(p); p = parse_dec(p, &v);
            tok.kind = TOK_BLOCK_H; tok.i = v;
        }

        /* ── 커밋 ── */
        else if (p[0]=='!' && p[1]=='!' && isdigit((unsigned char)p[2])) {
            p += 2; int32_t v; p = parse_dec(p, &v);
            tok.kind = TOK_COMMIT_N; tok.i = v;
        }
        else if (p[0]=='!' && p[1]=='B') { tok.kind = TOK_COMMIT_BLOCK; p += 2; }
        else if (p[0]=='!') { tok.kind = TOK_COMMIT; p++; }

        /* ── 저장/복원/재현 ── */
        else if (strncmp(p, "sv", 2)==0 && (p[2]==' '||p[2]=='\0'||p[2]=='\n')) {
            p += 2; p = skip_ws(p);
            tok.kind = TOK_SAVE;
            int k = 0;
            while (*p && *p != ' ' && *p != '\n' && k < 63) tok.file[k++] = *p++;
            tok.file[k] = '\0';
        }
        else if (strncmp(p, "ld", 2)==0 && (p[2]==' '||p[2]=='\0'||p[2]=='\n')) {
            p += 2; p = skip_ws(p);
            tok.kind = TOK_LOAD;
            int k = 0;
            while (*p && *p != ' ' && *p != '\n' && k < 63) tok.file[k++] = *p++;
            tok.file[k] = '\0';
        }
        else if (strncmp(p, "rp", 2)==0) {
            p += 2; p = skip_ws(p);
            int32_t from, to;
            p = parse_dec(p, &from); p = skip_ws(p);
            p = parse_dec(p, &to);
            tok.kind = TOK_REPLAY; tok.rp.from = from; tok.rp.to = to;
        }
        else if (strncmp(p, "tk", 2)==0) {
            p += 2; p = skip_ws(p);
            int32_t v = 1;
            if (isdigit((unsigned char)*p)) p = parse_dec(p, &v);
            tok.kind = TOK_TICK; tok.i = v;
        }

        /* ── 게이트 ── */
        else if (p[0]=='g' && p[1]=='o') {
            p += 2; p = skip_ws(p); int32_t v; p = parse_dec(p, &v);
            tok.kind = TOK_GATE_OPEN; tok.i = v;
        }
        else if (p[0]=='g' && p[1]=='c') {
            p += 2; p = skip_ws(p); int32_t v; p = parse_dec(p, &v);
            tok.kind = TOK_GATE_CLOSE; tok.i = v;
        }

        /* ── BH ── */
        else if (p[0]=='b' && p[1]=='e') {
            p += 2; p = skip_ws(p);
            int32_t pid; p = parse_dec(p, &pid); p = skip_ws(p);
            int32_t e;   p = parse_dec(p, &e);
            tok.kind = TOK_BH_SET;
            tok.bh.pid = (uint32_t)pid;
            tok.bh.val = (uint8_t)(e > 255 ? 255 : e < 0 ? 0 : e);
        }
        else if (p[0]=='b' && p[1]=='d') {
            p += 2; p = skip_ws(p);
            int32_t pid; p = parse_dec(p, &pid); p = skip_ws(p);
            int32_t d;   p = parse_dec(p, &d);
            tok.kind = TOK_BH_DECAY;
            tok.bh.pid = (uint32_t)pid;
            tok.bh.val = (uint8_t)(d > 255 ? 255 : d < 0 ? 0 : d);
        }

        /* ── 조회 ── */
        else if (*p == '?') { tok.kind = TOK_QUERY_CELL; p++; }
        else if (strncmp(p,"ps",2)==0 && (p[2]==' '||p[2]=='\0'||p[2]=='\n'))
            { tok.kind = TOK_QUERY_PS; p += 2; }
        else if (strncmp(p,"wl",2)==0) {
            p += 2; p = skip_ws(p);
            int32_t v = 12;
            if (isdigit((unsigned char)*p)) p = parse_dec(p, &v);
            tok.kind = TOK_QUERY_WL; tok.i = v;
        }
        else if (strncmp(p,"info",4)==0) { tok.kind = TOK_QUERY_INFO; p += 4; }
        else if (strncmp(p,"help",4)==0) { tok.kind = TOK_HELP; p += 4; }
        else if (strncmp(p,"quit",4)==0||strncmp(p,"exit",4)==0||(p[0]=='q'&&(p[1]==' '||p[1]=='\0'||p[1]=='\n')))
            { tok.kind = TOK_QUIT; p++; }

        else {
            /* 알 수 없는 토큰 — 한 단어 건너뜀 */
            tok.kind = TOK_UNKNOWN;
            while (*p && *p != ' ' && *p != '\n') p++;
        }

        out[n++] = tok;
    }

    if (n < max_toks) {
        out[n].kind = TOK_EOF;
    }
    return n;
}

/* ══════════════════════════════════════════
   커밋 내부
══════════════════════════════════════════ */
static inline bool coord_valid(int32_t x, int32_t y) {
    return x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H;
}

int ptl_do_commit(EngineContext *ctx, PtlState *st) {
    if (!coord_valid(st->cx, st->cy)) {
        P_ERR("commit: 좌표 범위 초과 (%d,%d)", st->cx, st->cy);
        return -1;
    }
    uint32_t idx = (uint32_t)st->cy * CANVAS_W + (uint32_t)st->cx;
    Cell *c = &ctx->cells[idx];

    c->A = st->reg_A;
    c->B = st->reg_B;
    c->G = st->reg_G;
    c->R = st->reg_R;

    /* WH_EDIT_COMMIT 기록 */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = (uint32_t)ctx->tick;
    wr.opcode_index  = WH_OP_EDIT_COMMIT;
    wr.flags         = 0;
    wr.param0        = st->reg_B;
    wr.target_addr   = (uint32_t)(st->cy * CANVAS_W + st->cx);
    wr.target_kind   = WH_TGT_CELL;
    wr.arg_state     = st->reg_G;
    wr.param1        = st->reg_R;
    wh_write_record(ctx, (uint64_t)ctx->tick, &wr);

    engctx_tick(ctx);
    st->edit_count++;

    /* auto-step */
    if (st->y_auto_step) {
        st->cx += st->auto_step_dx;
        st->cy += st->auto_step_dy;
    }
    return 0;
}

int ptl_do_commit_block(EngineContext *ctx, PtlState *st) {
    if (!st->blk_active) {
        P_ERR("!B: 블록 미설정 (bL/bT/bW/bH 먼저)");
        return -1;
    }
    int32_t x0 = st->blk_x0, y0 = st->blk_y0;
    int32_t x1 = st->blk_x1, y1 = st->blk_y1;
    if (x1 < x0) { int32_t t = x0; x0 = x1; x1 = t; }
    if (y1 < y0) { int32_t t = y0; y0 = y1; y1 = t; }

    int count = 0;
    for (int32_t y = y0; y <= y1 && y < CANVAS_H; y++) {
        for (int32_t x = x0; x <= x1 && x < CANVAS_W; x++) {
            if (x < 0 || y < 0) continue;
            uint32_t idx = (uint32_t)y * CANVAS_W + (uint32_t)x;
            ctx->cells[idx].B = st->reg_B;
            ctx->cells[idx].G = st->reg_G;
            ctx->cells[idx].R = st->reg_R;
            count++;
        }
    }

    /* WH_EDIT_BLOCK 기록 (블록 요약 1개) */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = (uint32_t)ctx->tick;
    wr.opcode_index  = WH_OP_EDIT_BLOCK;
    wr.param0        = st->reg_B;
    wr.target_addr   = (uint32_t)(y0 * CANVAS_W + x0);
    wr.arg_state     = st->reg_G;
    wr.param1        = st->reg_R;
    wh_write_record(ctx, (uint64_t)ctx->tick, &wr);
    engctx_tick(ctx);

    P_OK("!B  block(%d,%d)~(%d,%d) x%d cells  B=0x%02X G=%u R=0x%02X",
         x0, y0, x1, y1, count, st->reg_B, st->reg_G, st->reg_R);
    return 0;
}

/* ══════════════════════════════════════════
   ptl_exec_token
══════════════════════════════════════════ */
int ptl_exec_token(EngineContext *ctx, PtlState *st, const PtlToken *tok) {
    switch (tok->kind) {

    /* 이동 */
    case TOK_MOVE_UP:    st->cy--; return 0;
    case TOK_MOVE_DN:    st->cy++; return 0;
    case TOK_MOVE_LT:    st->cx--; return 0;
    case TOK_MOVE_RT:    st->cx++; return 0;
    case TOK_MOVE_RT_N:  st->cx += tok->i; return 0;
    case TOK_MOVE_DN_N:  st->cy += tok->i; return 0;
    case TOK_MOVE_ABS:
        st->cx = tok->pos.x;
        st->cy = tok->pos.y;
        P_INF("cursor → (%d,%d)", st->cx, st->cy);
        return 0;

    /* 레지스터 */
    case TOK_SET_A:  st->reg_A  = tok->u; return 0;
    case TOK_ADD_A:  st->reg_A += tok->u; return 0;
    case TOK_SET_B:  st->reg_B  = (uint8_t)tok->u; return 0;
    case TOK_SET_G:  st->reg_G  = (uint8_t)tok->u; return 0;
    case TOK_SET_R:  st->reg_R  = (uint8_t)tok->u; return 0;

    /* 블록 */
    case TOK_BLOCK_L:  st->blk_x0 = st->cx; st->blk_active = true; return 0;
    case TOK_BLOCK_R:  st->blk_x1 = st->cx; st->blk_active = true; return 0;
    case TOK_BLOCK_T:  st->blk_y0 = st->cy; st->blk_active = true; return 0;
    case TOK_BLOCK_B:  st->blk_y1 = st->cy; st->blk_active = true; return 0;
    case TOK_BLOCK_W:
        st->blk_x0 = st->cx;
        st->blk_x1 = st->cx + tok->i - 1;
        st->blk_active = true;
        return 0;
    case TOK_BLOCK_H:
        st->blk_y0 = st->cy;
        st->blk_y1 = st->cy + tok->i - 1;
        st->blk_active = true;
        return 0;

    /* 커밋 */
    case TOK_COMMIT:
        P_OK("! (%d,%d)  A=%08X B=%02X G=%u R=%02X",
             st->cx, st->cy, st->reg_A, st->reg_B, st->reg_G, st->reg_R);
        return ptl_do_commit(ctx, st);

    case TOK_COMMIT_N: {
        int n = tok->i > 0 ? tok->i : 1;
        if (n > 10000) n = 10000;
        P_INF("!!%d  from (%d,%d)", n, st->cx, st->cy);
        for (int i = 0; i < n; i++) {
            int r = ptl_do_commit(ctx, st);
            if (r) return r;
        }
        P_OK("!!%d done  → cursor(%d,%d)", n, st->cx, st->cy);
        return 0;
    }

    case TOK_COMMIT_BLOCK:
        return ptl_do_commit_block(ctx, st);

    /* 저장/복원 */
    case TOK_SAVE: {
        const char *path = tok->file[0] ? tok->file : "session.cvp";
        int r = (int)cvp_save_ctx(ctx, path, 0, 0, CVP_CONTRACT_HASH_V1, 0);
        if (r == CVP_OK) P_OK("sv → %s  tick=%u", path, (unsigned)ctx->tick);
        else             P_ERR("sv failed (%d)", r);
        return 0;
    }
    case TOK_LOAD: {
        const char *path = tok->file[0] ? tok->file : "session.cvp";
        int r = (int)cvp_load_ctx(ctx, path, false,
                       CVP_LOCK_SKIP, CVP_LOCK_SKIP, CVP_CONTRACT_HASH_V1);
        if (r == CVP_OK) P_OK("ld ← %s  tick=%u", path, (unsigned)ctx->tick);
        else             P_ERR("ld failed (%d)", r);
        return 0;
    }
    case TOK_REPLAY: {
        int n = engctx_replay(ctx, (uint32_t)tok->rp.from, (uint32_t)tok->rp.to);
        P_OK("rp %d..%d  replayed=%d", tok->rp.from, tok->rp.to, n);
        return 0;
    }
    case TOK_TICK: {
        int n = tok->i > 0 ? tok->i : 1;
        for (int i = 0; i < n; i++) engctx_tick(ctx);
        P_OK("tk×%d  tick=%u", n, (unsigned)ctx->tick);
        return 0;
    }

    /* 게이트 */
    case TOK_GATE_OPEN: {
        uint16_t gid = (uint16_t)tok->i;
        gate_open_tile(ctx, gid);
        WhRecord wr = { .tick_or_event=(uint32_t)ctx->tick,
            .opcode_index=WH_OP_GATE_OPEN, .target_addr=gid, .target_kind=WH_TGT_TILE };
        wh_write_record(ctx, (uint64_t)ctx->tick, &wr);
        engctx_tick(ctx);
        P_OK("go %u  [OPEN]", gid);
        return 0;
    }
    case TOK_GATE_CLOSE: {
        uint16_t gid = (uint16_t)tok->i;
        gate_close_tile(ctx, gid);
        WhRecord wr = { .tick_or_event=(uint32_t)ctx->tick,
            .opcode_index=WH_OP_GATE_CLOSE, .target_addr=gid, .target_kind=WH_TGT_TILE };
        wh_write_record(ctx, (uint64_t)ctx->tick, &wr);
        engctx_tick(ctx);
        P_OK("gc %u  [CLOSE]", gid);
        return 0;
    }

    /* BH */
    case TOK_BH_SET:
        bh_set_energy(ctx, (uint16_t)tok->bh.pid, tok->bh.val, tok->bh.val);
        P_OK("be pid=%u e=%u", tok->bh.pid, tok->bh.val);
        return 0;
    case TOK_BH_DECAY: {
        uint8_t rem = bh_decay_energy(ctx, (uint16_t)tok->bh.pid, tok->bh.val);
        P_OK("bd pid=%u -%u  rem=%u%s", tok->bh.pid, tok->bh.val, rem,
             rem==0 ? "  \033[31m→SLEEP\033[0m" : "");
        return 0;
    }

    /* 조회 */
    case TOK_QUERY_CELL: {
        if (!coord_valid(st->cx, st->cy)) {
            P_ERR("? 범위 초과 (%d,%d)", st->cx, st->cy); return 0;
        }
        const Cell *c = &ctx->cells[(uint32_t)st->cy*CANVAS_W+(uint32_t)st->cx];
        uint32_t gid = tile_id_of_xy((uint16_t)st->cx, (uint16_t)st->cy);
        int open = gate_is_open_tile(ctx, (uint16_t)gid);
        printf("\033[90m[tick=%06u]\033[0m \033[36m? (%d,%d)\033[0m  "
               "gate=%u[%s]  A=%08X B=%02X G=%3u R=%02X('%c')\n",
               (unsigned)ctx->tick, st->cx, st->cy,
               gid, open ? "\033[32mOPEN\033[0m" : "\033[90mCLOSE\033[0m",
               c->A, c->B, c->G, c->R,
               (c->R >= 0x20 && c->R < 0x7F) ? (char)c->R : '.');
        return 0;
    }
    case TOK_QUERY_PS:
        /* scheduler는 전역 — 여기선 BH 상태로 대체 */
        P_INF("ps — 현재 BH 에너지 (pid 0..7)");
        for (int i = 0; i < 8; i++) {
            uint8_t e = bh_get_energy(ctx, (uint16_t)i);
            printf("  pid=%-3d  e=%3u  %s\n", i, e,
                   e > 0 ? "\033[32mRUN\033[0m" : "\033[31mSLEEP\033[0m");
        }
        return 0;
    case TOK_QUERY_WL: {
        int n = tok->i > 0 ? tok->i : 12;
        uint32_t cur = (uint32_t)ctx->tick;
        uint32_t lo  = cur > (uint32_t)WH_CAP ? cur - (uint32_t)WH_CAP : 0;
        printf("\033[96mWH LOG\033[0m  (최근 %d, window %u..%u)\n", n, lo, cur);
        int printed = 0;
        for (int32_t t = (int32_t)cur-1; t >= (int32_t)lo && printed < n; t--) {
            WhRecord r;
            wh_read_record(ctx, (uint64_t)(uint32_t)t, &r);
            if (!r.opcode_index) continue;
            const char *nm =
                r.opcode_index == WH_OP_TICK        ? "TICK        " :
                r.opcode_index == WH_OP_GATE_OPEN   ? "\033[32mGATE_OPEN  \033[0m" :
                r.opcode_index == WH_OP_GATE_CLOSE  ? "\033[31mGATE_CLOSE \033[0m" :
                r.opcode_index == WH_OP_EDIT_COMMIT ? "\033[36mEDIT_COMMIT\033[0m" :
                r.opcode_index == WH_OP_EDIT_BLOCK  ? "\033[96mEDIT_BLOCK \033[0m" :
                r.opcode_index == WH_OP_DECAY       ? "BH_DECAY    " : "???         ";
            printf("  T%-6u  %s  tgt=%-6u  G=%3u  R=%02X\n",
                   (unsigned)t, nm, r.target_addr, r.arg_state, r.param1);
            printed++;
        }
        if (!printed) printf("  \033[90m(WH 비어있음)\033[0m\n");
        return 0;
    }
    case TOK_QUERY_INFO: {
        int open_n = 0;
        for (int i = 0; i < TILE_COUNT; i++)
            if (gate_is_open_tile(ctx, (uint16_t)i)) open_n++;
        uint32_t lo = ctx->tick > WH_CAP ? ctx->tick - WH_CAP : 0;
        printf("\033[1m  SJ CANVAS OS\033[0m\n");
        printf("  tick=%-8u  canvas=%dx%d  gates=%d/%d  WH=[%u..%u]  edits=%u\n",
               (unsigned)ctx->tick, CANVAS_W, CANVAS_H,
               open_n, TILE_COUNT, lo, (unsigned)ctx->tick,
               (unsigned)st->edit_count);
        printf("  cursor=(%d,%d)  A=%08X B=%02X G=%u R=%02X\n",
               st->cx, st->cy, st->reg_A, st->reg_B, st->reg_G, st->reg_R);
        if (st->blk_active)
            printf("  block=(%d,%d)~(%d,%d)\n",
                   st->blk_x0, st->blk_y0, st->blk_x1, st->blk_y1);
        return 0;
    }
    case TOK_HELP:
        printf(
            "\n\033[1m  SJ-PTL 건반 목록\033[0m\n"
            "  이동:  ^ v < >   .N ,N  :X,Y\n"
            "  레지:  A=HHHHHHHH  A+HH  B=HH  G=DD  R=HH or R='c'\n"
            "  블록:  bL bR bT bB  bW N  bH N\n"
            "  커밋:  !  !!N  !B\n"
            "  파일:  sv [f]  ld [f]  rp FROM TO  tk [N]\n"
            "  게이트: go N  gc N\n"
            "  BH:    be PID E  bd PID D\n"
            "  조회:  ?  ps  wl [N]  info  help  q\n\n"
        );
        return 0;
    case TOK_QUIT:
        return 1;
    case TOK_EOF:
        return 0;
    default:
        P_ERR("unknown token (%d)", (int)tok->kind);
        return 0;
    }
}

/* ══════════════════════════════════════════
   ptl_exec_line
══════════════════════════════════════════ */
int ptl_exec_line(EngineContext *ctx, PtlState *st, const char *line) {
    PtlToken toks[64];
    int n = ptl_parse_line(line, toks, 64);
    for (int i = 0; i < n; i++) {
        int r = ptl_exec_token(ctx, st, &toks[i]);
        if (r != 0) return r;
    }
    return 0;
}
