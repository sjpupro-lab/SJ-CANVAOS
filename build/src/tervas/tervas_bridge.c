/*
 * tervas_bridge.c — Engine ↔ Tervas bridge (READ-ONLY, R-4)
 *
 * attach:   configure filter defaults from engine state
 * snapshot: copy canvas/gates into TvSnapshot (never modifies engine)
 * inspect:  single-cell lookup with bounds check
 * region:   named region → viewport
 */
#include "../../include/tervas_bridge.h"
#include "../../include/tervas_core.h"
#include "../../include/canvasos_engine_ctx.h"
#include "../../include/canvasos_gate_ops.h"
#include "../../include/engine_time.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

int tervas_bridge_attach(Tervas *tv, EngineContext *eng) {
    if (!tv || !eng) return TV_ERR_NULL;
    tv->filter.tick = eng->tick;
    tv->filter.viewport = tv_viewport_full();
    snprintf(tv->status_msg, sizeof(tv->status_msg),
             "attached tick=%u", eng->tick);
    return TV_OK;
}

int tervas_bridge_snapshot(Tervas *tv, EngineContext *eng, uint32_t tick) {
    if (!tv || !eng) return TV_ERR_NULL;

    TvSnapshot *snap = &tv->snapshot;
    uint32_t need_cells;
    uint32_t vw, vh;

    switch (tv->filter.snap_mode) {
    case TV_SNAP_WINDOW:
        vw = tv->filter.viewport.w;
        vh = tv->filter.viewport.h;
        if (vw == 0 || vh == 0) return TV_ERR_OOB;
        need_cells = vw * vh;
        break;
    case TV_SNAP_FULL:
    default:
        vw = CANVAS_W;
        vh = CANVAS_H;
        need_cells = CANVAS_W * CANVAS_H;
        break;
    }

    /* (re)alloc if needed */
    if (snap->canvas_cap < need_cells) {
        free(snap->canvas);
        snap->canvas = (Cell *)malloc(sizeof(Cell) * need_cells);
        if (!snap->canvas) { snap->canvas_cap = 0; return TV_ERR_ALLOC; }
        snap->canvas_cap = need_cells;
    }

    /* copy canvas data (READ-ONLY from engine) */
    if (tv->filter.snap_mode == TV_SNAP_WINDOW) {
        uint32_t x0 = tv->filter.viewport.x0;
        uint32_t y0 = tv->filter.viewport.y0;
        for (uint32_t dy = 0; dy < vh; dy++) {
            uint32_t sy = y0 + dy;
            if (sy >= CANVAS_H) sy = CANVAS_H - 1;
            for (uint32_t dx = 0; dx < vw; dx++) {
                uint32_t sx = x0 + dx;
                if (sx >= CANVAS_W) sx = CANVAS_W - 1;
                snap->canvas[dy * vw + dx] = eng->cells[sy * CANVAS_W + sx];
            }
        }
    } else {
        memcpy(snap->canvas, eng->cells, sizeof(Cell) * need_cells);
    }

    /* copy gate states */
    for (int i = 0; i < TILE_COUNT; i++)
        snap->gates[i] = gate_is_open_tile(eng, (uint16_t)i) ? 1 : 0;

    snap->width = vw;
    snap->height = vh;
    snap->viewport = tv->filter.viewport;
    snap->snap_mode = tv->filter.snap_mode;
    snap->tick = tick;
    snap->valid = true;

    tv->filter.tick = tick;
    snprintf(tv->status_msg, sizeof(tv->status_msg),
             "snap tick=%u %ux%u mode=%d", tick, vw, vh, tv->filter.snap_mode);
    return TV_OK;
}

int tervas_bridge_inspect(EngineContext *eng, uint32_t x, uint32_t y,
                          char *buf, size_t buflen) {
    if (!eng || !buf) return TV_ERR_NULL;
    if (x >= CANVAS_W || y >= CANVAS_H) return TV_ERR_OOB;
    Cell c = eng->cells[y * CANVAS_W + x];
    snprintf(buf, buflen,
             "(%u,%u) A=%08X B=%02X G=%u R=%02X",
             x, y, c.A, c.B, c.G, c.R);
    return TV_OK;
}

int tervas_bridge_region(Tervas *tv, EngineContext *eng, const char *name) {
    if (!tv || !eng || !name) return TV_ERR_NULL;
    if (strcmp(name, "wh") == 0) {
        tv->filter.viewport = tv_viewport_wh();
        tv->filter.mode = TV_PROJ_WH;
    } else if (strcmp(name, "bh") == 0) {
        tv->filter.viewport = tv_viewport_bh();
        tv->filter.mode = TV_PROJ_BH;
    } else if (strcmp(name, "full") == 0 || strcmp(name, "all") == 0) {
        tv->filter.viewport = tv_viewport_full();
        tv->filter.mode = TV_PROJ_ALL;
    } else {
        return TV_ERR_NO_REGION;
    }
    /* re-snapshot with new viewport if windowed */
    return tervas_bridge_snapshot(tv, eng, eng->tick);
}
