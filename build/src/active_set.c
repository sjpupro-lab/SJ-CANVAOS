#include "../include/canvasos_types.h"
#include <string.h>

/* ==========================================
 * Adaptive ActiveSet — Phase 1.1
 * 밀도에 따라 스캔 전략 자동 선택
 * ========================================== */

void activeset_init(ActiveSet *as) {
    memset(as, 0, sizeof(*as));
    as->mode = SCAN_RING_MH;
}

static void activeset_recalc(ActiveSet *as) {
    uint32_t cnt = 0;
    for (int i = 0; i < TILE_COUNT; i++) cnt += as->open[i];
    as->open_count = cnt;
    as->density    = (float)cnt / (float)TILE_COUNT;
    as->mode       = activeset_mode(as->density);
}

void activeset_open(ActiveSet *as, uint32_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    if (as->open[tile_id]) return; /* already open, skip recalc */
    as->open[tile_id] = 1;
    as->open_count++;
    as->density = (float)as->open_count / (float)TILE_COUNT;
    as->mode    = activeset_mode(as->density);
}

void activeset_close(ActiveSet *as, uint32_t tile_id) {
    if (tile_id >= TILE_COUNT) return;
    if (!as->open[tile_id]) return;
    as->open[tile_id] = 0;
    as->open_count--;
    as->density = (float)as->open_count / (float)TILE_COUNT;
    as->mode    = activeset_mode(as->density);
}

int activeset_is_open(const ActiveSet *as, uint16_t x, uint16_t y) {
    return as->open[tile_id_of_xy(x, y)];
}

/* Boot Cross: origin tile + Q3 row strip (y=512, x=512..1023) */
void activeset_boot_cross(ActiveSet *as) {
    activeset_open(as, tile_id_of_xy(ORIGIN_X, ORIGIN_Y));
    for (uint16_t x = ORIGIN_X; x < CANVAS_W; x += TILE)
        activeset_open(as, tile_id_of_xy(x, ORIGIN_Y));
    activeset_recalc(as);
}

const char *scanmode_name(ScanMode m) {
    switch (m) {
    case SCAN_RING_MH: return "Ring(MH)";
    case SCAN_HYBRID:  return "Hybrid";
    case SCAN_SPIRAL:  return "Spiral";
    default:           return "?";
    }
}
