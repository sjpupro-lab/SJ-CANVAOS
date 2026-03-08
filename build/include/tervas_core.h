#ifndef TERVAS_CORE_H
#define TERVAS_CORE_H
/*
 * tervas_core.h — CanvasOS Phase-7 Tervas Canvas Terminal
 *
 * FIXED RULES (immutable across all future phases):
 *   [R-1] Y-axis = time axis  (Y↑ = past, Y↓ = future)
 *   [R-2] 4-quadrant layout:  Q0(+x,+y) Q1(-x,+y) Q2(-x,-y) Q3(-x,+y)
 *   [R-3] No layer system — projection only (condition-based filter)
 *   [R-4] Tervas is READ-ONLY — never modifies engine state
 *   [R-5] Snapshot only at tick boundary (after merge_tick)
 *   [R-6] Integer arithmetic only in projection paths (DK-2 inherited)
 */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"

/* ── Limits ──────────────────────────────────────────────────────── */
#define TV_MAX_A          64
#define TV_MAX_B          64
#define TV_ZOOM_MIN        1
#define TV_ZOOM_MAX        8
#define TV_PAN_MAX      1023   /* canvas coordinate bound */
#define TV_TICK_CLAMP   0xFFFFFFFFu

/* ── Renderer backend tags (for future upgrade path) ──────────────
 *  TV_RENDER_ASCII    : current — 64×32 ANSI terminal
 *  TV_RENDER_NCURSES  : upgrade 1 — color + mouse (ncurses)
 *  TV_RENDER_SDL2     : upgrade 2 — pixel-accurate, GPU-blitted
 *  TV_RENDER_OPENGL   : upgrade 3 — shader-driven, 1:1 cell mapping
 * ─────────────────────────────────────────────────────────────────*/
typedef enum {
    TV_RENDER_ASCII   = 0,
    TV_RENDER_NCURSES = 1,
    TV_RENDER_SDL2    = 2,
    TV_RENDER_OPENGL  = 3,
} TvRendererBackend;

/* ── Projection modes ────────────────────────────────────────────── */
typedef enum {
    TV_PROJ_ALL        = 0,
    TV_PROJ_A          = 1,
    TV_PROJ_B          = 2,
    TV_PROJ_AB_UNION   = 3,
    TV_PROJ_AB_OVERLAP = 4,
    TV_PROJ_WH         = 5,
    TV_PROJ_BH         = 6,
} TvProjectionMode;

/* ── Snapshot modes ──────────────────────────────────────────────── */
typedef enum {
    TV_SNAP_FULL    = 0,  /* full 8MB copy  — accuracy first          */
    TV_SNAP_WINDOW  = 1,  /* viewport window only — performance first  */
    TV_SNAP_COMPACT = 2,  /* active-cells-only sparse buffer           */
} TvSnapshotMode;

/* ── Viewport (window into canvas) ──────────────────────────────── */
typedef struct {
    uint32_t x0, y0;    /* top-left corner in canvas coords           */
    uint32_t w,  h;     /* width / height  (≤ CANVAS_W / CANVAS_H)    */
} TvViewport;

static inline TvViewport tv_viewport_full(void) {
    return (TvViewport){ 0, 0, CANVAS_W, CANVAS_H };
}
static inline TvViewport tv_viewport_wh(void) {  /* WH region */
    return (TvViewport){ 512, 512, 512, 128 };
}
static inline TvViewport tv_viewport_bh(void) {  /* BH region */
    return (TvViewport){ 512, 640, 512, 64  };
}

/* ── Filter ──────────────────────────────────────────────────────── */
typedef struct {
    uint32_t         a_values[TV_MAX_A];
    int              a_count;
    uint8_t          b_values[TV_MAX_B];
    int              b_count;
    TvProjectionMode mode;
    uint32_t         tick;
    int              zoom;
    int              pan_x;
    int              pan_y;
    TvViewport       viewport;
    TvSnapshotMode   snap_mode;
} TvFilter;

/* ── Snapshot ─────────────────────────────────────────────────────
 * canvas: heap-allocated. Size depends on snap_mode:
 *   FULL    → CANVAS_W * CANVAS_H cells  (8 MB)
 *   WINDOW  → viewport.w * viewport.h    (variable, << 8 MB)
 *   COMPACT → active_count cells         (sparse)
 * ─────────────────────────────────────────────────────────────────*/
typedef struct {
    Cell        *canvas;       /* heap-allocated               */
    uint32_t     canvas_cap;   /* allocated cell count         */
    uint8_t      gates[TILE_COUNT];
    uint32_t     width;        /* logical canvas width         */
    uint32_t     height;       /* logical canvas height        */
    TvViewport   viewport;     /* captured window              */
    TvSnapshotMode snap_mode;
    uint32_t     tick;
    bool         valid;
} TvSnapshot;

/* ── Error codes ─────────────────────────────────────────────────── */
typedef enum {
    TV_OK            =  0,
    TV_ERR_NULL      = -1,
    TV_ERR_OOB       = -2,   /* coordinate out of bounds      */
    TV_ERR_NO_REGION = -3,   /* named region not found        */
    TV_ERR_TICK_OOB  = -4,   /* tick outside WH window        */
    TV_ERR_OVERFLOW  = -5,   /* A/B set full                  */
    TV_ERR_ZOOM      = -6,
    TV_ERR_ALLOC     = -7,
    TV_ERR_CMD       = -8,   /* unknown command               */
} TvError;

/* ── Tervas ──────────────────────────────────────────────────────── */
typedef struct {
    TvSnapshot       snapshot;
    TvFilter         filter;
    bool             running;
    TvError          last_err;
    char             status_msg[256];
    TvRendererBackend renderer_backend;
} Tervas;

/* lifecycle */
int  tervas_init(Tervas *tv);
void tervas_free(Tervas *tv);

void tv_filter_reset(TvFilter *f);

#endif /* TERVAS_CORE_H */
