/*
 * canvas_bh_compress.c — Phase 5: BH 시간 의미 압축 구현
 *
 * 구현 상태:
 *   bh_analyze_window: BH-IDLE 구현 완료, LOOP/BURST 스켈레톤
 *   bh_compress: WH 기록 구현 완료
 *   bh_replay_summary: IDLE 완료, LOOP/BURST 스켈레톤
 *   bh_run_all: 스켈레톤 (Phase 5 완성 필요)
 */
#include "../include/canvas_bh_compress.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

/* ---- 내부 통계 ---- */
static BhStats s_stats = {0};

/* ---- 내부 헬퍼: WH 구간 해시 ---- */
static uint32_t _wh_range_hash(EngineContext *ctx,
                                uint32_t from_tick, uint32_t to_tick) {
    uint32_t h = 2166136261u;
    uint32_t ticks = (to_tick > from_tick) ? (to_tick - from_tick) : 0u;
    uint32_t cap   = (uint32_t)WH_CAP;
    uint32_t limit = ticks < cap ? ticks : cap;

    for (uint32_t i = 0; i < limit; i++) {
        uint32_t t = from_tick + i;
        WhRecord r;
        if (!wh_read_record(ctx, t, &r)) continue;
        /* FNV-1a over record fields (정수만, DK-2) */
        h ^= (uint32_t)DK_INT(r.opcode_index);  h *= 16777619u;
        h ^= (uint32_t)DK_INT(r.target_addr);   h *= 16777619u;
        h ^= (uint32_t)DK_INT(r.tick_or_event); h *= 16777619u;
    }
    return h;
}

/* ---- [BH-IDLE] 무변화 감지 ---- */
static int _detect_idle(EngineContext *ctx,
                         uint32_t from_tick, uint32_t to_tick,
                         uint16_t gate_id,
                         BhSummary *out) {
    uint32_t ticks = to_tick - from_tick;
    if (ticks < BH_IDLE_MIN_TICKS) return 0;

    uint32_t idle_run = 0;
    for (uint32_t t = from_tick; t < to_tick; t++) {
        WhRecord r;
        bool found = wh_read_record(ctx, t, &r);
        /* 이 gate_id에 관련된 이벤트가 있으면 IDLE 아님 */
        bool relevant = found &&
            (uint16_t)(r.target_addr & 0xFFFFu) == gate_id &&
            r.opcode_index != WH_OP_NOP &&
            r.opcode_index != WH_OP_TICK;
        if (relevant) {
            idle_run = 0;
        } else {
            idle_run++;
        }
    }

    if (idle_run < BH_IDLE_MIN_TICKS) return 0;

    /* IDLE 구간 발견 */
    *out = (BhSummary){
        .rule          = BH_RULE_IDLE,
        .from_tick     = from_tick,
        .to_tick       = to_tick,
        .gate_id       = gate_id,
        .opcode        = WH_OP_NOP,
        .count         = idle_run,
        .stride        = 0,
        .original_hash = _wh_range_hash(ctx, from_tick, to_tick),
    };
    return 1;
}

/* ---- [BH-LOOP] 반복 패턴 감지 ---- */
static int _detect_loop(EngineContext *ctx,
                         uint32_t from_tick, uint32_t to_tick,
                         uint16_t gate_id,
                         BhSummary *out) {
    /* Phase 5 TODO:
     * 1. from_tick..to_tick에서 gate_id 관련 레코드 추출
     * 2. 연속 레코드 간 (opcode, target) 패턴 비교
     * 3. 동일 패턴이 BH_LOOP_MIN_REPEAT 이상이면 stride 계산
     * 4. out에 LOOP 요약 채우기
     */
    (void)ctx; (void)from_tick; (void)to_tick; (void)gate_id; (void)out;
    return 0; /* stub */
}

/* ---- [BH-BURST] 폭주 이벤트 감지 ---- */
static int _detect_burst(EngineContext *ctx,
                          uint32_t from_tick, uint32_t to_tick,
                          uint16_t gate_id,
                          BhSummary *out) {
    /* Phase 5 TODO:
     * 1. 슬라이딩 윈도우(BH_BURST_WINDOW 크기)로 gate_id 이벤트 수 카운트
     * 2. 창 내 count >= BH_BURST_THRESHOLD 이면 BURST 감지
     * 3. 가장 큰 burst 구간을 선택해 out에 채우기
     */
    (void)ctx; (void)from_tick; (void)to_tick; (void)gate_id; (void)out;
    return 0; /* stub */
}

