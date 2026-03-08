/*
 * pixel_loader.c — Patch-D: PixelCode Self-Hosting Engine
 *
 * Core idea: each utility is "planted" as VM cells on the canvas,
 * then the VM runs from that position and produces output via VM_PRINT.
 * This makes utilities genuine PixelCode programs, not C functions.
 *
 * Cell encoding (per VM spec):
 *   B = opcode (VM_PRINT, VM_HALT, VM_SYSCALL, etc.)
 *   R = data byte (character to print, syscall number, etc.)
 *   G = parameter (energy, state)
 *   A = address operand
 */
#include "../include/canvasos_pixel_loader.h"
#include "../include/canvasos_vm.h"
#include "../include/canvasos_fd.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

/* ── Global execution mode ───────────────────────────── */
static int g_pxl_mode = PXL_MODE_C_FALLBACK;

void pxl_set_mode(int mode) { g_pxl_mode = mode; }
int  pxl_get_mode(void)     { return g_pxl_mode; }

/* ── Utility Registry ────────────────────────────────── */
typedef struct {
    const char *name;
    int         id;
} PxlUtilEntry;

static const PxlUtilEntry g_registry[] = {
    { "echo", PXL_UTIL_ECHO },
    { "cat",  PXL_UTIL_CAT  },
    { "info", PXL_UTIL_INFO },
    { "hash", PXL_UTIL_HASH },
    { "help", PXL_UTIL_HELP },
    { NULL,   PXL_UTIL_NONE },
};

int pxl_find_utility(const char *name) {
    if (!name) return PXL_UTIL_NONE;
    for (int i = 0; g_registry[i].name; i++)
        if (strcmp(g_registry[i].name, name) == 0)
            return g_registry[i].id;
    return PXL_UTIL_NONE;
}

/* ═══════════════════════════════════════════════════════
 * Program Planters
 *
 * Each function writes VM cells at (x, y↓) and returns
 * the number of cells planted. The caller then runs
 * vm_init(x, y) + vm_run() to execute.
 * ═══════════════════════════════════════════════════════ */

/* ── echo: PRINT each char + newline + HALT ──────────── */
int pxl_plant_echo(EngineContext *ctx, uint32_t x, uint32_t y,
                   const char *arg) {
    if (!ctx || !arg) return 0;
    int n = 0;

    /* Print each character */
    for (int i = 0; arg[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)arg[i]);
        n++;
    }

    /* Newline */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, '\n');
        n++;
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }

    return n;
}

