#include "../include/canvasos_types.h"
#include <stdio.h>
#include <string.h>

/* ==========================================
 * CanvasOS Machine Engine — Phase 1
 * Phase 1.1: Adaptive ActiveSet 스캔 모드
 * Phase 1.2: Energy Decay (G값 안정 수렴)
 * ========================================== */

/* --- Forward decls --- */
typedef struct { uint32_t d; int32_t k; bool started; } RingMHState;
typedef struct { uint32_t pos; } SpiralState;
extern bool scan_next_ringmh(RingMHState *s, uint16_t *ox, uint16_t *oy);
extern bool scan_next_spiral(SpiralState *s, uint16_t *ox, uint16_t *oy);

extern void activeset_init(ActiveSet *as);
extern void activeset_boot_cross(ActiveSet *as);
extern void activeset_open(ActiveSet *as, uint32_t tile_id);
extern void activeset_close(ActiveSet *as, uint32_t tile_id);
extern int  activeset_is_open(const ActiveSet *as, uint16_t x, uint16_t y);
extern const char *scanmode_name(ScanMode m);

/* --- Canvas state --- */
static Cell       canvas[CANVAS_W * CANVAS_H];
static ActiveSet  aset;
static EnergyConfig ecfg = { .decay_rate = 1, .enabled = true };
static uint64_t   tick;

static inline uint32_t pidx(uint16_t x, uint16_t y) {
    return (uint32_t)y * CANVAS_W + (uint32_t)x;
}

/* ---- Phase 1.2: Energy Decay — G값 소멸 ---- */
static void energy_decay_cell(Cell *c) {
    if (!ecfg.enabled) return;
    if (c->G > ecfg.decay_rate) c->G -= ecfg.decay_rate;
    else                         c->G  = 0;
}

/* ---- Cell 실행 (B-page placeholder) ---- */
static bool exec_cell(Cell *c, uint16_t x, uint16_t y) {
    energy_decay_cell(c); /* Phase 1.2: decay before exec */

    uint16_t op  = (uint16_t)c->B;        /* opcode index lives in B (Behavior Layer) */
    uint16_t arg = (uint16_t)(c->A & 0xFFFF); /* arg/address low bits (legacy demo) */

    switch ((Opcode)op) {
    case OP_NOP:     return true;
    case OP_PRINT:   putchar((char)c->R); fflush(stdout); return true;
    case OP_HALT:    return false;
    case OP_GATE_ON: activeset_open(&aset, arg);  return true;
    case OP_GATE_OFF:activeset_close(&aset, arg); return true;
    case OP_ENERGY:  c->G = (uint8_t)(c->G + arg > 255 ? 255 : c->G + arg); return true;
    case OP_MKFILE:  /* Phase 2: B=file_id, G=meta — stub */ return true;
    default:         return true;
    }
    (void)x; (void)y;
}

/* ---- Phase 1.1: Adaptive scan frame ---- */
static bool run_frame_adaptive(void) {
    ScanMode mode = aset.mode;

    if (mode == SCAN_SPIRAL) {
        /* > 30%: Spiral stream */
        SpiralState ss = {0};
        uint16_t x, y;
        while (scan_next_spiral(&ss, &x, &y)) {
            if (!activeset_is_open(&aset, x, y)) continue;
            Cell *c = &canvas[pidx(x, y)];
            if (!exec_cell(c, x, y)) return false;
        }
    } else if (mode == SCAN_HYBRID) {
        /* 2~30%: Ring(MH) + 열린 타일 내 local linear */
        RingMHState rs = {0};
        uint16_t rx, ry;
        while (scan_next_ringmh(&rs, &rx, &ry)) {
            if (!activeset_is_open(&aset, rx, ry)) {
                /* local linear: 같은 타일 내 셀은 건너뜀 → Ring만 진행 */
                continue;
            }
            Cell *c = &canvas[pidx(rx, ry)];
            if (!exec_cell(c, rx, ry)) return false;
        }
    } else {
        /* < 2%: Ring(MH) 결정론 */
        RingMHState rs = {0};
        uint16_t x, y;
        while (scan_next_ringmh(&rs, &x, &y)) {
            if (!activeset_is_open(&aset, x, y)) continue;
            Cell *c = &canvas[pidx(x, y)];
            if (!exec_cell(c, x, y)) return false;
        }
    }
    return true;
}

#ifdef ENGINE_DEMO_MAIN
int main(void) {
    memset(canvas, 0, sizeof(canvas));
    activeset_init(&aset);
    activeset_boot_cross(&aset);

    /* Demo: "HELLO\n" along y=512, x=512..518 */
    const char *msg = "HELLO\n";
    for (int i = 0; msg[i]; i++) {
        uint16_t x = (uint16_t)(ORIGIN_X + i);
        uint16_t y = ORIGIN_Y;
        Cell *c    = &canvas[pidx(x, y)];
        c->A       = A_make(0, OP_PRINT, 0);
        c->R       = (uint8_t)msg[i];
        c->G       = 10; /* 초기 에너지 */
    }
    /* HALT */
    {
        uint16_t x = (uint16_t)(ORIGIN_X + (int)strlen(msg));
        Cell *c    = &canvas[pidx(x, ORIGIN_Y)];
        c->A       = A_make(0, OP_HALT, 0);
    }

    printf("=== CanvasOS Phase 1 ===\n");
    printf("Scan mode: %s (density=%.1f%%)\n",
           scanmode_name(aset.mode), aset.density * 100.0f);
    printf("Output: ");

    bool ok = run_frame_adaptive();
    (void)ok;
    tick++;

    printf("\n=== HALT | tick=%llu ===\n", (unsigned long long)tick);
    return 0;
}
#endif /* ENGINE_DEMO_MAIN */
