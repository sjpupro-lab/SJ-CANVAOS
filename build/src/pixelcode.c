#define _GNU_SOURCE
/*
 * pixelcode.c — Phase-9: PixelCode Parser
 *
 * 터미널 입력을 파싱하여 캔버스 셀 조작 + VM 제어.
 *
 * 문법:
 *   A=HEX  B=HH  G=DDD  R=HH|R='C'|R="STR"
 *   !  !!N  @(x,y) @home @wh @bh
 *   #(x0,y0)~(x1,y1)  #fill  #copy  #clear  #hash  #count
 *   >N  <N  vN  ^N
 *   run  stop  step [N]  trace
 *   gate N  gate -N
 *   ? ?(x,y)
 */
#include "../include/canvasos_pixelcode.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvas_determinism.h"
#include "../include/engine_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

void pxstate_init(PxState *px) {
    memset(px, 0, sizeof(*px));
    px->cx = ORIGIN_X;
    px->cy = ORIGIN_Y;
}

/* ── commit: write registers to cell at cursor, advance Y↓ ── */
static void px_commit(EngineContext *ctx, PxState *px) {
    if (px->cy >= CANVAS_H || px->cx >= CANVAS_W) return;
    uint32_t idx = px->cy * CANVAS_W + px->cx;
    ctx->cells[idx].A = px->reg_A;
    ctx->cells[idx].B = px->reg_B;
    ctx->cells[idx].G = px->reg_G;
    ctx->cells[idx].R = px->reg_R;
    engctx_tick(ctx);
    px->commit_count++;
    px->cy++; /* Y↓ = 타자기 캐리지 리턴 */
}

/* ── range ops ───────────────────────────────────────── */
static void px_range_fill(EngineContext *ctx, PxState *px) {
    if (!px->range.active) return;
    for (uint32_t y = px->range.y0; y <= px->range.y1 && y < CANVAS_H; y++)
        for (uint32_t x = px->range.x0; x <= px->range.x1 && x < CANVAS_W; x++) {
            uint32_t idx = y * CANVAS_W + x;
            ctx->cells[idx].B = px->reg_B;
            ctx->cells[idx].G = px->reg_G;
            ctx->cells[idx].R = px->reg_R;
        }
}

static void px_range_clear(EngineContext *ctx, const PxState *px) {
    if (!px->range.active) return;
    for (uint32_t y = px->range.y0; y <= px->range.y1 && y < CANVAS_H; y++)
        for (uint32_t x = px->range.x0; x <= px->range.x1 && x < CANVAS_W; x++) {
            uint32_t idx = y * CANVAS_W + x;
            memset(&ctx->cells[idx], 0, sizeof(Cell));
        }
}

static void px_range_copy(EngineContext *ctx, const PxState *px, int32_t dx, int32_t dy) {
    if (!px->range.active) return;
    uint32_t w = px->range.x1 - px->range.x0 + 1;
    uint32_t h = px->range.y1 - px->range.y0 + 1;
    if (w > 256 || h > 256) return; /* safety limit */
    Cell *tmp = malloc(w * h * sizeof(Cell));
    if (!tmp) return;
    for (uint32_t yy = 0; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++) {
            uint32_t si = (px->range.y0 + yy) * CANVAS_W + (px->range.x0 + xx);
            tmp[yy * w + xx] = ctx->cells[si];
        }
    for (uint32_t yy = 0; yy < h; yy++)
        for (uint32_t xx = 0; xx < w; xx++) {
            uint32_t dy2 = px->range.y0 + yy + (uint32_t)dy;
            uint32_t dx2 = px->range.x0 + xx + (uint32_t)dx;
            if (dx2 < CANVAS_W && dy2 < CANVAS_H)
                ctx->cells[dy2 * CANVAS_W + dx2] = tmp[yy * w + xx];
        }
    free(tmp);
}

static uint32_t px_range_count(const EngineContext *ctx, const PxState *px) {
    if (!px->range.active) return 0;
    uint32_t n = 0;
    for (uint32_t y = px->range.y0; y <= px->range.y1 && y < CANVAS_H; y++)
        for (uint32_t x = px->range.x0; x <= px->range.x1 && x < CANVAS_W; x++)
            if (ctx->cells[y * CANVAS_W + x].G > 0) n++;
    return n;
}

