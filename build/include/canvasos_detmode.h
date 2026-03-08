#pragma once
/* canvasos_detmode.h — Phase-8: Determinism Toggle (CanvasOS 특장점)
 *
 * 기본: 결정론 (DK-1~5 전부 ON)
 * det off: 비결정론 모드 → float, 랜덤, 비순차 허용
 * det on: 결정론 복원
 *
 * 비결정론 구간은 WH에 기록되지만, replay 불가.
 */
#include <stdint.h>
#include <stdbool.h>

#define WH_OP_DET_MODE  0x81

typedef struct {
    bool dk1_tick_boundary;    /* true = tick boundary 강제 */
    bool dk2_integer_only;     /* true = float 금지 */
    bool dk3_fixed_order;      /* true = cell_index 순서 강제 */
    bool dk4_normalize;        /* true = clamp 강제 */
    bool dk5_noise_absorb;     /* true = ±1 흡수 */
    bool wh_recording;         /* false = WH 기록 중단 */
    uint32_t nondet_since;     /* 비결정론 시작 tick (0=결정론) */
} DetMode;

void det_init(DetMode *dm);                  /* 전부 true (결정론) */
void det_set_all(DetMode *dm, bool on);      /* 전체 토글 */
void det_set_dk(DetMode *dm, int dk_id, bool on);  /* dk_id=1~5 개별 토글 */
bool det_is_deterministic(const DetMode *dm); /* 전부 true인지 */
void det_log_change(void *ctx, const DetMode *dm); /* WH에 기록 */
