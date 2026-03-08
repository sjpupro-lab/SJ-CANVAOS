/*
 * tervas_cli.c — Tervas CLI command parser (Spec §8)
 *
 * Uses dispatch table from tervas_dispatch.h.
 * Each command follows: CLI → Bridge → Projection → Renderer
 */
#include "../../include/tervas_cli.h"
#include "../../include/tervas_bridge.h"
#include "../../include/tervas_dispatch.h"
#include "../../include/tervas_projection.h"
#include "../../include/canvasos_engine_ctx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tv_cli_print_help(void) {
    printf("Tervas commands:\n");
    for (int i = 0; TV_DISPATCH_TABLE[i].prefix; i++)
        printf("  %-22s %s\n", TV_DISPATCH_TABLE[i].prefix,
               TV_DISPATCH_TABLE[i].desc);
}

/* parse "x y" from args, returns 0 on success */
static int parse_xy(const char *s, uint32_t *x, uint32_t *y) {
    char *end1, *end2;
    long lx = strtol(s, &end1, 10);
    if (end1 == s || *end1 != ' ') return -1;
    long ly = strtol(end1 + 1, &end2, 10);
    if (end2 == end1 + 1) return -1;
    if (lx < 0 || ly < 0) return -1;
    *x = (uint32_t)lx;
    *y = (uint32_t)ly;
    return 0;
}

