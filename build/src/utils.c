/*
 * utils.c — Phase-10: Shell Utility Commands
 */
#include "../include/canvasos_utils.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvasos_bridge.h"
#include <stdio.h>
#include <string.h>

int cmd_ps(ProcTable *pt) {
    if (!pt) return -1;
    printf("  PID  STATE    ENERGY  LANE  PARENT  TILE\n");
    for (int i = 0; i < PROC8_MAX; i++) {
        const Proc8 *p = &pt->procs[i];
        if (p->pid == 0xFFFFFFFF) continue;
        if (p->state == PROC_ZOMBIE && p->energy == 0 && p->pid != 0) continue;
        const char *st = p->state == PROC_RUNNING  ? "RUN " :
                         p->state == PROC_SLEEPING ? "SLP " :
                         p->state == PROC_BLOCKED  ? "BLK " : "ZMB ";
        printf("  %3u  %s  %6u  %4u  %6u  %4u\n",
               p->pid, st, p->energy, p->lane_id,
               p->parent_pid, p->code_tile);
    }
    return 0;
}

int cmd_kill(ProcTable *pt, uint32_t pid, uint8_t sig) {
    return sig_send(pt, pid, sig);
}

int cmd_ls(EngineContext *ctx, PathContext *pc, const char *dir) {
    FsKey target = pc->cwd;
    if (dir && strlen(dir) > 0) {
        if (path_resolve(ctx, pc, dir, &target) != 0) {
            printf("  ls: not found: %s\n", dir);
            return -1;
        }
    }
    char names[16][16];
    FsKey keys[16];
    int n = path_ls(ctx, pc, target, names, keys, 16);
    if (n == 0) { printf("  (empty)\n"); return 0; }
    for (int i = 0; i < n; i++)
        printf("  %s\n", names[i]);
    return n;
}

int cmd_cd(PathContext *pc, EngineContext *ctx, const char *path) {
    int rc = path_cd(pc, ctx, path);
    if (rc != 0) printf("  cd: not found\n");
    return rc;
}

int cmd_mkdir(EngineContext *ctx, PathContext *pc, const char *name) {
    int rc = path_mkdir(ctx, pc, name);
    if (rc == -2) printf("  mkdir: already exists\n");
    else if (rc < 0) printf("  mkdir: error\n");
    return rc;
}

int cmd_rm(EngineContext *ctx, PathContext *pc, const char *path) {
    int rc = path_rm(ctx, pc, path);
    if (rc != 0) printf("  rm: not found\n");
    return rc;
}

int cmd_cat(EngineContext *ctx, PathContext *pc, uint32_t pid, const char *path) {
    if (!ctx || !pc || !path || strlen(path) == 0) {
        printf("  usage: cat <path>\n");
        return -1;
    }

    /* First try virtual path */
    FsKey vkey;
    if (path_resolve_virtual(ctx, pc, path, &vkey) == 0) {
        char vbuf[512];
        if (path_render_virtual(NULL, ctx, vkey, vbuf, sizeof(vbuf)) == 0) {
            printf("%s", vbuf);
            return 0;
        }
    }

    /* Regular file: open, read, print, close */
    int fd = fd_open(ctx, pid, path, O_READ);
    if (fd < 0) {
        printf("  cat: cannot open '%s'\n", path);
        return -1;
    }

    uint8_t buf[256];
    int n = fd_read(ctx, pid, fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("%s", (char *)buf);
        if (buf[n-1] != '\n') printf("\n");
    } else {
        printf("  (empty)\n");
    }

    fd_close(ctx, pid, fd);
    return 0;
}

int cmd_echo(uint32_t pid, const char *text) {
    if (!text) return -1;
    fd_write(NULL, pid, FD_STDOUT, (const uint8_t *)text, (uint16_t)strlen(text));
    fd_write(NULL, pid, FD_STDOUT, (const uint8_t *)"\n", 1);
    return 0;
}

int cmd_hash(EngineContext *ctx) {
    uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    printf("  canvas hash = 0x%08X  tick=%u\n", h, ctx->tick);
    return 0;
}

int cmd_info(EngineContext *ctx, ProcTable *pt) {
    uint32_t h = dk_canvas_hash(ctx->cells, ctx->cells_count);
    int gates = 0;
    for (int i = 0; i < TILE_COUNT; i++)
        if (gate_is_open_tile(ctx, (uint16_t)i)) gates++;
    printf("  tick=%u  hash=0x%08X\n", ctx->tick, h);
    printf("  gates=%d/%d  procs=%u/%u\n", gates, TILE_COUNT, pt->count, PROC8_MAX);
    printf("  canvas=%ux%u=%u cells (%zu MB)\n",
           CANVAS_W, CANVAS_H, CANVAS_W * CANVAS_H,
           sizeof(Cell) * CANVAS_W * CANVAS_H / (1024 * 1024));
    return 0;
}
