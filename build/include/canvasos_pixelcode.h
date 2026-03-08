#pragma once
/*
 * canvasos_pixelcode.h — Phase-9: PixelCode Parser
 *
 * 터미널 입력 → 캔버스 셀 조작 + VM 제어.
 * 타자기처럼 치고, 피아노처럼 연주한다.
 */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "canvasos_vm.h"

/* ── 영역 선택 ───────────────────────────────────────── */
typedef struct {
    uint32_t x0, y0, x1, y1;
    bool     active;
} PxRange;

/* ── PixelCode 커서 상태 ─────────────────────────────── */
typedef struct {
    uint32_t cx, cy;       /* 커서 위치 */
    uint32_t reg_A;        /* 다음 커밋에 쓸 A */
    uint8_t  reg_B;        /* 다음 커밋에 쓸 B */
    uint8_t  reg_G;        /* 다음 커밋에 쓸 G */
    uint8_t  reg_R;        /* 다음 커밋에 쓸 R */
    PxRange  range;        /* 영역 선택 */
    uint32_t commit_count; /* 커밋 횟수 */
} PxState;

/* ── API ─────────────────────────────────────────────── */
void pxstate_init(PxState *px);
int  px_exec_line(EngineContext *ctx, PxState *px, VmState *vm, const char *line);
