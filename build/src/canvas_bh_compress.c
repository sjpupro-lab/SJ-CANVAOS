/*
 * canvas_bh_compress.c — Phase 5: BH 시간 의미 압축 구현
 *
 * 구현 상태: Phase-11 완성
 *   bh_analyze_window: IDLE/LOOP/BURST 모두 구현 완료
 *   bh_compress: WH 기록 구현 완료
 *   bh_replay_summary: IDLE/LOOP/BURST 모두 구현 완료
 *   bh_run_all: WH 스캔 + gate별 청크 분석 구현 완료
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
    uint32_t ticks = to_tick - from_tick;
    if (ticks < BH_LOOP_MIN_REPEAT * 2) return 0;

    /* 윈도우 내 해시 패턴 수집 (최대 64개) */
    #define LOOP_WIN 64
    uint32_t hashes[LOOP_WIN];
    uint32_t n = 0;

    for (uint32_t t = from_tick; t < to_tick && n < LOOP_WIN; t++) {
        WhRecord r;
        if (!wh_read_record(ctx, t, &r)) continue;
        if ((uint16_t)(r.target_addr & 0xFFFFu) != gate_id) continue;
        if (r.opcode_index == WH_OP_NOP || r.opcode_index == WH_OP_TICK) continue;
        /* FNV-1a hash of (opcode, target, param) */
        uint32_t h = 2166136261u;
        h ^= (uint32_t)DK_INT(r.opcode_index); h *= 16777619u;
        h ^= (uint32_t)DK_INT(r.param0);       h *= 16777619u;
        h ^= (uint32_t)DK_INT(r.target_addr);  h *= 16777619u;
        hashes[n++] = h;
    }

    if (n < BH_LOOP_MIN_REPEAT * 2) return 0;

    /* period P를 1부터 n/2까지 시도 */
    for (uint32_t p = 1; p <= n / BH_LOOP_MIN_REPEAT; p++) {
        uint32_t repeats = 0;
        bool match = true;
        for (uint32_t i = p; i < n && match; i++) {
            if (hashes[i] == hashes[i % p]) {
                if (i % p == 0) repeats++;
            } else {
                match = false;
            }
        }
        if (!match) continue;
        if (repeats < BH_LOOP_MIN_REPEAT) continue;

        /* LOOP 발견 */
        uint32_t stride = (ticks > repeats && repeats > 0) ? ticks / repeats : 1;
        *out = (BhSummary){
            .rule          = BH_RULE_LOOP,
            .from_tick     = from_tick,
            .to_tick       = to_tick,
            .gate_id       = gate_id,
            .opcode        = 0,
            .count         = repeats,
            .stride        = stride,
            .original_hash = _wh_range_hash(ctx, from_tick, to_tick),
        };
        return 1;
    }
    #undef LOOP_WIN
    return 0;
}

