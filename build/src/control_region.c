#include "../include/canvasos_types.h"
#include <string.h>
#include <stdint.h>

/* ==========================================
 * Control Region — SPEC §3-8
 * Helpers to read/write CR0-CR3 structures
 * into the canvas via byte-packing in B/G/R
 * ========================================== */

static inline uint32_t pidx(uint16_t x, uint16_t y) {
    return (uint32_t)y * CANVAS_W + (uint32_t)x;
}

/* Serialize raw bytes into canvas starting at (x0,y0),
 * packing 3 bytes per cell (B,G,R). A=tag, pad=0. */
static void cr_write_bytes(Cell *canvas, uint16_t x0, uint16_t y0,
                           uint32_t tag, const uint8_t *data, size_t n) {
    size_t i = 0;
    for (uint16_t row = 0; i < n; row++) {
        for (uint16_t col = 0; col < CANVAS_W && i < n; col += 1) {
            /* advance one cell at a time within the 32-wide sub-region */
            Cell *c = &canvas[pidx((uint16_t)(x0 + (col % 32)),
                                   (uint16_t)(y0 + row + col / 32))];
            c->A   = tag;
            c->B   = (i < n) ? data[i++] : 0;
            c->G   = (i < n) ? data[i++] : 0;
            c->R   = (i < n) ? data[i++] : 0;
            c->pad = 0;
            if (col % 32 == 31) break; /* max 32 cols per logical row */
        }
        if (row > 64) break; /* safety */
    }
}

static void cr_read_bytes(const Cell *canvas, uint16_t x0, uint16_t y0,
                          uint8_t *out, size_t n) {
    size_t i = 0;
    for (uint16_t row = 0; i < n; row++) {
        for (uint16_t col = 0; col < 32 && i < n; col++) {
            const Cell *c = &canvas[pidx((uint16_t)(x0 + col),
                                         (uint16_t)(y0 + row))];
            if (i < n) out[i++] = c->B;
            if (i < n) out[i++] = c->G;
            if (i < n) out[i++] = c->R;
        }
    }
}

/* ---- SuperBlock (CR0 at CR0_X, CR0_Y) ---- */

void cr_superblock_write(Cell *canvas, const SuperBlock *sb) {
    cr_write_bytes(canvas, CR0_X, CR0_Y, 0xCA000001u,
                   (const uint8_t *)sb, sizeof(*sb));
}

void cr_superblock_read(const Cell *canvas, SuperBlock *sb) {
    cr_read_bytes(canvas, CR0_X, CR0_Y, (uint8_t *)sb, sizeof(*sb));
}

/* ---- BranchCommit (CR1) ---- */
/* Max entries that fit in 32×32 cells @ 3 bytes/cell */
#define CR_BRANCH_MAX  ((32*32*3) / (int)sizeof(BranchCommit))

void cr_branch_write(Cell *canvas, int idx, const BranchCommit *bc) {
    if (idx < 0 || idx >= CR_BRANCH_MAX) return;
    size_t off = (size_t)idx * sizeof(*bc);
    /* Write one entry: deserialise offset into flat cell array */
    uint16_t x0 = CR1_X, y0 = CR1_Y;
    /* cell index = off/3 */
    size_t ci = off / 3;
    uint16_t col = (uint16_t)(ci % 32);
    uint16_t row = (uint16_t)(ci / 32);
    const uint8_t *p = (const uint8_t *)bc;
    size_t n = sizeof(*bc), i = 0;
    while (i < n) {
        Cell *c = &canvas[pidx((uint16_t)(x0 + col), (uint16_t)(y0 + row))];
        c->A = 0xCA000002u;
        if (i < n) c->B = p[i++]; else c->B = 0;
        if (i < n) c->G = p[i++]; else c->G = 0;
        if (i < n) c->R = p[i++]; else c->R = 0;
        c->pad = 0;
        col++;
        if (col >= 32) { col = 0; row++; }
    }
}

void cr_branch_read(const Cell *canvas, int idx, BranchCommit *bc) {
    if (idx < 0 || idx >= CR_BRANCH_MAX) return;
    size_t off = (size_t)idx * sizeof(*bc);
    uint16_t x0 = CR1_X, y0 = CR1_Y;
    size_t ci = off / 3;
    uint16_t col = (uint16_t)(ci % 32);
    uint16_t row = (uint16_t)(ci / 32);
    uint8_t *p = (uint8_t *)bc;
    size_t n = sizeof(*bc), i = 0;
    while (i < n) {
        const Cell *c = &canvas[pidx((uint16_t)(x0 + col), (uint16_t)(y0 + row))];
        if (i < n) p[i++] = c->B;
        if (i < n) p[i++] = c->G;
        if (i < n) p[i++] = c->R;
        col++;
        if (col >= 32) { col = 0; row++; }
    }
}

/* ---- SuperBlock default init ---- */
void cr_superblock_default(SuperBlock *sb) {
    memset(sb, 0, sizeof(*sb));
    const char *m = "CANVAOS1";
    for (int i = 0; i < 8; i++) sb->magic[i] = (uint8_t)m[i];
    sb->spec_version     = 0x00010000u;
    sb->build_id         = 0x00000001u;
    sb->default_scan     = 0; /* RING_WAVEFRONT */
    sb->default_neighbor = 4; /* N4 */
    sb->default_bpage    = 0;
    sb->default_branch   = 0;
}
