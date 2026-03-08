#ifndef CANVASOS_ENGINE_H
#define CANVASOS_ENGINE_H

#include "canvasos_types.h"

/* ==========================================
 * CanvasOS Engine API
 * ========================================== */

typedef struct {
    Cell     grid[CANVAS_H][CANVAS_W];
    GateState gates[TILEGATE_COUNT];
    BPageRule bpage[256];
    Coord    pc;       /* 현재 스캔 포인터 */
    int      halted;
    uint64_t tick;
} CanvasEngine;

/* 엔진 초기화 / 실행 */
void engine_init(CanvasEngine *eng);
void engine_step(CanvasEngine *eng);   /* 셀 1개 실행 */
void engine_run_frame(CanvasEngine *eng); /* 전체 Ring(MH) 1 프레임 */
void engine_reset(CanvasEngine *eng);

/* 게이트 */
void gate_open(CanvasEngine *eng, uint16_t gate_id);
void gate_close(CanvasEngine *eng, uint16_t gate_id);
int  gate_is_open(const CanvasEngine *eng, uint16_t gate_id);

/* CVP I/O */
int cvp_load(CanvasEngine *eng, const char *path);
int cvp_save(const CanvasEngine *eng, const char *path);

/* Ring(MH) 스캔 */
void scan_ringmh_reset(CanvasEngine *eng);
int  scan_ringmh_next(CanvasEngine *eng, Coord *out); /* 0=완료, 1=계속 */

#endif /* CANVASOS_ENGINE_H */
