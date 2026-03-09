/**
 * SJ Spatial Compression Engine v1.0 — C Native Implementation
 * ==============================================================
 * 512×512 고정 이미지 밝기 누산 공간압축.
 * 양발커널: 결정론 코어(누산) + 비결정론 어댑터(I/O)
 * WH/BH 완전 통합.
 */

#include "sj_spatial_compress.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── 내부 유틸 ─── */

static SjFrontLabel next_front_label(uint32_t index) {
    /*
     * 프론트 순서 생성:
     *   R/G 번갈아, B 캐리.
     *   index 0 → (r=1, g=0, b=0)
     *   index 1 → (r=0, g=1, b=0)
     *   index 2 → (r=2, g=0, b=0)
     *   ...
     *   index 509 → (r=0, g=255, b=0)
     *   index 510 → (r=1, g=0, b=1)
     *   ...
     */
    SjFrontLabel lbl;
    uint32_t cycle = 510;  /* 255*2 per B increment */
    lbl.b = (uint8_t)(index / cycle);
    uint32_t rem = index % cycle;
    uint32_t i_val = (rem / 2) + 1;       /* 1..255 */
    if (i_val > 255) i_val = 255;
    if (rem % 2 == 0) {
        lbl.r = (uint8_t)i_val;
        lbl.g = 0;
    } else {
        lbl.r = 0;
        lbl.g = (uint8_t)i_val;
    }
    return lbl;
}

/* ═══════════════════════════════════════════════
 *  WH 구현
 * ═══════════════════════════════════════════════ */

void sj_wh_init(SjWhiteHole *wh) {
    memset(wh, 0, sizeof(*wh));
}

void sj_wh_write(SjWhiteHole *wh, const SjWhRecord *rec) {
    uint32_t idx = wh->write_cursor % WH_CAP;
    wh->records[idx] = *rec;
    wh->write_cursor++;
    if (wh->count < WH_CAP) wh->count++;
}

int sj_wh_read_range(const SjWhiteHole *wh, uint32_t from_tick,
                     uint32_t to_tick, SjWhRecord *out, uint32_t max_out) {
    uint32_t found = 0;
    for (uint32_t i = 0; i < wh->count && found < max_out; i++) {
        const SjWhRecord *r = &wh->records[i];
        if (r->tick >= from_tick && r->tick < to_tick) {
            out[found++] = *r;
        }
    }
    return (int)found;
}

/* ═══════════════════════════════════════════════
 *  BH 구현
 * ═══════════════════════════════════════════════ */

void sj_bh_init(SjBlackHole *bh) {
    memset(bh, 0, sizeof(*bh));
}

/* IDLE 탐지: 윈도우 내 ACCUM 이벤트 없음 */
static int bh_detect_idle(const SjWhiteHole *wh, uint32_t ft, uint32_t tt,
                          SjBhSummary *out) {
    if (tt - ft < BH_IDLE_MIN) return 0;

    for (uint32_t i = 0; i < wh->count; i++) {
        const SjWhRecord *r = &wh->records[i];
        if (r->tick >= ft && r->tick < tt && r->opcode == WH_OP_ACCUM) {
            return 0;  /* 이벤트 있음 → IDLE 아님 */
        }
    }

    memset(out, 0, sizeof(*out));
    out->rule = BH_RULE_IDLE;
    out->from_tick = ft;
    out->to_tick = tt;
    out->count = tt - ft;
    return 1;
}

/* LOOP 탐지: 동일 (b0,b1) 반복 */
static int bh_detect_loop(const SjWhiteHole *wh, uint32_t ft, uint32_t tt,
                          SjBhSummary *out) {
    /* 윈도우 내 ACCUM 수집 */
    uint8_t first_b0 = 0, first_b1 = 0;
    uint32_t first_tick = 0, second_tick = 0;
    uint32_t match_count = 0;
    int has_first = 0;

    for (uint32_t i = 0; i < wh->count; i++) {
        const SjWhRecord *r = &wh->records[i];
        if (r->tick < ft || r->tick >= tt) continue;
        if (r->opcode != WH_OP_ACCUM) continue;

        if (!has_first) {
            first_b0 = r->b0;
            first_b1 = r->b1;
            first_tick = r->tick;
            has_first = 1;
            match_count = 1;
        } else if (r->b0 == first_b0 && r->b1 == first_b1) {
            if (match_count == 1) second_tick = r->tick;
            match_count++;
        } else {
            return 0;  /* 패턴 불일치 */
        }
    }

    if (match_count < BH_LOOP_MIN_REP) return 0;

    memset(out, 0, sizeof(*out));
    out->rule = BH_RULE_LOOP;
    out->from_tick = ft;
    out->to_tick = tt;
    out->count = match_count;
    out->stride = (match_count >= 2) ? (uint16_t)(second_tick - first_tick) : 0;
    out->pattern_b0 = first_b0;
    out->pattern_b1 = first_b1;
    return 1;
}

