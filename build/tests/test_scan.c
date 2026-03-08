#include "../include/canvasos_types.h"
#include <stdio.h>
#include <assert.h>

typedef struct { uint32_t d; int32_t k; bool started; } RingMHState;
extern bool scan_next_ringmh(RingMHState *s, uint16_t *ox, uint16_t *oy);

int main(void) {
    RingMHState st = {0};
    uint16_t x, y;

    /* 1. 첫 좌표 = (512,512) */
    bool ok = scan_next_ringmh(&st, &x, &y);
    assert(ok && x == ORIGIN_X && y == ORIGIN_Y);

    /* 2. 전체 셀 수 = 1024*1024 */
    st = (RingMHState){0};
    int count = 0;
    while (scan_next_ringmh(&st, &x, &y)) count++;
    printf("total cells: %d (expected: %d)\n", count, CANVAS_W * CANVAS_H);
    assert(count == CANVAS_W * CANVAS_H);

    /* 3. x축 포인트 (513,512) 확인 — 버그 수정 검증 */
    st = (RingMHState){0};
    bool found_x_axis = false;
    while (scan_next_ringmh(&st, &x, &y)) {
        if (x == 513 && y == 512) { found_x_axis = true; break; }
    }
    assert(found_x_axis);
    printf("x-axis cell (513,512): FOUND\n");

    printf("[PASS] test_scan\n");
    return 0;
}
