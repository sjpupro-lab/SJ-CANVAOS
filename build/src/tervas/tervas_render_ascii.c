/*
 * tervas_render_ascii.c — ASCII terminal renderer (Spec §11)
 *
 * Consumes TvFrame produced by tv_build_frame().
 * Renderer makes NO decisions — only draws what Projection decided.
 */
#include "../../include/tervas_render.h"
#include "../../include/tervas_render_cell.h"
#include "../../include/tervas_core.h"
#include <stdio.h>

/* ANSI color helpers */
static const char *style_color(uint8_t style) {
    if (style & TV_STYLE_OVERLAP)  return "\033[1;35m"; /* magenta bold */
    if (style & TV_STYLE_WH)       return "\033[36m";    /* cyan */
    if (style & TV_STYLE_BH)       return "\033[31m";    /* red */
    if (style & TV_STYLE_A_MATCH)  return "\033[33m";    /* yellow */
    if (style & TV_STYLE_B_MATCH)  return "\033[32m";    /* green */
    if (style & TV_STYLE_GATE_ON)  return "\033[1;37m";  /* bright white */
    if (style & TV_STYLE_INACTIVE) return "\033[2m";     /* dim */
    return "\033[0m";
}

static char cell_glyph(const TvRenderCell *rc) {
    if (!rc->visible) return ' ';
    if (rc->style & TV_STYLE_INACTIVE) return '.';
    if (rc->b != 0) return '#';
    if (rc->g > 0)  return '*';
    if (rc->r > 0x20 && rc->r < 0x7F) return (char)rc->r;
    return '.';
}

int tv_render_frame(const Tervas *tv) {
    if (!tv || !tv->snapshot.valid) return TV_ERR_NULL;

    /* Build frame from current state */
    TvFrame frame;
    TvFilter f = tv->filter;
    tv_build_frame(&frame, &tv->snapshot, &f, 64, 32);

    printf("\033[2J\033[H"); /* clear screen */
    printf("\033[1m=== Tervas  tick=%u  visible=%u  wh=%u  bh=%u ===\033[0m\n",
           frame.tick, frame.total_visible, frame.wh_active, frame.bh_active);

    /* Render grid */
    uint32_t cols = 64, idx = 0;
    for (uint32_t i = 0; i < frame.count; i++) {
        const TvRenderCell *rc = &frame.cells[i];
        printf("%s%c\033[0m", style_color(rc->style), cell_glyph(rc));
        if (++idx >= cols) { printf("\n"); idx = 0; }
    }
    if (idx > 0) printf("\n");

    /* Status line */
    printf("\033[2m%s\033[0m\n", tv->status_msg);
    return TV_OK;
}