/* BURST 탐지: 짧은 구간에 대량 이벤트 */
static int bh_detect_burst(const SjWhiteHole *wh, uint32_t ft, uint32_t tt,
                           SjBhSummary *out) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < wh->count; i++) {
        const SjWhRecord *r = &wh->records[i];
        if (r->tick >= ft && r->tick < tt && r->opcode == WH_OP_ACCUM) {
            count++;
        }
    }

    if (count < BH_BURST_THRESH) return 0;

    memset(out, 0, sizeof(*out));
    out->rule = BH_RULE_BURST;
    out->from_tick = ft;
    out->to_tick = tt;
    out->count = count;
    return 1;
}

int sj_bh_compress(SjBlackHole *bh, const SjWhiteHole *wh,
                   uint32_t current_tick, uint32_t window_size) {
    int compressed = 0;
    uint32_t t = 0;

    while (t + window_size <= current_tick &&
           bh->summary_count < BH_MAX_SUMMARIES) {
        uint32_t ft = t, tt = t + window_size;
        SjBhSummary s;

        /* 우선순위: LOOP > BURST > IDLE */
        if (bh_detect_loop(wh, ft, tt, &s) ||
            bh_detect_burst(wh, ft, tt, &s) ||
            bh_detect_idle(wh, ft, tt, &s)) {
            bh->summaries[bh->summary_count++] = s;
            bh->ticks_saved += s.count;
            compressed++;
        }
        t += window_size;
    }
    return compressed;
}

/* ═══════════════════════════════════════════════
 *  캔버스 초기화 / 해제
 * ═══════════════════════════════════════════════ */

int sj_init(SjSpatialCanvas *sj) {
    if (!sj) return -1;

    memset(sj->canvas, 0, sizeof(sj->canvas));
    sj_wh_init(&sj->wh);
    sj_bh_init(&sj->bh);
    sj->tick = 0;
    sj->front_count = 0;

    /* 초기 프론트 로그: 32K 엔트리 */
    sj->front_capacity = 32768;
    sj->front_log = (SjFrontEntry *)calloc(sj->front_capacity,
                                            sizeof(SjFrontEntry));
    if (!sj->front_log) return -1;
    return 0;
}

void sj_free(SjSpatialCanvas *sj) {
    if (sj && sj->front_log) {
        free(sj->front_log);
        sj->front_log = NULL;
    }
}

void sj_reset(SjSpatialCanvas *sj) {
    if (!sj) return;
    memset(sj->canvas, 0, sizeof(sj->canvas));
    sj_wh_init(&sj->wh);
    sj_bh_init(&sj->bh);
    sj->tick = 0;
    sj->front_count = 0;
}

/* ═══════════════════════════════════════════════
 *  Foot 1: 결정론 코어
 * ═══════════════════════════════════════════════ */

void sj_front_step(SjSpatialCanvas *sj, uint8_t b0, uint8_t b1,
                   const SjFrontLabel *label) {
    uint32_t x0 = SJ_SEG0 + b0;
    uint32_t x1 = SJ_SEG1 + b1;

    /* y축 전체에 +1 누산 */
    for (uint32_t y = 0; y < SJ_H; y++) {
        sj->canvas[y * SJ_W + x0] += 1;
        sj->canvas[y * SJ_W + x1] += 1;
    }

    /* WH 기록 */
    SjWhRecord rec;
    rec.tick = sj->tick;
    rec.opcode = WH_OP_ACCUM;
    rec.label = *label;
    rec.b0 = b0;
    rec.b1 = b1;
    sj_wh_write(&sj->wh, &rec);
    sj->tick++;
}

