#include "../include/canvasos_types.h"

/* ==========================================
 * Ring(MH) Scan — 버그 수정 + Adaptive 모드
 * REF: docs/SCAN_RingMH.md
 *
 * 버그 수정: 4 세그먼트 방식으로 축 방향 포인트(dy=0, dx!=0) 누락 해결
 * 세그먼트 구조 (각 d포인트):
 *   seg0 Q3축:  t=0..d-1 → (t,   d-t)   (0,d)→(d-1,1)
 *   seg1 +x·Q1: t=0..d-1 → (d-t, -t)   (d,0)→(1,-(d-1))
 *   seg2 Q0축:  t=0..d-1 → (-t, -(d-t)) (0,-d)→(-(d-1),-1)
 *   seg3 -x·Q2: t=0..d-1 → (-(d-t), t) (-d,0)→(-1,d-1)
 * 총 4d개, 중복 없음, 4축 포인트 모두 포함
 * ========================================== */

typedef struct {
    uint32_t d;
    int32_t  k;
    bool     started;
} RingMHState;

static bool in_bounds(int32_t x, int32_t y) {
    return x >= 0 && x < CANVAS_W && y >= 0 && y < CANVAS_H;
}

bool scan_next_ringmh(RingMHState *s, uint16_t *out_x, uint16_t *out_y) {
    if (!s->started) { s->started = true; s->d = 0; s->k = 0; }

    for (;;) {
        uint32_t d     = s->d;
        int32_t  total = (d == 0) ? 1 : (int32_t)(4 * d);

        if (s->k >= total) {
            s->d++;
            s->k = 0;
            if (s->d > (uint32_t)(ORIGIN_X + ORIGIN_Y)) return false;
            continue;
        }

        int32_t idx = s->k++;
        int32_t dx  = 0, dy = 0;

        if (d == 0) {
            dx = 0; dy = 0;
        } else {
            int32_t seg = idx / (int32_t)d;
            int32_t t   = idx % (int32_t)d;
            switch (seg) {
            case 0: dx =  t;             dy =  (int32_t)d - t; break; /* Q3 */
            case 1: dx =  (int32_t)d-t;  dy = -t;              break; /* +x·Q1 */
            case 2: dx = -t;             dy = -((int32_t)d-t); break; /* Q0 */
            default:dx = -((int32_t)d-t);dy =  t;              break; /* -x·Q2 */
            }
        }

        int32_t x = (int32_t)ORIGIN_X + dx;
        int32_t y = (int32_t)ORIGIN_Y + dy;
        if (!in_bounds(x, y)) continue;

        *out_x = (uint16_t)x;
        *out_y = (uint16_t)y;
        return true;
    }
}

/* ---- Spiral Stream (고밀도 모드: 행 우선 단순 순회) ---- */
typedef struct {
    uint32_t pos;
} SpiralState;

bool scan_next_spiral(SpiralState *s, uint16_t *out_x, uint16_t *out_y) {
    if (s->pos >= (uint32_t)(CANVAS_W * CANVAS_H)) return false;
    *out_x = (uint16_t)(s->pos % CANVAS_W);
    *out_y = (uint16_t)(s->pos / CANVAS_W);
    s->pos++;
    return true;
}