/* ── inspect: show cell / A-group ────────────────────── */
static void px_inspect(const EngineContext *ctx, uint32_t x, uint32_t y) {
    if (x >= CANVAS_W || y >= CANVAS_H) {
        printf("  OOB (%u,%u)\n", x, y);
        return;
    }
    uint32_t idx = y * CANVAS_W + x;
    uint32_t a_val = ctx->cells[idx].A;
    uint16_t tid = tile_id_of_xy((uint16_t)x, (uint16_t)y);
    int gopen = gate_is_open_tile(ctx, tid);

    printf("  A=%08X\n", a_val);

    /* find A-group (consecutive Y with same A) */
    uint32_t sy = y;
    while (sy > 0 && ctx->cells[(sy - 1) * CANVAS_W + x].A == a_val &&
           ctx->cells[(sy - 1) * CANVAS_W + x].B != 0) sy--;
    for (uint32_t yy = sy; yy < CANVAS_H; yy++) {
        uint32_t i = yy * CANVAS_W + x;
        if (ctx->cells[i].A != a_val && yy != y) break;
        if (ctx->cells[i].B == 0 && ctx->cells[i].G == 0 &&
            ctx->cells[i].R == 0 && yy != y) break;
        char ch = (ctx->cells[i].R >= 0x20 && ctx->cells[i].R < 0x7F)
                  ? (char)ctx->cells[i].R : '.';
        printf("  (%u,%u) B=%02X G=%03u R=%02X '%c'%s\n",
               x, yy, ctx->cells[i].B, ctx->cells[i].G, ctx->cells[i].R, ch,
               yy == y ? "  ← cursor" : "");
    }
    printf("  tile=%u gate=%s\n", tid, gopen ? "OPEN" : "CLOSE");
}

/* ── token helper ────────────────────────────────────── */
static const char *skip_ws(const char *p) {
    while (*p && (*p == ' ' || *p == '\t')) p++;
    return p;
}