void sj_front_unstep(SjSpatialCanvas *sj, uint8_t b0, uint8_t b1,
                     const SjFrontLabel *label) {
    uint32_t x0 = SJ_SEG0 + b0;
    uint32_t x1 = SJ_SEG1 + b1;

    for (uint32_t y = 0; y < SJ_H; y++) {
        int64_t *c0 = &sj->canvas[y * SJ_W + x0];
        int64_t *c1 = &sj->canvas[y * SJ_W + x1];
        *c0 = DK_CLAMP_I64(*c0 - 1);
        *c1 = DK_CLAMP_I64(*c1 - 1);
    }

    SjWhRecord rec;
    rec.tick = sj->tick;
    rec.opcode = WH_OP_DEACCUM;
    rec.label = *label;
    rec.b0 = b0;
    rec.b1 = b1;
    sj_wh_write(&sj->wh, &rec);
    sj->tick++;
}

bool sj_is_zero(const SjSpatialCanvas *sj) {
    for (uint32_t i = 0; i < SJ_TOTAL_CELLS; i++) {
        if (sj->canvas[i] != 0) return false;
    }
    return true;
}

/* ═══════════════════════════════════════════════
 *  Foot 2: 비결정론 어댑터 (I/O)
 * ═══════════════════════════════════════════════ */

static int ensure_front_capacity(SjSpatialCanvas *sj, uint32_t needed) {
    if (needed <= sj->front_capacity) return 0;
    uint32_t new_cap = sj->front_capacity;
    while (new_cap < needed) new_cap *= 2;
    SjFrontEntry *new_log = (SjFrontEntry *)realloc(sj->front_log,
                                                     new_cap * sizeof(SjFrontEntry));
    if (!new_log) return -1;
    sj->front_log = new_log;
    sj->front_capacity = new_cap;
    return 0;
}

int sj_encode(SjSpatialCanvas *sj, const uint8_t *data, size_t len) {
    if (!sj || !data) return -1;

    uint32_t fronts_needed = (uint32_t)((len + 1) / 2);  /* 2 bytes per front */
    if (ensure_front_capacity(sj, sj->front_count + fronts_needed) != 0)
        return -1;

    size_t pos = 0;
    uint32_t front_idx = sj->front_count;

    while (pos < len) {
        SjFrontLabel label = next_front_label(front_idx);
        uint8_t b0 = (pos < len) ? data[pos] : 0; pos++;
        uint8_t b1 = (pos < len) ? data[pos] : 0; pos++;

        sj_front_step(sj, b0, b1, &label);

        sj->front_log[sj->front_count].b0 = b0;
        sj->front_log[sj->front_count].b1 = b1;
        sj->front_count++;
        front_idx++;
    }

    /* BH 압축 실행 */
    sj_bh_compress(&sj->bh, &sj->wh, sj->tick, 64);

    return 0;
}

int sj_decode(const SjSpatialCanvas *sj, uint8_t *out, size_t *out_len) {
    if (!sj || !out || !out_len) return -1;

    size_t total = (size_t)sj->front_count * 2;
    if (*out_len < total) {
        *out_len = total;
        return -1;  /* 버퍼 부족 */
    }

    size_t pos = 0;
    for (uint32_t i = 0; i < sj->front_count; i++) {
        out[pos++] = sj->front_log[i].b0;
        out[pos++] = sj->front_log[i].b1;
    }
    *out_len = total;
    return 0;
}

int sj_decode_verify(SjSpatialCanvas *sj, uint8_t *out, size_t *out_len,
                     bool *reversible) {
    int rc = sj_decode(sj, out, out_len);
    if (rc != 0) return rc;

    /* 역누산: 역순 */
    for (int32_t i = (int32_t)sj->front_count - 1; i >= 0; i--) {
        SjFrontLabel label = next_front_label((uint32_t)i);
        sj_front_unstep(sj, sj->front_log[i].b0, sj->front_log[i].b1, &label);
    }

    *reversible = sj_is_zero(sj);
    return 0;
}

/* ═══════════════════════════════════════════════
 *  압축 패키지
 * ═══════════════════════════════════════════════ */

size_t sj_compressed_size_bound(const SjSpatialCanvas *sj) {
    /* magic(4) + header(16) + rle_data(worst case = count*5) + padding */
    return 20 + (size_t)sj->front_count * 5 + 64;
}

