#pragma once
/*
 * canvas_bh_compress.h — Phase 5: BH 시간 의미 압축
 *
 * =================================================================
 * BH(BlackHole)의 진짜 의미
 * =================================================================
 *
 * Phase 3/4에서 BH는 "에너지 decay(감쇠)" 전용 공간이었다.
 * Phase 5에서 BH는 **시간 의미 압축기**가 된다.
 *
 * 핵심 규약:
 *   BH는 WH 레코드를 "삭제"하지 않는다.
 *   WH 레코드를 "더 적은 셀로 표현하는 요약 이벤트로 치환"한다.
 *   치환 결과 자체도 WH에 기록한다(감사/재현 가능).
 *   치환 후에도 원본 구간의 인덱스(참조 링크/해시)는 보존한다.
 *
 * =================================================================
 * 3종 압축 규칙
 * =================================================================
 *
 * [BH-IDLE]  무변화 구간 요약
 *   조건: tick T1..T2 동안 특정 gate_id에 대해 WH 이벤트 없음
 *   압축: "gate_id X는 T1..T2 동안 IDLE (N틱 무변화)" 1개 레코드로 대체
 *   복원: replay 시 IDLE 구간은 no-op으로 처리
 *
 * [BH-LOOP]  반복 패턴 요약
 *   조건: tick T1..T2에서 동일 (opcode, gate_id, param) 패턴이 K번 반복
 *   압축: "pattern P, K회 반복, stride=N틱" 1개 레코드로 대체
 *   복원: replay 시 P를 K번, N틱 간격으로 재실행
 *
 * [BH-BURST] 폭주 이벤트 요약
 *   조건: 단시간(K틱 이내)에 동일 gate_id/opcode 이벤트가 BURST_THRESHOLD 이상
 *   압축: "T_start, count=M, gate=X, op=Y" 1개 레코드로 대체
 *   복원: replay 시 M개를 T_start부터 순서대로 재실행
 *
 * =================================================================
 * WH 기록 규약 (감사/재현)
 * =================================================================
 *
 * BH 수행은 틱 경계에서만 실행 (DK-1).
 * BH 결과는 WH에 opcode=WH_OP_BH_SUMMARY로 기록.
 * 원본 구간 [T1,T2]의 해시도 함께 기록 (완전 삭제 방지).
 *
 * WH BH_SUMMARY 레코드 포맷 (2-cell):
 *   C0.A = T1 (from_tick)
 *   C0.B = WH_OP_BH_SUMMARY (0x40)
 *   C0.G = bh_rule (IDLE=1/LOOP=2/BURST=3)
 *   C0.R = gate_id low8
 *   C1.A = T2 (to_tick)
 *   C1.B = compressed count (반복 횟수 or 이벤트 수)
 *   C1.G = gate_id high8
 *   C1.R = stride (LOOP only, 0=others)
 *
 * =================================================================
 */

#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"
#include "engine_time.h"
#include "canvas_determinism.h"

/* ---- BH 압축 규칙 타입 ---- */
typedef enum {
    BH_RULE_NONE  = 0,
    BH_RULE_IDLE  = 1,    /* 무변화 구간 요약 */
    BH_RULE_LOOP  = 2,    /* 반복 패턴 요약 */
    BH_RULE_BURST = 3,    /* 폭주 이벤트 요약 */
} BhCompressRule;

/* WH opcode 확장 (engine_time.h의 WhOpcode와 연동) */
#define WH_OP_BH_SUMMARY  0x45u   /* BH 압축 결과 기록 */

/* ---- BH 압축 파라미터 ---- */
#define BH_IDLE_MIN_TICKS    16u   /* IDLE 감지 최소 구간 (틱) */
#define BH_LOOP_MIN_REPEAT   3u    /* LOOP 감지 최소 반복 횟수 */
#define BH_BURST_THRESHOLD   8u    /* BURST 감지 임계치 (K틱 내 N개) */
#define BH_BURST_WINDOW      4u    /* BURST 감지 시간 창 (틱) */

/* ---- BH 압축 결과 구조체 ---- */
typedef struct {
    BhCompressRule rule;
    uint32_t from_tick;      /* 원본 구간 시작 */
    uint32_t to_tick;        /* 원본 구간 끝 */
    uint16_t gate_id;        /* 대상 gate */
    uint8_t  opcode;         /* 대상 opcode (LOOP/BURST) */
    uint32_t count;          /* 반복 횟수 or 이벤트 수 */
    uint32_t stride;         /* LOOP stride (틱) */
    uint32_t original_hash;  /* 원본 구간 WH 셀 해시 (참조 보존) */
} BhSummary;

/* ---- API ---- */

/*
 * bh_analyze_window: WH 윈도우 [from_tick, to_tick] 에서
 * gate_id에 대해 압축 가능한 패턴을 찾는다.
 * 결과: summary에 채워서 반환. BH_RULE_NONE이면 압축 없음.
 * [DK-1] 반드시 틱 경계에서 호출.
 */
int bh_analyze_window(EngineContext *ctx,
                       uint32_t from_tick, uint32_t to_tick,
                       uint16_t gate_id,
                       BhSummary *summary);

/*
 * bh_compress: summary를 실제로 WH에 기록 (WH_OP_BH_SUMMARY).
 * 원본 구간 레코드는 덮어쓰지 않는다(WH circular 특성상 자연 만료).
 * [DK-1] 틱 경계 guard 필요.
 */
int bh_compress(EngineContext *ctx, const BhSummary *summary,
                const TickBoundaryGuard *guard);

/*
 * bh_replay_summary: BH_SUMMARY 레코드를 replay 시 복원한다.
 * IDLE: no-op. LOOP: pattern K회 재실행. BURST: M개 이벤트 재실행.
 */
int bh_replay_summary(EngineContext *ctx, const BhSummary *summary);

/*
 * bh_run_all: EngineContext의 WH 전체를 스캔해서
 * 압축 가능한 구간을 모두 BH 처리한다.
 * 대형 루프 → Phase 6에서 SIMD/병렬 최적화 예정.
 * [DK-1] 틱 경계에서만 호출할 것.
 */
int bh_run_all(EngineContext *ctx, uint32_t current_tick);

/*
 * bh_stats: 현재까지 압축 통계 출력 (디버그용)
 */
typedef struct {
    uint64_t idle_count;
    uint64_t loop_count;
    uint64_t burst_count;
    uint64_t ticks_saved;    /* 원본 tick 수 - 요약 tick 수 */
} BhStats;

void bh_get_stats(BhStats *out);
void bh_print_stats(void);
