/*
 * livedemo.c — Patch-F: Live Demo Panel Rendering
 *
 * Projection → Heatmap → Panel buffers → Composite output
 * Follows Tervas [R-4]: READ-ONLY — never modifies engine state
 */
#include "../include/canvasos_livedemo.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvas_determinism.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* ── ANSI color codes ────────────────────────────────── */
#define A_RESET  "\033[0m"
#define A_BOLD   "\033[1m"
#define A_DIM    "\033[2m"
#define A_RED    "\033[31m"
#define A_GREEN  "\033[32m"
#define A_YELLOW "\033[33m"
#define A_BLUE   "\033[34m"
#define A_MAG    "\033[35m"
#define A_CYAN   "\033[36m"
#define A_BG_RED "\033[41m"

/* ── Panel buffer helpers ────────────────────────────── */
void panel_clear(PanelBuf *p) {
    p->len = 0;
    p->buf[0] = '\0';
}

int panel_append(PanelBuf *p, const char *fmt, ...) {
    if (p->len >= PANEL_BUF_MAX - 1) return -1;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(p->buf + p->len, PANEL_BUF_MAX - p->len, fmt, ap);
    va_end(ap);
    if (n > 0) p->len += (uint32_t)n;
    return n;
}

const char *panel_str(const PanelBuf *p) {
    return p->buf;
}

/* ── Init ────────────────────────────────────────────── */
void demo_init(LiveDemo *demo, uint32_t vx, uint32_t vy,
               uint32_t vw, uint32_t vh) {
    memset(demo, 0, sizeof(*demo));
    demo->view_x = vx;
    demo->view_y = vy;
    demo->view_w = vw < DEMO_VIEW_W ? vw : DEMO_VIEW_W;
    demo->view_h = vh < DEMO_VIEW_H ? vh : DEMO_VIEW_H;
}

/* ── Heatmap ─────────────────────────────────────────── */
void demo_heatmap_update(LiveDemo *demo, const EngineContext *ctx) {
    if (!demo || !ctx) return;

    for (uint32_t ly = 0; ly < demo->view_h; ly++) {
        for (uint32_t lx = 0; lx < demo->view_w; lx++) {
            uint32_t cx = demo->view_x + lx;
            uint32_t cy = demo->view_y + ly;
            if (cx >= CANVAS_W || cy >= CANVAS_H) continue;

            uint32_t idx = cy * CANVAS_W + cx;
            uint32_t lidx = ly * demo->view_w + lx;
            const Cell *c = &ctx->cells[idx];

            /* Detect activity: non-zero B/G/R */
            if (c->B != 0 || c->G != 0 || c->R != 0) {
                demo->heat.last_tick[lidx] = ctx->tick;
                int age = 0; /* just modified */
                demo->heat.heat[lidx] = (uint8_t)(HEAT_WINDOW - age);
                if (demo->heat.heat[lidx] > HEAT_WINDOW)
                    demo->heat.heat[lidx] = HEAT_WINDOW;
            } else {
                /* Decay heat based on tick distance */
                uint32_t last = demo->heat.last_tick[lidx];
                if (last > 0 && ctx->tick > last) {
                    uint32_t age = ctx->tick - last;
                    demo->heat.heat[lidx] = (age < HEAT_WINDOW)
                        ? (uint8_t)(HEAT_WINDOW - age) : 0;
                }
            }
        }
    }
}

int demo_heatmap_get(const LiveDemo *demo, uint32_t lx, uint32_t ly) {
    if (!demo || lx >= demo->view_w || ly >= demo->view_h) return 0;
    return (int)demo->heat.heat[ly * demo->view_w + lx];
}

/* ── Stats ───────────────────────────────────────────── */
void demo_compute_stats(LiveDemo *demo, const EngineContext *ctx) {
    if (!demo || !ctx) return;
    demo->open_gates = 0;
    demo->active_tiles = 0;
    demo->modified_cells = 0;

    for (uint32_t i = 0; i < TILE_COUNT; i++) {
        if (gate_is_open_tile(ctx, (uint16_t)i)) {
            demo->open_gates++;
            demo->active_tiles++;
        }
    }

    /* Count non-zero cells in viewport */
    for (uint32_t ly = 0; ly < demo->view_h; ly++) {
        for (uint32_t lx = 0; lx < demo->view_w; lx++) {
            uint32_t cx = demo->view_x + lx;
            uint32_t cy = demo->view_y + ly;
            if (cx >= CANVAS_W || cy >= CANVAS_H) continue;
            const Cell *c = &ctx->cells[cy * CANVAS_W + cx];
            if (c->B != 0 || c->G != 0 || c->R != 0)
                demo->modified_cells++;
        }
    }
}

