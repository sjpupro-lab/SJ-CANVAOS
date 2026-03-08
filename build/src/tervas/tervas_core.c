/*
 * tervas_core.c — Tervas lifecycle (init / free / filter_reset)
 */
#include "../../include/tervas_core.h"
#include <stdlib.h>
#include <string.h>

int tervas_init(Tervas *tv) {
    if (!tv) return TV_ERR_NULL;
    memset(tv, 0, sizeof(*tv));
    tv_filter_reset(&tv->filter);
    tv->running = false;
    tv->last_err = TV_OK;
    tv->renderer_backend = TV_RENDER_ASCII;
    tv->snapshot.valid = false;
    tv->snapshot.canvas = NULL;
    tv->snapshot.canvas_cap = 0;
    snprintf(tv->status_msg, sizeof(tv->status_msg), "tervas ready");
    return TV_OK;
}

void tervas_free(Tervas *tv) {
    if (!tv) return;
    free(tv->snapshot.canvas);
    tv->snapshot.canvas = NULL;
    tv->snapshot.canvas_cap = 0;
    tv->snapshot.valid = false;
}

void tv_filter_reset(TvFilter *f) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->mode = TV_PROJ_ALL;
    f->zoom = 1;
    f->snap_mode = TV_SNAP_FULL;
    f->viewport = tv_viewport_full();
}