/* ── main parser ─────────────────────────────────────── */
int px_exec_line(EngineContext *ctx, PxState *px, VmState *vm, const char *line) {
    if (!ctx || !px || !vm || !line) return -1;

    const char *p = skip_ws(line);
    if (!*p) return 0;

    /* multi-token: split by spaces, process left to right */
    char buf[512];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *save = NULL;
    char *tok = strtok_r(buf, " \t", &save);

    while (tok) {
        /* ── A=HHHHHHHH ─────────────────────── */
        if (tok[0] == 'A' && tok[1] == '=') {
            px->reg_A = (uint32_t)strtoul(tok + 2, NULL, 16);
        }
        /* ── B=HH ───────────────────────────── */
        else if (tok[0] == 'B' && tok[1] == '=') {
            px->reg_B = (uint8_t)strtoul(tok + 2, NULL, 16);
        }
        /* ── G=DDD or G+N or G-N ────────────── */
        else if (tok[0] == 'G' && tok[1] == '=') {
            px->reg_G = (uint8_t)atoi(tok + 2);
        }
        else if (tok[0] == 'G' && tok[1] == '+') {
            px->reg_G += (uint8_t)atoi(tok + 2);
        }
        else if (tok[0] == 'G' && tok[1] == '-') {
            px->reg_G -= (uint8_t)atoi(tok + 2);
        }
        /* ── R=HH or R='C' or R="STR" ───────── */
        else if (tok[0] == 'R' && tok[1] == '=') {
            if (tok[2] == '\'') {
                px->reg_R = (uint8_t)tok[3];
            }
            else if (tok[2] == '"') {
                /* R="HELLO" — string stream Y↓ */
                /* reconstruct from original line */
                const char *q = strstr(line, "R=\"");
                if (q) {
                    q += 3;
                    while (*q && *q != '"') {
                        px->reg_R = (uint8_t)*q;
                        px_commit(ctx, px);
                        q++;
                    }
                }
                /* skip remaining tokens (string consumed) */
                tok = NULL;
                continue;
            }
            else {
                px->reg_R = (uint8_t)strtoul(tok + 2, NULL, 16);
            }
        }
        /* ── ! commit ────────────────────────── */
        else if (tok[0] == '!' && tok[1] == '!' && isdigit((unsigned char)tok[2])) {
            /* !!N = N times */
            int n = atoi(tok + 2);
            for (int i = 0; i < n; i++) px_commit(ctx, px);
        }
        else if (tok[0] == '!' && tok[1] == '#') {
            /* !# = range fill */
            px_range_fill(ctx, px);
        }
        else if (tok[0] == '!' && (tok[1] == '\0' || tok[1] == ' ')) {
            px_commit(ctx, px);
        }
        /* ── @(x,y) @home @wh @bh ───────────── */
        else if (tok[0] == '@') {
            if (strcmp(tok, "@home") == 0) {
                px->cx = ORIGIN_X; px->cy = ORIGIN_Y;
            }
            else if (strcmp(tok, "@wh") == 0) {
                px->cx = WH_X0; px->cy = WH_Y0;
            }
            else if (strcmp(tok, "@bh") == 0) {
                px->cx = BH_X0; px->cy = BH_Y0;
            }
            else if (tok[1] == '(') {
                uint32_t x, y;
                if (sscanf(tok, "@(%u,%u)", &x, &y) == 2) {
                    if (x < CANVAS_W) px->cx = x;
                    if (y < CANVAS_H) px->cy = y;
                }
            }
        }
        /* ── >N <N vN ^N movement ────────────── */
        else if (tok[0] == '>' && isdigit((unsigned char)tok[1])) {
            px->cx += (uint32_t)atoi(tok + 1);
            if (px->cx >= CANVAS_W) px->cx = CANVAS_W - 1;
        }
        else if (tok[0] == '<' && isdigit((unsigned char)tok[1])) {
            uint32_t n = (uint32_t)atoi(tok + 1);
            px->cx = n > px->cx ? 0 : px->cx - n;
        }
        else if (tok[0] == 'v' && isdigit((unsigned char)tok[1])) {
            px->cy += (uint32_t)atoi(tok + 1);
            if (px->cy >= CANVAS_H) px->cy = CANVAS_H - 1;
        }
        else if (tok[0] == '^' && isdigit((unsigned char)tok[1])) {
            uint32_t n = (uint32_t)atoi(tok + 1);
            px->cy = n > px->cy ? 0 : px->cy - n;
        }
        /* ── #(x0,y0)~(x1,y1) range select ──── */
        else if (tok[0] == '#' && tok[1] == '(') {
            uint32_t x0, y0, x1, y1;
            /* may need rest of line */
            const char *rng = strstr(line, "#(");
            if (rng && sscanf(rng, "#(%u,%u)~(%u,%u)", &x0, &y0, &x1, &y1) == 4) {
                px->range.x0 = x0; px->range.y0 = y0;
                px->range.x1 = x1; px->range.y1 = y1;
                px->range.active = true;
                printf("  range: (%u,%u)~(%u,%u)\n", x0, y0, x1, y1);
            }
        }
        else if (strcmp(tok, "#fill") == 0) {
            px_range_fill(ctx, px);
            printf("  filled\n");
        }
        else if (strcmp(tok, "#clear") == 0) {
            px_range_clear(ctx, px);
            printf("  cleared\n");
        }
        else if (strncmp(tok, "#copy", 5) == 0) {
            int32_t dx = 0, dy = 0;
            const char *cp = strstr(line, "#copy");
            if (cp) sscanf(cp, "#copy @(%d,%d)", &dx, &dy);
            px_range_copy(ctx, px, dx, dy);
            printf("  copied +(%d,%d)\n", dx, dy);
        }
        else if (strcmp(tok, "#hash") == 0) {
            uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
            printf("  hash=0x%08X\n", h);
        }
        else if (strcmp(tok, "#count") == 0) {
            printf("  active=%u\n", px_range_count(ctx, px));
        }
        /* ── ~N repeat (last commit N times) ─── */
        else if (tok[0] == '~' && isdigit((unsigned char)tok[1])) {
            int n = atoi(tok + 1);
            for (int i = 0; i < n; i++) px_commit(ctx, px);
        }
        /* ── gate N / gate -N ────────────────── */
        else if (strcmp(tok, "gate") == 0) {
            tok = strtok_r(NULL, " \t", &save);
            if (tok) {
                int gid = atoi(tok);
                if (gid >= 0 && gid < TILE_COUNT)
                    gate_open_tile(ctx, (uint16_t)gid);
                else if (gid < 0 && (-gid) < TILE_COUNT)
                    gate_close_tile(ctx, (uint16_t)(-gid));
            }
        }
        /* ── ? inspect ───────────────────────── */
        else if (tok[0] == '?') {
            uint32_t x = px->cx, y = px->cy;
            if (tok[1] == '(')
                sscanf(tok, "?(%u,%u)", &x, &y);
            px_inspect(ctx, x, y);
        }
        /* ── VM control ──────────────────────── */
        else if (strcmp(tok, "run") == 0) {
            vm->pc_x = px->cx;
            vm->pc_y = px->cy;
            vm->tick_count = 0;
            printf("  [VM] run from (%u,%u)\n", vm->pc_x, vm->pc_y);
            vm_run(ctx, vm);
            printf("\n  [VM] halted at (%u,%u) after %u steps\n",
                   vm->pc_x, vm->pc_y, vm->tick_count);
        }
        else if (strcmp(tok, "stop") == 0) {
            vm->running = false;
        }
        else if (strcmp(tok, "step") == 0) {
            uint32_t n = 1;
            tok = strtok_r(NULL, " \t", &save);
            if (tok && isdigit((unsigned char)tok[0])) n = (uint32_t)atoi(tok);
            vm->pc_x = px->cx;
            vm->pc_y = px->cy;
            vm->running = true;
            for (uint32_t i = 0; i < n && vm->running; i++)
                vm_step(ctx, vm);
            printf("  [VM] stepped %u → PC(%u,%u)\n", n, vm->pc_x, vm->pc_y);
        }
        else if (strcmp(tok, "trace") == 0) {
            vm->trace = !vm->trace;
            printf("  trace %s\n", vm->trace ? "ON" : "OFF");
        }
        /* ── hash / save / load / exit ───────── */
        else if (strcmp(tok, "hash") == 0) {
            uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
            printf("  hash=0x%08X  tick=%u\n", h, ctx->tick);
        }
        else if (strcmp(tok, "exit") == 0 || strcmp(tok, "quit") == 0 ||
                 strcmp(tok, "q") == 0) {
            return 1; /* quit signal */
        }

        tok = strtok_r(NULL, " \t", &save);
    }
    return 0;
}