/* ── Grid Panel ──────────────────────────────────────── */
static char cell_glyph(const Cell *c, int heat) {
    if (c->B == 0 && c->G == 0 && c->R == 0) return '.';
    if (c->R >= 0x20 && c->R <= 0x7E) return (char)c->R;
    if (c->G > 128) return '#';
    if (c->G > 32)  return '+';
    if (c->G > 0)   return ':';
    if (heat > 3)    return '*';
    return '.';
}

int demo_render_grid(LiveDemo *demo, const EngineContext *ctx) {
    if (!demo || !ctx) return -1;
    panel_clear(&demo->grid_panel);

    demo_heatmap_update(demo, ctx);

    for (uint32_t ly = 0; ly < demo->view_h; ly++) {
        panel_append(&demo->grid_panel, "  ");
        for (uint32_t lx = 0; lx < demo->view_w; lx++) {
            uint32_t cx = demo->view_x + lx;
            uint32_t cy = demo->view_y + ly;
            int heat = demo_heatmap_get(demo, lx, ly);

            if (cx >= CANVAS_W || cy >= CANVAS_H) {
                panel_append(&demo->grid_panel, " ");
                continue;
            }

            const Cell *c = &ctx->cells[cy * CANVAS_W + cx];
            char g = cell_glyph(c, heat);

            if (heat > 5)
                panel_append(&demo->grid_panel, A_BOLD A_YELLOW "%c" A_RESET, g);
            else if (heat > 2)
                panel_append(&demo->grid_panel, A_YELLOW "%c" A_RESET, g);
            else if (g != '.')
                panel_append(&demo->grid_panel, A_GREEN "%c" A_RESET, g);
            else
                panel_append(&demo->grid_panel, A_DIM "." A_RESET);
        }
        panel_append(&demo->grid_panel, "\n");
    }
    return 0;
}

/* ── Status Panel ────────────────────────────────────── */
int demo_render_status(LiveDemo *demo, const EngineContext *ctx,
                       const Timeline *tl) {
    if (!demo || !ctx) return -1;
    panel_clear(&demo->status_panel);

    demo_compute_stats(demo, ctx);
    uint32_t hash = dk_canvas_hash(ctx->cells, ctx->cells_count);

    panel_append(&demo->status_panel,
        "  Tick: " A_CYAN "%u" A_RESET
        "  Hash: " A_YELLOW "%08X" A_RESET
        "  Gates: " A_GREEN "%u" A_RESET "/%u"
        "  Modified: " A_MAG "%u" A_RESET "\n",
        ctx->tick, hash, demo->open_gates, TILE_COUNT,
        demo->modified_cells);

    if (tl) {
        panel_append(&demo->status_panel,
            "  Branch: " A_CYAN "%u" A_RESET
            "  Snapshots: %u  Branches: %u",
            tl->current_branch, tl->snapshots.count, tl->branches.count);
        if (tl->timewarp.active)
            panel_append(&demo->status_panel,
                "  " A_RED "[TIMEWARP → %u]" A_RESET,
                tl->timewarp.target_tick);
        panel_append(&demo->status_panel, "\n");
    }
    return 0;
}

