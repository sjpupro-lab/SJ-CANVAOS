/*
 * tervas_projection.c — Cell visibility & TvFrame builder (Spec §11)
 *
 * [R-3] No layer system — condition-based filter only
 * [R-6] Integer arithmetic only
 */
#include "../../include/tervas_projection.h"
#include "../../include/tervas_core.h"
#include "../../include/tervas_render_cell.h"
#include "../../include/canvasos_types.h"
#include "../../include/engine_time.h"
#include <string.h>

/* ── Region geometry ──────────────────────────────────────────────── */

bool tv_is_wh_cell(uint32_t x, uint32_t y) {
    return x >= WH_X0 && x < WH_X0 + WH_W &&
           y >= WH_Y0 && y < WH_Y0 + WH_H;
}

bool tv_is_bh_cell(uint32_t x, uint32_t y) {
    return x >= BH_X0 && x < BH_X0 + BH_W &&
           y >= BH_Y0 && y < BH_Y0 + BH_H;
}

/* ── Set matching ─────────────────────────────────────────────────── */

bool tv_match_a(uint32_t a, const TvFilter *f) {
    for (int i = 0; i < f->a_count; i++)
        if (f->a_values[i] == a) return true;
    return false;
}

bool tv_match_b(uint8_t b, const TvFilter *f) {
    for (int i = 0; i < f->b_count; i++)
        if (f->b_values[i] == b) return true;
    return false;
}

/* ── Single cell visibility ───────────────────────────────────────── */

bool tv_cell_visible(uint32_t x, uint32_t y, const Cell *c,
                     const TvFilter *f, const uint8_t *gates) {
    (void)gates;
    switch (f->mode) {
    case TV_PROJ_ALL:
        return true;
    case TV_PROJ_A:
        return tv_match_a(c->A, f);
    case TV_PROJ_B:
        return tv_match_b(c->B, f);
    case TV_PROJ_AB_UNION:
        return tv_match_a(c->A, f) || tv_match_b(c->B, f);
    case TV_PROJ_AB_OVERLAP:
        return tv_match_a(c->A, f) && tv_match_b(c->B, f);
    case TV_PROJ_WH:
        return tv_is_wh_cell(x, y);
    case TV_PROJ_BH:
        return tv_is_bh_cell(x, y);
    default:
        return true;
    }
}

/* ── Style bits ───────────────────────────────────────────────────── */

static uint8_t compute_style(uint32_t x, uint32_t y, const Cell *c,
                             const TvFilter *f, const uint8_t *gates) {
    uint8_t s = TV_STYLE_NORMAL;
    if (tv_is_wh_cell(x, y)) s |= TV_STYLE_WH;
    if (tv_is_bh_cell(x, y)) s |= TV_STYLE_BH;
    if (f->a_count > 0 && tv_match_a(c->A, f)) s |= TV_STYLE_A_MATCH;
    if (f->b_count > 0 && tv_match_b(c->B, f)) s |= TV_STYLE_B_MATCH;
    if ((s & TV_STYLE_A_MATCH) && (s & TV_STYLE_B_MATCH)) s |= TV_STYLE_OVERLAP;
    /* gate status */
    uint32_t tile_x = x / 32;
    uint32_t tile_y = y / 32;
    uint32_t tile_id = tile_y * (CANVAS_W / 32) + tile_x;
    if (tile_id < TILE_COUNT && gates[tile_id]) s |= TV_STYLE_GATE_ON;
    /* inactive dim */
    if (c->B == 0 && c->G == 0) s |= TV_STYLE_INACTIVE;
    return s;
}

/* ── Frame builder (Spec §11) ─────────────────────────────────────
 *
 * Scans the snapshot canvas, applies projection filter,
 * and fills TvFrame with up to TV_FRAME_MAX_CELLS results.
 *
 * view_cols × view_rows determines sampling stride (zoom).
 * Statistics (wh_active, bh_active, total_visible) are always
 * computed over the FULL snapshot regardless of view size.
 * ──────────────────────────────────────────────────────────────────*/

int tv_build_frame(TvFrame *frame, const TvSnapshot *snap,
                   const TvFilter *f,
                   uint32_t view_cols, uint32_t view_rows) {
    if (!frame || !snap || !f || !snap->valid) return TV_ERR_NULL;
    memset(frame, 0, sizeof(*frame));
    frame->tick = snap->tick;

    uint32_t sw = snap->width;
    uint32_t sh = snap->height;
    uint32_t vx0 = snap->viewport.x0;
    uint32_t vy0 = snap->viewport.y0;

    /* Full-scan pass: compute stats over entire snapshot */
    uint32_t total_vis = 0, wh_act = 0, bh_act = 0;
    for (uint32_t sy = 0; sy < sh; sy++) {
        for (uint32_t sx = 0; sx < sw; sx++) {
            uint32_t cx = vx0 + sx;
            uint32_t cy = vy0 + sy;
            const Cell *c = &snap->canvas[sy * sw + sx];
            if (tv_cell_visible(cx, cy, c, f, snap->gates))
                total_vis++;
            if (tv_is_wh_cell(cx, cy) && c->G > 0) wh_act++;
            if (tv_is_bh_cell(cx, cy) && c->G > 0) bh_act++;
        }
    }
    frame->total_visible = total_vis;
    frame->wh_active = wh_act;
    frame->bh_active = bh_act;

    /* Sampled pass: fill frame cells for the given view resolution */
    if (view_cols == 0) view_cols = 1;
    if (view_rows == 0) view_rows = 1;
    uint32_t step_x = sw / view_cols; if (step_x == 0) step_x = 1;
    uint32_t step_y = sh / view_rows; if (step_y == 0) step_y = 1;

    uint32_t idx = 0;
    for (uint32_t vy = 0; vy < view_rows && idx < TV_FRAME_MAX_CELLS; vy++) {
        uint32_t sy = vy * step_y;
        if (sy >= sh) break;
        for (uint32_t vx = 0; vx < view_cols && idx < TV_FRAME_MAX_CELLS; vx++) {
            uint32_t sx = vx * step_x;
            if (sx >= sw) break;
            uint32_t cx = vx0 + sx;
            uint32_t cy = vy0 + sy;
            const Cell *c = &snap->canvas[sy * sw + sx];
            bool vis = tv_cell_visible(cx, cy, c, f, snap->gates);
            TvRenderCell *rc = &frame->cells[idx++];
            rc->x = cx;
            rc->y = cy;
            rc->visible = vis ? 1 : 0;
            rc->style = compute_style(cx, cy, c, f, snap->gates);
            rc->a = c->A;
            rc->b = c->B;
            rc->g = c->G;
            rc->r = c->R;
        }
    }
    frame->count = idx;
    return TV_OK;
}