int tv_cli_exec(Tervas *tv, EngineContext *eng, const char *line) {
    if (!tv || !eng || !line) return TV_ERR_NULL;

    /* skip leading whitespace */
    while (*line == ' ') line++;
    if (*line == '\0') return TV_OK;

    /* ── dispatch table lookup ── */
    const TvCmdDispatch *d = tv_dispatch_find(line);

    /* handle known commands */

    /* view all */
    if (strncmp(line, "view all", 8) == 0) {
        tv->filter.mode = TV_PROJ_ALL;
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    /* view a <values> */
    if (strncmp(line, "view a ", 7) == 0) {
        const char *args = line + 7;
        tv->filter.a_count = 0;
        tv->filter.mode = TV_PROJ_A;
        char *end;
        while (*args && tv->filter.a_count < TV_MAX_A) {
            long v = strtol(args, &end, 0);
            if (end == args) break;
            tv->filter.a_values[tv->filter.a_count++] = (uint32_t)v;
            args = end;
            while (*args == ' ' || *args == ',') args++;
        }
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    /* view b <values> */
    if (strncmp(line, "view b ", 7) == 0) {
        const char *args = line + 7;
        tv->filter.b_count = 0;
        tv->filter.mode = TV_PROJ_B;
        char *end;
        while (*args && tv->filter.b_count < TV_MAX_B) {
            long v = strtol(args, &end, 0);
            if (end == args) break;
            tv->filter.b_values[tv->filter.b_count++] = (uint8_t)v;
            args = end;
            while (*args == ' ' || *args == ',') args++;
        }
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    /* view ab-union / ab-overlap */
    if (strncmp(line, "view ab-union", 13) == 0) {
        tv->filter.mode = TV_PROJ_AB_UNION;
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    if (strncmp(line, "view ab-overlap", 15) == 0) {
        tv->filter.mode = TV_PROJ_AB_OVERLAP;
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    /* view wh / bh */
    if (strncmp(line, "view wh", 7) == 0) {
        tv->filter.mode = TV_PROJ_WH;
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    if (strncmp(line, "view bh", 7) == 0) {
        tv->filter.mode = TV_PROJ_BH;
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }

    /* inspect x y */
    if (strncmp(line, "inspect ", 8) == 0) {
        uint32_t x, y;
        if (parse_xy(line + 8, &x, &y) != 0) return TV_ERR_OOB;
        if (x >= CANVAS_W || y >= CANVAS_H) return TV_ERR_OOB;
        char buf[256];
        int rc = tervas_bridge_inspect(eng, x, y, buf, sizeof(buf));
        if (rc == TV_OK)
            snprintf(tv->status_msg, sizeof(tv->status_msg), "%s", buf);
        return rc;
    }

    /* tick now */
    if (strcmp(line, "tick now") == 0) {
        tv->filter.tick = eng->tick;
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }
    /* tick goto <n> */
    if (strncmp(line, "tick goto ", 10) == 0) {
        char *end;
        long t = strtol(line + 10, &end, 10);
        if (end == line + 10) t = 0;
        if (t < 0) t = 0;
        uint32_t tick = (uint32_t)t;
        if (tick > eng->tick) tick = eng->tick; /* clamp */
        tv->filter.tick = tick;
        return tervas_bridge_snapshot(tv, eng, tick);
    }

    /* region <name> */
    if (strncmp(line, "region ", 7) == 0) {
        return tervas_bridge_region(tv, eng, line + 7);
    }

    /* snap <mode> */
    if (strncmp(line, "snap ", 5) == 0) {
        const char *m = line + 5;
        if (strcmp(m, "full") == 0) { tv->filter.snap_mode = TV_SNAP_FULL; return TV_OK; }
        if (strcmp(m, "win") == 0 || strcmp(m, "window") == 0) {
            tv->filter.snap_mode = TV_SNAP_WINDOW; return TV_OK;
        }
        if (strcmp(m, "compact") == 0) { tv->filter.snap_mode = TV_SNAP_COMPACT; return TV_OK; }
        return TV_ERR_CMD;
    }

    /* zoom <level> */
    if (strncmp(line, "zoom ", 5) == 0) {
        int z = atoi(line + 5);
        if (z < TV_ZOOM_MIN || z > TV_ZOOM_MAX) return TV_ERR_ZOOM;
        tv->filter.zoom = z;
        return TV_OK;
    }

    /* pan <dx> <dy> */
    if (strncmp(line, "pan ", 4) == 0) {
        uint32_t dx, dy;
        if (parse_xy(line + 4, &dx, &dy) != 0) return TV_ERR_OOB;
        if (dx > TV_PAN_MAX || dy > TV_PAN_MAX) return TV_ERR_OOB;
        tv->filter.pan_x = (int)dx;
        tv->filter.pan_y = (int)dy;
        tv->filter.viewport.x0 = dx;
        tv->filter.viewport.y0 = dy;
        return TV_OK;
    }

    /* refresh */
    if (strcmp(line, "refresh") == 0) {
        return tervas_bridge_snapshot(tv, eng, eng->tick);
    }

    /* help */
    if (strcmp(line, "help") == 0) {
        tv_cli_print_help();
        return TV_OK;
    }

    /* quit */
    if (strcmp(line, "quit") == 0) {
        tv->running = false;
        return TV_OK;
    }

    /* ── quick 명령군 (Spec §12) ── */
    if (strncmp(line, "quick ", 6) == 0) {
        const char *sub = line + 6;
        if (strcmp(sub, "wh") == 0) {
            tv->filter.mode = TV_PROJ_WH;
            return tervas_bridge_snapshot(tv, eng, eng->tick);
        }
        if (strcmp(sub, "bh") == 0) {
            tv->filter.mode = TV_PROJ_BH;
            return tervas_bridge_snapshot(tv, eng, eng->tick);
        }
        if (strcmp(sub, "all") == 0) {
            tv->filter.mode = TV_PROJ_ALL;
            return tervas_bridge_snapshot(tv, eng, eng->tick);
        }
        if (strcmp(sub, "overlap") == 0) {
            tv->filter.mode = TV_PROJ_AB_OVERLAP;
            return tervas_bridge_snapshot(tv, eng, eng->tick);
        }
        if (strcmp(sub, "now") == 0) {
            tv->filter.tick = eng->tick;
            tv->filter.mode = TV_PROJ_ALL;
            return tervas_bridge_snapshot(tv, eng, eng->tick);
        }
        if (strncmp(sub, "inspect ", 8) == 0) {
            uint32_t x, y;
            if (parse_xy(sub + 8, &x, &y) != 0) return TV_ERR_OOB;
            if (x >= CANVAS_W || y >= CANVAS_H) return TV_ERR_OOB;
            char buf[256];
            return tervas_bridge_inspect(eng, x, y, buf, sizeof(buf));
        }
        if (strncmp(sub, "region ", 7) == 0) {
            return tervas_bridge_region(tv, eng, sub + 7);
        }
        return TV_ERR_CMD;
    }

    /* unknown command */
    return TV_ERR_CMD;
}