/* ---- bh_analyze_window ---- */
int bh_analyze_window(EngineContext *ctx,
                       uint32_t from_tick, uint32_t to_tick,
                       uint16_t gate_id,
                       BhSummary *summary) {
    if (!ctx || !summary) return -1;
    if (from_tick >= to_tick) return 0;

    memset(summary, 0, sizeof(*summary));
    summary->rule = BH_RULE_NONE;

    /* 우선순위: IDLE > LOOP > BURST */
    if (_detect_idle (ctx, from_tick, to_tick, gate_id, summary)) return 1;
    if (_detect_loop (ctx, from_tick, to_tick, gate_id, summary)) return 1;
    if (_detect_burst(ctx, from_tick, to_tick, gate_id, summary)) return 1;

    return 0;
}

/* ---- bh_compress: WH에 BH_SUMMARY 레코드 기록 ---- */
int bh_compress(EngineContext *ctx, const BhSummary *s,
                const TickBoundaryGuard *guard) {
    if (!ctx || !s || s->rule == BH_RULE_NONE) return -1;

    /* [DK-1] 틱 경계 검증 */
    ASSERT_TICK_BOUNDARY(ctx, *guard);

    /* BH_SUMMARY를 현재 tick 위치에 WH로 기록 */
    WhRecord r;
    memset(&r, 0, sizeof(r));

    r.tick_or_event = DK_INT(s->from_tick);
    r.opcode_index  = WH_OP_BH_SUMMARY;
    r.flags         = (uint8_t)DK_INT(s->rule);
    r.param0        = (uint8_t)(DK_INT(s->gate_id) & 0xFFu);

    r.target_addr   = DK_INT(s->to_tick);
    r.target_kind   = (uint8_t)DK_INT(s->count & 0xFFu);
    r.arg_state     = (uint8_t)((DK_INT(s->gate_id) >> 8u) & 0xFFu);
    r.param1        = (uint8_t)DK_INT(s->stride & 0xFFu);

    wh_write_record(ctx, ctx->tick, &r);

    /* 통계 업데이트 (정수만) */
    switch (s->rule) {
        case BH_RULE_IDLE:  s_stats.idle_count++;  break;
        case BH_RULE_LOOP:  s_stats.loop_count++;  break;
        case BH_RULE_BURST: s_stats.burst_count++; break;
        default: break;
    }
    uint64_t span = (uint64_t)DK_INT(s->to_tick) - (uint64_t)DK_INT(s->from_tick);
    s_stats.ticks_saved += span > 1u ? span - 1u : 0u;

    return 0;
}

/* ---- bh_replay_summary ---- */
int bh_replay_summary(EngineContext *ctx, const BhSummary *s) {
    if (!ctx || !s) return -1;

    switch (s->rule) {
        case BH_RULE_IDLE:
            /* IDLE 복원: 해당 구간은 no-op. gate 상태 변화 없음. */
            return 0;

        case BH_RULE_LOOP:
            /* Phase 5 TODO:
             * pattern을 s->count 회, s->stride 틱 간격으로 재실행.
             * wh_exec_record() 루프.
             */
            return -1; /* not yet implemented */

        case BH_RULE_BURST:
            /* Phase 5 TODO:
             * s->from_tick 부터 s->count 개 이벤트를 순서대로 재실행.
             */
            return -1; /* not yet implemented */

        default:
            return -1;
    }
}

/* ---- bh_run_all ---- */
int bh_run_all(EngineContext *ctx, uint32_t current_tick) {
    /* [DK-1] 틱 경계 선언 */
    TickBoundaryGuard guard = dk_begin_tick(ctx, "bh_run_all");

    /* Phase 5 TODO:
     * 1. WH retained window [current_tick - WH_CAP, current_tick] 순회
     * 2. gate_id별로 bh_analyze_window() 호출
     * 3. 압축 가능하면 bh_compress() 호출
     *
     * 최적화 방향 (Phase 6):
     * - gate별로 구간을 분리해서 병렬 분석
     * - SIMD(AVX2)로 hash/compare 가속
     */
    int compressed = 0;
    (void)current_tick;

    /* stub: 현재는 아무것도 압축하지 않음 */

    dk_end_tick(&guard);
    return compressed;
}

/* ---- 통계 ---- */
void bh_get_stats(BhStats *out) {
    if (out) *out = s_stats;
}

void bh_print_stats(void) {
    printf("=== BH Compress Stats ===\n");
    printf("  IDLE  : %llu\n", (unsigned long long)s_stats.idle_count);
    printf("  LOOP  : %llu\n", (unsigned long long)s_stats.loop_count);
    printf("  BURST : %llu\n", (unsigned long long)s_stats.burst_count);
    printf("  Ticks saved: %llu\n", (unsigned long long)s_stats.ticks_saved);
    printf("=========================\n");
}
