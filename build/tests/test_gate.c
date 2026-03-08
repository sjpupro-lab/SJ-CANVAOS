#include "../include/canvasos_types.h"
#include <stdio.h>
#include <assert.h>

extern void activeset_init(ActiveSet *as);
extern void activeset_open(ActiveSet *as, uint32_t tile_id);
extern void activeset_close(ActiveSet *as, uint32_t tile_id);
extern int  activeset_is_open(const ActiveSet *as, uint16_t x, uint16_t y);
extern void activeset_boot_cross(ActiveSet *as);
extern const char *scanmode_name(ScanMode m);

int main(void) {
    ActiveSet as;
    activeset_init(&as);

    /* 1. 기본 CLOSE */
    assert(as.open_count == 0);
    assert(as.mode == SCAN_RING_MH);

    /* 2. open/close */
    uint32_t tid = tile_id_of_xy(ORIGIN_X, ORIGIN_Y);
    activeset_open(&as, tid);
    assert(activeset_is_open(&as, ORIGIN_X, ORIGIN_Y));
    activeset_close(&as, tid);
    assert(!activeset_is_open(&as, ORIGIN_X, ORIGIN_Y));

    /* 3. Adaptive mode: boot_cross → density 확인 */
    activeset_boot_cross(&as);
    printf("density=%.2f%% mode=%s\n", as.density*100.f, scanmode_name(as.mode));
    assert(as.open_count > 0);

    printf("[PASS] test_gate\n");
    return 0;
}