/* ── Timeline Panel ──────────────────────────────────── */
int demo_render_timeline(LiveDemo *demo, const Timeline *tl,
                         const EngineContext *ctx) {
    if (!demo || !tl) return -1;
    panel_clear(&demo->timeline_panel);

    /* Snapshots */
    if (tl->snapshots.count > 0) {
        panel_append(&demo->timeline_panel, "  Snapshots: ");
        for (uint32_t i = 0; i < tl->snapshots.count; i++) {
            const Snapshot *s = &tl->snapshots.snaps[i];
            if (!s->active) continue;
            panel_append(&demo->timeline_panel,
                "[" A_CYAN "%s" A_RESET " t%u] ", s->name, s->tick);
        }
        panel_append(&demo->timeline_panel, "\n");
    }

    /* Branches */
    if (tl->branches.count > 0) {
        panel_append(&demo->timeline_panel, "  Branches:  ");
        for (uint32_t i = 0; i < tl->branches.count; i++) {
            const BranchDesc *b = &tl->branches.branches[i];
            if (b->branch_id == tl->current_branch)
                panel_append(&demo->timeline_panel,
                    A_BOLD A_GREEN "[%u*]" A_RESET " ", b->branch_id);
            else
                panel_append(&demo->timeline_panel, "[%u] ", b->branch_id);
        }
        panel_append(&demo->timeline_panel, "\n");
    }

    /* Timeline bar */
    panel_append(&demo->timeline_panel, "  Timeline:  ");
    uint32_t tick = ctx ? ctx->tick : 0;
    uint32_t bar_len = tick < 40 ? tick : 40;
    for (uint32_t i = 0; i < bar_len; i++) {
        /* Check if a snapshot exists at this point */
        bool has_snap = false;
        for (uint32_t s = 0; s < tl->snapshots.count; s++) {
            uint32_t st = tl->snapshots.snaps[s].tick;
            if (st > 0 && (st * 40 / (tick + 1)) == i)
                has_snap = true;
        }
        if (has_snap)
            panel_append(&demo->timeline_panel, A_YELLOW "◆" A_RESET);
        else
            panel_append(&demo->timeline_panel, "─");
    }
    panel_append(&demo->timeline_panel, A_GREEN "▶" A_RESET "\n");

    return 0;
}

/* ── VM Panel ────────────────────────────────────────── */
int demo_render_vm(LiveDemo *demo, const VmState *vm) {
    if (!demo) return -1;
    panel_clear(&demo->vm_panel);

    if (!vm) {
        panel_append(&demo->vm_panel, "  VM: " A_DIM "inactive" A_RESET "\n");
        return 0;
    }

    panel_append(&demo->vm_panel,
        "  VM: %s  PC(%u,%u)  Steps: %u/%u\n",
        vm->running ? (A_GREEN "RUNNING" A_RESET) : (A_DIM "HALTED" A_RESET),
        vm->pc_x, vm->pc_y, vm->tick_count, vm->tick_limit);

    panel_append(&demo->vm_panel,
        "  A=%08X B=%02X G=%u R=%02X Flag=%u SP=%u\n",
        vm->reg_A, vm->reg_B, vm->reg_G, vm->reg_R,
        vm->flag, vm->sp);

    return 0;
}

/* ═══════════════════════════════════════════════════════
 * Composite Frame Renderer
 *
 * Assembles all panels and prints to stdout.
 * Layout:
 *   ┌─── Header ──────────────────────────┐
 *   │  Canvas Grid        │ VM Status     │
 *   │                     │               │
 *   ├─── Status ──────────────────────────┤
 *   ├─── Timeline ────────────────────────┤
 *   └─── Commands ────────────────────────┘
 * ═══════════════════════════════════════════════════════ */
int demo_render_frame(LiveDemo *demo, const EngineContext *ctx,
                      const Timeline *tl, const VmState *vm,
                      const ProcTable *pt) {
    if (!demo || !ctx) return -1;
    (void)pt; /* reserved for future proc panel */

    /* Build all panels */
    demo_render_grid(demo, ctx);
    demo_render_status(demo, ctx, tl);
    if (tl) demo_render_timeline(demo, tl, ctx);
    demo_render_vm(demo, vm);

    /* Composite output */
    printf(A_BOLD "╔═══ CanvasOS Live Demo ══════════════════════════════╗\n" A_RESET);

    /* Grid */
    printf(A_BOLD "║" A_RESET " Canvas (%u,%u) %ux%u\n",
           demo->view_x, demo->view_y, demo->view_w, demo->view_h);
    printf("%s", panel_str(&demo->grid_panel));

    /* Status */
    printf(A_BOLD "╠═══ Status ═════════════════════════════════════════╣\n" A_RESET);
    printf("%s", panel_str(&demo->status_panel));

    /* Timeline */
    if (tl && demo->timeline_panel.len > 0) {
        printf(A_BOLD "╠═══ Timeline ═══════════════════════════════════════╣\n" A_RESET);
        printf("%s", panel_str(&demo->timeline_panel));
    }

    /* VM */
    if (demo->vm_panel.len > 0) {
        printf(A_BOLD "╠═══ VM ═════════════════════════════════════════════╣\n" A_RESET);
        printf("%s", panel_str(&demo->vm_panel));
    }

    printf(A_BOLD "╚════════════════════════════════════════════════════╝\n" A_RESET);

    demo->frame_count++;
    return 0;
}