/* ---- [BH-BURST] 폭주 이벤트 감지 ---- */
static int _detect_burst(EngineContext *ctx,
                          uint32_t from_tick, uint32_t to_tick,
                          uint16_t gate_id,
                          BhSummary *out) {
    uint32_t ticks = to_tick - from_tick;
    if (ticks < BH_BURST_WINDOW) return 0;

    /* 슬라이딩 윈도우: 각 틱의 gate_id 관련 이벤트 수 카운트 */
    uint32_t best_start = 0, best_count = 0;

    for (uint32_t w = from_tick; w + BH_BURST_WINDOW <= to_tick; w++) {
        uint32_t count = 0;
        for (uint32_t t = w; t < w + BH_BURST_WINDOW; t++) {
            WhRecord r;
            if (!wh_read_record(ctx, t, &r)) continue;
            if ((uint16_t)(r.target_addr & 0xFFFFu) != gate_id) continue;
            if (r.opcode_index == WH_OP_NOP || r.opcode_index == WH_OP_TICK) continue;
            count++;
        }
        if (count > best_count) {
            best_count = count;
            best_start = w;
        }
    }

    if (best_count < BH_BURST_THRESHOLD) return 0;

    *out = (BhSummary){
        .rule          = BH_RULE_BURST,
        .from_tick     = best_start,
        .to_tick       = best_start + BH_BURST_WINDOW,
        .gate_id       = gate_id,
        .opcode        = 0,
        .count         = best_count,
        .stride        = 0,
        .original_hash = _wh_range_hash(ctx, best_start, best_start + BH_BURST_WINDOW),
    };
    return 1;
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

        case BH_RULE_LOOP: {
            /* LOOP 복원: pattern을 count 회, stride 틱 간격으로 재실행 */
            uint32_t stride = s->stride > 0 ? s->stride : 1;
            for (uint32_t i = 0; i < s->count; i++) {
                uint32_t t = s->from_tick + i * stride;
                WhRecord rec;
                if (wh_read_record(ctx, t, &rec))
                    wh_exec_record(ctx, &rec);
            }
            return 0;
        }

        case BH_RULE_BURST: {
            /* BURST 복원: from_tick부터 count 개 이벤트 순서대로 재실행 */
            uint32_t replayed = 0;
            for (uint32_t t = s->from_tick; t < s->to_tick && replayed < s->count; t++) {
                WhRecord rec;
                if (!wh_read_record(ctx, t, &rec)) continue;
                if ((uint16_t)(rec.target_addr & 0xFFFFu) != s->gate_id) continue;
                wh_exec_record(ctx, &rec);
                replayed++;
            }
            return 0;
        }

        default:
            return -1;
    }
}

/* ---- bh_run_all ---- */
int bh_run_all(EngineContext *ctx, uint32_t current_tick) {
    if (!ctx) return 0;

    /* [DK-1] 틱 경계 선언 */
    TickBoundaryGuard guard = dk_begin_tick(ctx, "bh_run_all");

    int compressed = 0;

    /* WH retained window 계산 */
    uint32_t wh_cap = (uint32_t)WH_CAP;
    uint32_t from_tick = (current_tick > wh_cap) ? (current_tick - wh_cap) : 0;
    uint32_t to_tick   = current_tick;

    /* 윈도우를 BH_IDLE_MIN_TICKS 단위 청크로 분할하여 분석 */
    uint32_t chunk_size = BH_IDLE_MIN_TICKS * 4;  /* 64 틱 청크 */
    if (chunk_size == 0) chunk_size = 64;

    /* gate_id 순회: WH에서 사용된 gate만 추적 (최적화) */
    /* 간소화: 0~TILEGATE_COUNT에서 샘플링 — 최대 256개 gate 검사 */
    #define BH_SCAN_GATES  256u
    uint16_t gates_seen[BH_SCAN_GATES];
    uint32_t gates_count = 0;

    /* WH를 스캔하여 사용된 gate_id 수집 */
    for (uint32_t t = from_tick; t < to_tick && gates_count < BH_SCAN_GATES; t++) {
        WhRecord r;
        if (!wh_read_record(ctx, t, &r)) continue;
        uint16_t gid = (uint16_t)(r.target_addr & 0xFFFF);
        /* 중복 확인 (간단한 선형 검색) */
        bool found = false;
        for (uint32_t g = 0; g < gates_count; g++) {
            if (gates_seen[g] == gid) { found = true; break; }
        }
        if (!found) gates_seen[gates_count++] = gid;
    }

    /* 각 gate_id에 대해 청크별 분석 */
    for (uint32_t g = 0; g < gates_count; g++) {
        uint16_t gid = gates_seen[g];

        for (uint32_t chunk_start = from_tick; chunk_start < to_tick;
             chunk_start += chunk_size) {
            uint32_t chunk_end = chunk_start + chunk_size;
            if (chunk_end > to_tick) chunk_end = to_tick;

            BhSummary summary;
            int found = bh_analyze_window(ctx, chunk_start, chunk_end,
                                          gid, &summary);
            if (found > 0) {
                int rc = bh_compress(ctx, &summary, &guard);
                if (rc == 0) compressed++;
            }
        }
    }
    #undef BH_SCAN_GATES

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