/**
 * BH-aware RLE 압축: 프론트 로그를 밝기 패턴 기반으로 압축.
 *
 * 형식:
 *   [0xFF][count_hi][count_lo][b0][b1] = RUN (같은 패턴 반복) → 5바이트로 최대 65535회
 *   [b0][b1]                           = LITERAL (단일 엔트리) → 2바이트
 *
 * 이 방식은 BH의 LOOP/BURST 탐지 결과와 동일하지만
 * 프론트 로그에 직접 적용하여 최종 패키지를 축소합니다.
 */
/**
 * BH-RLE 인코더.
 * 형식:
 *   [0xFF][count_hi][count_lo][b0][b1] = RUN (5바이트, count >= 1)
 *   [b0][b1]  (b0 != 0xFF)             = LITERAL (2바이트)
 *
 * b0 == 0xFF인 경우도 RUN으로 인코딩 (count=1)하여 마커 충돌 방지.
 */
static size_t rle_encode_fronts(const SjFrontEntry *log, uint32_t count,
                                uint8_t *out, size_t out_cap) {
    size_t pos = 0;
    uint32_t i = 0;

    while (i < count) {
        uint8_t b0 = log[i].b0;
        uint8_t b1 = log[i].b1;
        uint32_t run = 1;
        while (i + run < count && run < 65535 &&
               log[i + run].b0 == b0 && log[i + run].b1 == b1) {
            run++;
        }

        if (run >= 3 || b0 == 0xFF) {
            /* RUN 인코딩 (b0==0xFF도 여기서 처리하여 마커 충돌 방지) */
            if (pos + 5 > out_cap) break;
            out[pos++] = 0xFF;
            out[pos++] = (uint8_t)(run >> 8);
            out[pos++] = (uint8_t)(run & 0xFF);
            out[pos++] = b0;
            out[pos++] = b1;
        } else {
            /* LITERAL: b0 != 0xFF 보장 */
            for (uint32_t j = 0; j < run; j++) {
                if (pos + 2 > out_cap) goto done;
                out[pos++] = log[i + j].b0;
                out[pos++] = log[i + j].b1;
            }
        }
        i += run;
    }
done:
    return pos;
}

static uint32_t rle_decode_fronts(const uint8_t *data, size_t data_len,
                                  uint8_t *out, size_t out_cap) {
    size_t rpos = 0, wpos = 0;

    while (rpos < data_len && wpos + 1 < out_cap) {
        if (data[rpos] == 0xFF && rpos + 4 < data_len) {
            /* RUN */
            uint32_t run = ((uint32_t)data[rpos + 1] << 8) | data[rpos + 2];
            uint8_t b0 = data[rpos + 3];
            uint8_t b1 = data[rpos + 4];
            rpos += 5;
            for (uint32_t j = 0; j < run && wpos + 1 < out_cap; j++) {
                out[wpos++] = b0;
                out[wpos++] = b1;
            }
        } else {
            /* LITERAL (b0 != 0xFF 보장) */
            out[wpos++] = data[rpos++];
            if (rpos < data_len && wpos < out_cap)
                out[wpos++] = data[rpos++];
        }
    }
    return (uint32_t)wpos;
}

size_t sj_compress_full(const SjSpatialCanvas *sj, uint8_t *out, size_t out_cap) {
    if (!sj || !out) return 0;

    size_t pos = 0;
    uint32_t original_size = sj->front_count * 2;

    /* RLE 압축된 프론트 데이터를 임시 버퍼에 */
    size_t rle_cap = (size_t)sj->front_count * 5 + 64;
    uint8_t *rle_buf = (uint8_t *)malloc(rle_cap);
    if (!rle_buf) return 0;
    size_t rle_len = rle_encode_fronts(sj->front_log, sj->front_count,
                                       rle_buf, rle_cap);

    size_t needed = 20 + rle_len;
    if (out_cap < needed) { free(rle_buf); return 0; }

    /* Magic */
    memcpy(out + pos, "SJSC", 4); pos += 4;

    /* Header */
    memcpy(out + pos, &sj->front_count, 4); pos += 4;
    memcpy(out + pos, &original_size, 4); pos += 4;
    uint32_t rle_len32 = (uint32_t)rle_len;
    memcpy(out + pos, &rle_len32, 4); pos += 4;

    /* 압축 방식 플래그 */
    uint32_t flags = 0x01;  /* 0x01 = BH-RLE */
    memcpy(out + pos, &flags, 4); pos += 4;

    /* RLE 압축 프론트 데이터 */
    memcpy(out + pos, rle_buf, rle_len);
    pos += rle_len;

    free(rle_buf);
    return pos;
}

