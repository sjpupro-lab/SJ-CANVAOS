#pragma once
/* canvasos_timewarp.h — Phase-8: Time Travel Debugger (CanvasOS 특장점) */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_engine_ctx.h"

typedef struct {
    uint32_t  saved_tick;       /* timewarp 시작 전 tick */
    uint32_t  target_tick;      /* 이동한 tick */
    bool      active;           /* timewarp 모드 중인지 */
    char      backup_path[64];  /* CVP 백업 경로 */
} TimeWarp;

void timewarp_init(TimeWarp *tw);

/* tick 이동: 자동 CVP 백업 → WH 기반 상태 재구성 */
int  timewarp_goto(TimeWarp *tw, EngineContext *ctx, uint32_t target_tick);

/* 현재로 복귀: CVP 백업에서 복원 */
int  timewarp_resume(TimeWarp *tw, EngineContext *ctx);

/* n_ticks만큼 재실행 (중간 관찰용) */
int  timewarp_step(TimeWarp *tw, EngineContext *ctx, uint32_t n_ticks);

/* 두 tick 사이의 차이: 변경된 레코드 수 기반 추정값 반환 */
uint32_t timewarp_diff(EngineContext *ctx, uint32_t tick_a, uint32_t tick_b);