/* ── cat: read virtual path and print content ────────── */
int pxl_plant_cat(EngineContext *ctx, uint32_t x, uint32_t y,
                  const char *path, ProcTable *pt) {
    if (!ctx) return 0;
    int n = 0;

    /* For cat, we need to read the file content first
     * and then plant PRINT cells for each byte.
     * This is the "self-hosting" approach: the program
     * is generated dynamically from the file content. */

    /* Try to render virtual path content */
    extern int path_resolve_virtual(EngineContext*, void*, const char*, void*);
    extern int path_render_virtual(const ProcTable*, EngineContext*,
                                   FsKey, char*, size_t);

    char content[512] = {0};
    int got_content = 0;

    /* Try virtual path */
    FsKey vkey;
    if (path && path_resolve_virtual(ctx, NULL, path, &vkey) == 0) {
        if (path_render_virtual(pt, ctx, vkey, content, sizeof(content)) == 0)
            got_content = 1;
    }

    /* Try CanvasFS bridge */
    if (!got_content && path) {
        extern int fd_open_bridged(void*, void*, uint32_t, const char*, uint8_t);
        int fd = fd_open_bridged(ctx, NULL, 0, path, 0x01 /* O_READ */);
        if (fd >= 3) {
            uint8_t buf[256];
            int nr = fd_read(ctx, 0, fd, buf, 255);
            if (nr > 0) {
                memcpy(content, buf, (size_t)nr);
                content[nr] = '\0';
                got_content = 1;
            }
            fd_close(ctx, 0, fd);
        }
    }

    if (!got_content) {
        const char *msg = "(empty)\n";
        for (int i = 0; msg[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)msg[i]);
            n++;
        }
    } else {
        for (int i = 0; content[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
            vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)content[i]);
            n++;
        }
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── info: print system information ──────────────────── */
int pxl_plant_info(EngineContext *ctx, uint32_t x, uint32_t y,
                   ProcTable *pt) {
    if (!ctx) return 0;
    int n = 0;

    /* Generate info string */
    char info[256];
    uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    int gates = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (gate_is_open_tile(ctx, (uint16_t)i)) gates++;

    uint32_t procs = pt ? pt->count : 0;
    snprintf(info, sizeof(info),
             "tick=%u hash=0x%08X\n"
             "gates=%d/%d procs=%u/%u\n"
             "canvas=%ux%u=%u cells\n",
             ctx->tick, h,
             gates, TILE_COUNT, procs, PROC8_MAX,
             CANVAS_W, CANVAS_H, CANVAS_W * CANVAS_H);

    /* Plant PRINT cells */
    for (int i = 0; info[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)info[i]);
        n++;
    }

    /* HALT */
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── hash: print canvas hash ─────────────────────────── */
int pxl_plant_hash(EngineContext *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    int n = 0;

    uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    char line[64];
    snprintf(line, sizeof(line), "canvas hash = 0x%08X  tick=%u\n",
             h, ctx->tick);

    for (int i = 0; line[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)line[i]);
        n++;
    }
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ── help: print available commands ──────────────────── */
int pxl_plant_help(EngineContext *ctx, uint32_t x, uint32_t y) {
    if (!ctx) return 0;
    int n = 0;

    const char *text =
        "CanvasOS Shell — PixelCode Self-Hosted\n"
        "  echo <text>    Print text\n"
        "  cat <path>     Show file/virtual path\n"
        "  info           System information\n"
        "  hash           Canvas hash\n"
        "  help           This message\n"
        "  ps, kill, ls, cd, mkdir, rm\n"
        "  det, timewarp, env, exit\n";

    for (int i = 0; text[i] && (y + (uint32_t)n) < CANVAS_H; i++) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_PRINT, 0, (uint8_t)text[i]);
        n++;
    }
    if ((y + (uint32_t)n) < CANVAS_H) {
        vm_plant(ctx, x, y + (uint32_t)n, 0, VM_HALT, 0, 0);
        n++;
    }
    return n;
}

/* ═══════════════════════════════════════════════════════
 * Execute a utility via PixelCode
 *
 * 1. Look up utility in registry
 * 2. Plant program on canvas at PXL_PROG region
 * 3. Run VM from planted position
 * 4. Return 0=ok, -1=not found, -2=plant failed
 * ═══════════════════════════════════════════════════════ */
int pxl_exec_utility(EngineContext *ctx, ProcTable *pt, PipeTable *pipes,
                     const char *cmd, const char *arg) {
    if (!ctx || !cmd) return -1;
    (void)pipes;

    int uid = pxl_find_utility(cmd);
    if (uid == PXL_UTIL_NONE) return -1;

    /* Clear the program region before planting */
    for (uint32_t y = PXL_PROG_Y; y < PXL_PROG_Y + 512 && y < CANVAS_H; y++) {
        uint32_t idx = y * CANVAS_W + PXL_PROG_X;
        memset(&ctx->cells[idx], 0, sizeof(Cell));
    }

    int planted = 0;

    switch (uid) {
    case PXL_UTIL_ECHO:
        planted = pxl_plant_echo(ctx, PXL_PROG_X, PXL_PROG_Y,
                                 arg ? arg : "");
        break;
    case PXL_UTIL_CAT:
        planted = pxl_plant_cat(ctx, PXL_PROG_X, PXL_PROG_Y, arg, pt);
        break;
    case PXL_UTIL_INFO:
        planted = pxl_plant_info(ctx, PXL_PROG_X, PXL_PROG_Y, pt);
        break;
    case PXL_UTIL_HASH:
        planted = pxl_plant_hash(ctx, PXL_PROG_X, PXL_PROG_Y);
        break;
    case PXL_UTIL_HELP:
        planted = pxl_plant_help(ctx, PXL_PROG_X, PXL_PROG_Y);
        break;
    default:
        return -1;
    }

    if (planted <= 0) return -2;

    /* Run VM from program position */
    VmState vm;
    vm_init(&vm, PXL_PROG_X, PXL_PROG_Y, PID_SHELL);
    vm_run(ctx, &vm);

    return 0;
}