int sj_decompress_full(const uint8_t *compressed, size_t comp_len,
                       uint8_t *out, size_t *out_len) {
    if (!compressed || !out || !out_len) return -1;
    if (comp_len < 20) return -1;

    /* Magic 확인 */
    if (memcmp(compressed, "SJSC", 4) != 0) return -1;

    uint32_t front_count, original_size, rle_len, flags;
    memcpy(&front_count, compressed + 4, 4);
    memcpy(&original_size, compressed + 8, 4);
    memcpy(&rle_len, compressed + 12, 4);
    memcpy(&flags, compressed + 16, 4);

    if (*out_len < original_size) {
        *out_len = original_size;
        return -1;
    }

    if (20 + rle_len > comp_len) return -1;

    if (flags & 0x01) {
        /* BH-RLE 디코딩 */
        uint32_t decoded = rle_decode_fronts(compressed + 20, rle_len,
                                             out, *out_len);
        *out_len = (decoded < original_size) ? decoded : original_size;
    } else {
        /* Raw copy */
        memcpy(out, compressed + 20, original_size);
        *out_len = original_size;
    }
    return 0;
}

/* ═══════════════════════════════════════════════
 *  통계
 * ═══════════════════════════════════════════════ */

SjCompressStats sj_stats(const SjSpatialCanvas *sj) {
    SjCompressStats s;
    memset(&s, 0, sizeof(s));

    s.original_bytes = sj->front_count * 2;

    /* 압축 크기 계산 */
    size_t bound = sj_compressed_size_bound(sj);
    uint8_t *tmp = (uint8_t *)malloc(bound);
    if (tmp) {
        s.compressed_bytes = (uint32_t)sj_compress_full(sj, tmp, bound);
        free(tmp);
    }

    s.compression_ratio = (s.compressed_bytes > 0)
        ? (double)s.original_bytes / (double)s.compressed_bytes
        : 0.0;

    /* 캔버스 분석 */
    uint32_t nonzero = 0;
    int64_t max_val = 0;
    for (uint32_t i = 0; i < SJ_TOTAL_CELLS; i++) {
        if (sj->canvas[i] != 0) nonzero++;
        if (sj->canvas[i] > max_val) max_val = sj->canvas[i];
    }
    s.canvas_nonzero = nonzero;
    s.canvas_sparsity = 1.0 - (double)nonzero / (double)SJ_TOTAL_CELLS;
    s.canvas_max_brightness = max_val;

    /* WH/BH */
    s.wh_records = sj->wh.count;
    s.bh_summaries = sj->bh.summary_count;
    s.bh_ticks_saved = sj->bh.ticks_saved;

    return s;
}

void sj_print_stats(const SjCompressStats *s) {
    printf("\n");
    printf("  ┌─ SJ Spatial Compress Stats ──────────────\n");
    printf("  │ Original:      %10u bytes\n", s->original_bytes);
    printf("  │ Compressed:    %10u bytes\n", s->compressed_bytes);
    printf("  │ Ratio:         %10.2fx\n", s->compression_ratio);
    printf("  ├─ Canvas ─────────────────────────────────\n");
    printf("  │ Nonzero:       %10u / %u\n", s->canvas_nonzero, SJ_TOTAL_CELLS);
    printf("  │ Sparsity:      %9.1f%%\n", s->canvas_sparsity * 100.0);
    printf("  │ Max Brightness: %9lld\n", (long long)s->canvas_max_brightness);
    printf("  ├─ WH/BH ─────────────────────────────────\n");
    printf("  │ WH Records:    %10u\n", s->wh_records);
    printf("  │ BH Summaries:  %10u\n", s->bh_summaries);
    printf("  │ BH Ticks Saved: %9llu\n", (unsigned long long)s->bh_ticks_saved);
    printf("  └──────────────────────────────────────────\n");
}
