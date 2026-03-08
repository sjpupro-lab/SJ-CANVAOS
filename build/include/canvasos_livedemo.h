#pragma once
/*
 * canvasos_livedemo.h — Patch-F: Live Demo Panel System
 *
 * Provides heatmap projection, panel rendering, and live demo
 * integration for CanvasOS's core features:
 *   - Canvas grid with heatmap highlighting
 *   - VM activity indicator
 *   - Timeline / branch / snapshot panels
 *   - Timewarp replay cursor
 *   - File I/O hotspot tracking
 */
#include "canvasos_engine_ctx.h"
#include "canvasos_timeline.h"
#include "canvasos_proc.h"
#include "canvasos_vm.h"

/* ── Heatmap (recent modification tracking) ──────────── */
#define HEAT_WINDOW  8  /* ticks within which a cell is "hot" */
#define DEMO_VIEW_W  32 /* demo viewport width in cells */
#define DEMO_VIEW_H  16 /* demo viewport height in cells */

typedef struct {
    uint32_t last_tick[DEMO_VIEW_W * DEMO_VIEW_H];
    uint8_t  heat[DEMO_VIEW_W * DEMO_VIEW_H];  /* 0=cold .. 5=hot */
} Heatmap;

/* ── Demo Panel (composited output buffer) ───────────── */
#define PANEL_BUF_MAX 4096

typedef struct {
    char     buf[PANEL_BUF_MAX];
    uint32_t len;
} PanelBuf;

/* ── Live Demo State ─────────────────────────────────── */
typedef struct {
    /* Viewport into canvas */
    uint32_t  view_x, view_y;
    uint32_t  view_w, view_h;

    /* Heatmap */
    Heatmap   heat;

    /* Panel buffers (built by render functions) */
    PanelBuf  grid_panel;
    PanelBuf  status_panel;
    PanelBuf  timeline_panel;
    PanelBuf  vm_panel;

    /* Stats */
    uint32_t  active_tiles;
    uint32_t  open_gates;
    uint32_t  modified_cells;
    uint32_t  frame_count;
} LiveDemo;

/* ── API ─────────────────────────────────────────────── */

/* Init / lifecycle */
void demo_init(LiveDemo *demo, uint32_t vx, uint32_t vy,
               uint32_t vw, uint32_t vh);

/* Heatmap */
void demo_heatmap_update(LiveDemo *demo, const EngineContext *ctx);
int  demo_heatmap_get(const LiveDemo *demo, uint32_t lx, uint32_t ly);

/* Panel rendering (writes to PanelBuf, does NOT print) */
int  demo_render_grid(LiveDemo *demo, const EngineContext *ctx);
int  demo_render_status(LiveDemo *demo, const EngineContext *ctx,
                        const Timeline *tl);
int  demo_render_timeline(LiveDemo *demo, const Timeline *tl,
                          const EngineContext *ctx);
int  demo_render_vm(LiveDemo *demo, const VmState *vm);

/* Composite: render all panels to stdout */
int  demo_render_frame(LiveDemo *demo, const EngineContext *ctx,
                       const Timeline *tl, const VmState *vm,
                       const ProcTable *pt);

/* Utility: gate/tile stats */
void demo_compute_stats(LiveDemo *demo, const EngineContext *ctx);

/* Panel buffer helpers */
void panel_clear(PanelBuf *p);
int  panel_append(PanelBuf *p, const char *fmt, ...);
const char *panel_str(const PanelBuf *p);
