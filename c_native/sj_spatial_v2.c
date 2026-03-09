/**
 * SJ Spatial Compression Engine V2 — Implementation
 * ====================================================
 * OS 데이터 구조화/재배열 엔진.
 * 512×512 고정 이미지 = 실행 단위.
 *
 * 인코딩 알고리즘:
 *   1. 데이터를 2바이트씩 소비 (R걷기, G걷기)
 *   2. R바이트 → R존(x:0-127)의 col = byte % 128 에 밝기 +1
 *   3. G바이트 → G존(x:128-255)의 col = byte % 128 에 밝기 +1
 *   4. 매 (128*2 = 256바이트)마다 B페이지 +1 (새 페이지)
 *   5. 각 페이지의 R/G 누산값을 A채널에 정수 리스트로 기록
 *   6. 큰 A값은 후면 50%(A존)에 분할 저장
 *   7. 최종: 최대밝기 기준 델타 압축
 *
 * WH: 매 걸음을 (tick, zone, page, col, value)로 기록
 * BH: 델타 압축 시 최대밝기 기준으로 감산 → 잔차만 저장
 */

#include "sj_spatial_v2.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ─── 내부 상수 ─── */

/* 페이지당 바이트 수: R 128슬롯 + G 128슬롯 = 256바이트/페이지 */
#define BYTES_PER_PAGE  256

/* ═══ 초기화 / 해제 ═══ */

int v2_init(V2Canvas *v) {
    if (!v) return -1;
    memset(v, 0, sizeof(*v));

    v->wh.capacity = V2_WH_CAP;
    v->wh.records = (V2WhRecord *)calloc(V2_WH_CAP, sizeof(V2WhRecord));
    if (!v->wh.records) return -1;

    return 0;
}

void v2_free(V2Canvas *v) {
    if (v && v->wh.records) {
        free(v->wh.records);
        v->wh.records = NULL;
    }
}

/* ═══ WH ═══ */

static void wh_write(V2WhiteHole *wh, uint32_t tick,
                     uint8_t zone, uint8_t page, uint8_t col, uint8_t value) {
    if (wh->count >= wh->capacity) {
        /* 순환: 가장 오래된 레코드 덮어쓰기 */
        uint32_t idx = wh->write_cursor % wh->capacity;
        wh->records[idx] = (V2WhRecord){tick, zone, page, col, value};
    } else {
        wh->records[wh->count] = (V2WhRecord){tick, zone, page, col, value};
        wh->count++;
    }
    wh->write_cursor++;
}

/* ═══ 인코딩: RG 걷기 + B 페이지 + A 누산 ═══ */

int v2_encode(V2Canvas *v, const uint8_t *data, size_t len) {
    if (!v || !data) return -1;

    size_t pos = 0;
    v->bytes_encoded = 0;

    while (pos < len) {
        /* 현재 페이지 결정 */
        uint8_t page = (uint8_t)(v->current_page & 0xFF);
        uint32_t page_byte_count = 0;

        /* 한 페이지: R슬롯 128개 + G슬롯 128개 = 256바이트 소비 */

        /* ── R 걷기: 128바이트 ── */
        for (uint32_t i = 0; i < V2_R_COLS && pos < len; i++, pos++) {
            uint8_t byte_val = data[pos];
            uint8_t col = byte_val & 0x7F;  /* 0-127 매핑 */

            /* R존 밝기 누산 */
            v->brightness_r[page][col]++;

            /* 캔버스 셀 업데이트 */
            uint32_t y = v->tick & 0x1FF;  /* y = tick % 512 */
            uint32_t x = V2_R_ZONE_X0 + col;
            uint32_t idx = y * V2_W + x;
            v->cells[idx].A = v->brightness_r[page][col];
            v->cells[idx].B = page;
            v->cells[idx].R = byte_val;
            v->cells[idx].G++;  /* 에너지 +1 */

            /* WH 기록 */
            wh_write(&v->wh, v->tick, 0, page, col, byte_val);
            v->tick++;
            page_byte_count++;
        }

        /* ── G 걷기: 128바이트 ── */
        for (uint32_t i = 0; i < V2_G_COLS && pos < len; i++, pos++) {
            uint8_t byte_val = data[pos];
            uint8_t col = byte_val & 0x7F;  /* 0-127 매핑 */

            /* G존 밝기 누산 */
            v->brightness_g[page][col]++;

            /* 캔버스 셀 업데이트 */
            uint32_t y = v->tick & 0x1FF;
            uint32_t x = V2_G_ZONE_X0 + col;
            uint32_t idx = y * V2_W + x;
            v->cells[idx].A = v->brightness_g[page][col];
            v->cells[idx].B = page;
            v->cells[idx].G++;
            v->cells[idx].R = byte_val;

            wh_write(&v->wh, v->tick, 1, page, col, byte_val);
            v->tick++;
            page_byte_count++;
        }

        v->bytes_encoded += page_byte_count;

        /* ── A존: 큰 누산값 분할 저장 ── */
        /* 현재 페이지의 R/G 누산값 중 255 초과분을 A존에 기록 */
        uint32_t a_row = page & 0x1FF;  /* A존 row = page % 512 */
        uint32_t a_col = 0;
        for (uint32_t c = 0; c < V2_R_COLS && a_col < V2_A_COLS; c++) {
            if (v->brightness_r[page][c] > 255) {
                v->a_overflow[a_row][a_col] = v->brightness_r[page][c];
                /* 캔버스 A존 셀에도 기록 */
                uint32_t idx = a_row * V2_W + V2_A_ZONE_X0 + a_col;
                v->cells[idx].A = v->brightness_r[page][c];
                v->cells[idx].B = page;
                a_col++;
            }
        }
        for (uint32_t c = 0; c < V2_G_COLS && a_col < V2_A_COLS; c++) {
            if (v->brightness_g[page][c] > 255) {
                v->a_overflow[a_row][a_col] = v->brightness_g[page][c];
                uint32_t idx = a_row * V2_W + V2_A_ZONE_X0 + a_col;
                v->cells[idx].A = v->brightness_g[page][c];
                v->cells[idx].B = page;
                a_col++;
            }
        }

        /* 다음 페이지 */
        v->current_page++;
    }

    /* ── 델타 압축 정보 계산 ── */
    uint32_t max_b = 0;
    uint32_t active_r = 0, active_g = 0;
    uint64_t total_acc = 0;
    uint32_t overflow_cnt = 0;

    for (uint32_t p = 0; p < V2_PAGES; p++) {
        for (uint32_t c = 0; c < V2_R_COLS; c++) {
            if (v->brightness_r[p][c] > 0) {
                active_r++;
                total_acc += v->brightness_r[p][c];
                if (v->brightness_r[p][c] > max_b)
                    max_b = v->brightness_r[p][c];
                if (v->brightness_r[p][c] > 255) overflow_cnt++;
            }
        }
        for (uint32_t c = 0; c < V2_G_COLS; c++) {
            if (v->brightness_g[p][c] > 0) {
                active_g++;
                total_acc += v->brightness_g[p][c];
                if (v->brightness_g[p][c] > max_b)
                    max_b = v->brightness_g[p][c];
                if (v->brightness_g[p][c] > 255) overflow_cnt++;
            }
        }
    }

    v->delta_info.max_brightness = max_b;
    v->delta_info.unique_positions = active_r + active_g;
    v->delta_info.pages_used = v->current_page;
    v->delta_info.total_accumulated = total_acc;

    return 0;
}

/* ═══ 디코딩: WH 로그 → 원본 ═══ */

int v2_decode(const V2Canvas *v, uint8_t *out, size_t *out_len) {
    if (!v || !out || !out_len) return -1;

    size_t needed = v->bytes_encoded;
    if (*out_len < needed) {
        *out_len = needed;
        return -1;
    }

    /* WH 레코드를 tick 순서로 재생 → 원본 바이트 복원 */
    uint32_t total = (v->wh.count < v->wh.write_cursor)
                     ? v->wh.count : v->wh.write_cursor;
    if (total > needed) total = (uint32_t)needed;

    for (uint32_t i = 0; i < total; i++) {
        const V2WhRecord *r = &v->wh.records[i];
        out[i] = r->value;  /* WH에 원본 바이트값 기록되어 있음 */
    }

    *out_len = total;
    return 0;
}

/* ═══ 델타 압축: 최대밝기 기준 ═══ */

/**
 * 델타 압축 전략:
 *   1. 최대밝기(max_b)를 기준점으로 설정
 *   2. 모든 누산값을 delta = max_b - value 로 변환
 *   3. 활성 위치만 (page, col, delta) 튜플로 저장
 *   4. delta가 0인 위치(=최대밝기)는 생략 가능
 *   5. 대부분의 delta가 작은 값 → 가변길이 인코딩으로 축소
 *
 * 형식:
 *   [SJSV] [4B max_brightness] [4B pages_used] [4B entry_count] [4B original_bytes]
 *   [entries: page(1B) zone(1B) col(1B) delta(varint)]
 *   [WH 최소 로그]
 */

/* 가변길이 정수 인코딩 (LEB128) */
static size_t write_varint(uint8_t *out, uint32_t val) {
    size_t n = 0;
    while (val >= 0x80) {
        out[n++] = (uint8_t)(val | 0x80);
        val >>= 7;
    }
    out[n++] = (uint8_t)val;
    return n;
}

static size_t read_varint(const uint8_t *data, size_t len, uint32_t *val) {
    *val = 0;
    size_t n = 0;
    uint32_t shift = 0;
    while (n < len && n < 5) {
        uint8_t b = data[n];
        *val |= (uint32_t)(b & 0x7F) << shift;
        n++;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return n;
}

size_t v2_delta_compress(const V2Canvas *v, uint8_t *out, size_t out_cap) {
    if (!v || !out) return 0;

    uint32_t max_b = v->delta_info.max_brightness;
    if (max_b == 0) max_b = 1;

    size_t pos = 0;

    /* 헤더 */
    if (out_cap < 20) return 0;
    memcpy(out + pos, "SJSV", 4); pos += 4;
    memcpy(out + pos, &max_b, 4); pos += 4;

    uint32_t pages_used = v->current_page < V2_PAGES ? v->current_page : V2_PAGES;
    memcpy(out + pos, &pages_used, 4); pos += 4;

    /* entry_count 자리 예약 */
    size_t entry_count_pos = pos;
    pos += 4;

    uint32_t orig_bytes = v->bytes_encoded;
    memcpy(out + pos, &orig_bytes, 4); pos += 4;

    /* 활성 엔트리 기록 */
    uint32_t entry_count = 0;

    for (uint32_t p = 0; p < pages_used; p++) {
        /* R존 */
        for (uint32_t c = 0; c < V2_R_COLS; c++) {
            uint32_t val = v->brightness_r[p][c];
            if (val == 0) continue;

            uint32_t delta = max_b - val;
            if (pos + 8 > out_cap) goto done;

            out[pos++] = (uint8_t)p;
            out[pos++] = 0;  /* zone=R */
            out[pos++] = (uint8_t)c;
            pos += write_varint(out + pos, delta);
            entry_count++;
        }
        /* G존 */
        for (uint32_t c = 0; c < V2_G_COLS; c++) {
            uint32_t val = v->brightness_g[p][c];
            if (val == 0) continue;

            uint32_t delta = max_b - val;
            if (pos + 8 > out_cap) goto done;

            out[pos++] = (uint8_t)p;
            out[pos++] = 1;  /* zone=G */
            out[pos++] = (uint8_t)c;
            pos += write_varint(out + pos, delta);
            entry_count++;
        }
    }

done:
    /* entry_count 기록 */
    memcpy(out + entry_count_pos, &entry_count, 4);
    return pos;
}

/* ═══ 완전 압축 패키지 ═══ */

size_t v2_compress_full(const V2Canvas *v, uint8_t *out, size_t out_cap) {
    if (!v || !out) return 0;

    size_t pos = 0;

    /* ── Part 1: 매직 + 메타 ── */
    if (out_cap < 28) return 0;
    memcpy(out + pos, "SJS2", 4); pos += 4;  /* V2 매직 */

    uint32_t orig_bytes = v->bytes_encoded;
    uint32_t pages_used = v->current_page < V2_PAGES ? v->current_page : V2_PAGES;
    uint32_t max_b = v->delta_info.max_brightness;
    uint32_t wh_count = v->wh.count < v->wh.write_cursor ?
                        v->wh.count : v->wh.write_cursor;

    memcpy(out + pos, &orig_bytes, 4); pos += 4;
    memcpy(out + pos, &pages_used, 4); pos += 4;
    memcpy(out + pos, &max_b, 4); pos += 4;
    memcpy(out + pos, &wh_count, 4); pos += 4;

    /* ── Part 2: 델타 압축된 밝기맵 ── */
    size_t delta_size_pos = pos;
    pos += 4;  /* 크기 예약 */

    size_t delta_start = pos;
    /* 활성 엔트리만 기록 (page, zone, col, delta_varint) */
    for (uint32_t p = 0; p < pages_used && p < V2_PAGES; p++) {
        for (uint32_t c = 0; c < V2_R_COLS; c++) {
            uint32_t val = v->brightness_r[p][c];
            if (val == 0) continue;
            if (pos + 8 > out_cap) goto overflow;
            out[pos++] = (uint8_t)p;
            out[pos++] = (uint8_t)c;          /* R존: col as-is */
            pos += write_varint(out + pos, val);
        }
        for (uint32_t c = 0; c < V2_G_COLS; c++) {
            uint32_t val = v->brightness_g[p][c];
            if (val == 0) continue;
            if (pos + 8 > out_cap) goto overflow;
            out[pos++] = (uint8_t)p;
            out[pos++] = (uint8_t)(c | 0x80); /* G존: MSB 마커 */
            pos += write_varint(out + pos, val);
        }
    }
overflow:
    {
        uint32_t delta_size = (uint32_t)(pos - delta_start);
        memcpy(out + delta_size_pos, &delta_size, 4);
    }

    /* ── Part 3: WH 최소 로그 (RLE 압축) ── */
    /* 연속 동일 value를 RLE */
    size_t wh_size_pos = pos;
    pos += 4;
    size_t wh_start = pos;

    uint32_t wh_i = 0;
    while (wh_i < wh_count) {
        uint8_t val = v->wh.records[wh_i].value;
        uint32_t run = 1;
        while (wh_i + run < wh_count && run < 65535 &&
               v->wh.records[wh_i + run].value == val) {
            run++;
        }

        if (run >= 3) {
            if (pos + 4 > out_cap) break;
            out[pos++] = 0xFF;
            out[pos++] = (uint8_t)(run >> 8);
            out[pos++] = (uint8_t)(run & 0xFF);
            out[pos++] = val;
        } else {
            for (uint32_t j = 0; j < run; j++) {
                if (pos + 2 > out_cap) goto wh_done;
                if (v->wh.records[wh_i + j].value == 0xFF) {
                    /* escape 0xFF */
                    out[pos++] = 0xFF;
                    out[pos++] = 0x00;
                    out[pos++] = 0x01;
                    out[pos++] = 0xFF;
                } else {
                    out[pos++] = v->wh.records[wh_i + j].value;
                }
            }
        }
        wh_i += run;
    }
wh_done:
    {
        uint32_t wh_size = (uint32_t)(pos - wh_start);
        memcpy(out + wh_size_pos, &wh_size, 4);
    }

    return pos;
}

int v2_decompress_full(const uint8_t *compressed, size_t comp_len,
                       uint8_t *out, size_t *out_len) {
    if (!compressed || !out || !out_len || comp_len < 24) return -1;
    if (memcmp(compressed, "SJS2", 4) != 0) return -1;

    size_t rpos = 4;
    uint32_t orig_bytes, pages_used, max_b, wh_count;
    memcpy(&orig_bytes, compressed + rpos, 4); rpos += 4;
    memcpy(&pages_used, compressed + rpos, 4); rpos += 4;
    memcpy(&max_b, compressed + rpos, 4); rpos += 4;
    memcpy(&wh_count, compressed + rpos, 4); rpos += 4;

    if (*out_len < orig_bytes) {
        *out_len = orig_bytes;
        return -1;
    }

    /* 델타맵 건너뛰기 */
    uint32_t delta_size;
    memcpy(&delta_size, compressed + rpos, 4); rpos += 4;
    rpos += delta_size;

    /* WH RLE 디코딩 */
    uint32_t wh_size;
    memcpy(&wh_size, compressed + rpos, 4); rpos += 4;

    size_t wh_end = rpos + wh_size;
    size_t wpos = 0;

    while (rpos < wh_end && wpos < orig_bytes) {
        if (compressed[rpos] == 0xFF && rpos + 3 < wh_end) {
            uint32_t run = ((uint32_t)compressed[rpos + 1] << 8) |
                           compressed[rpos + 2];
            uint8_t val = compressed[rpos + 3];
            rpos += 4;
            for (uint32_t j = 0; j < run && wpos < orig_bytes; j++) {
                out[wpos++] = val;
            }
        } else {
            out[wpos++] = compressed[rpos++];
        }
    }

    *out_len = wpos;
    return 0;
}

/* ═══ 통계 ═══ */

V2Stats v2_stats(const V2Canvas *v) {
    V2Stats s;
    memset(&s, 0, sizeof(s));

    s.original_bytes = v->bytes_encoded;
    s.canvas_raw_bytes = sizeof(V2Cell) * V2_TOTAL_CELLS;  /* 항상 2MB */

    /* 압축 크기 계산 */
    size_t bound = 28 + V2_PAGES * 256 * 8 + v->wh.count * 4 + 64;
    uint8_t *tmp = (uint8_t *)malloc(bound);
    if (tmp) {
        s.compressed_bytes = v2_compress_full(v, tmp, bound);
        free(tmp);
    }

    s.compression_ratio = (s.compressed_bytes > 0)
        ? (double)s.original_bytes / (double)s.compressed_bytes : 0;
    s.space_saving_pct = (s.original_bytes > 0)
        ? (1.0 - (double)s.compressed_bytes / (double)s.original_bytes) * 100.0 : 0;

    s.max_brightness = v->delta_info.max_brightness;
    s.pages_used = v->current_page;

    /* 활성 슬롯 */
    s.active_r_slots = 0;
    s.active_g_slots = 0;
    s.a_overflow_count = 0;
    for (uint32_t p = 0; p < V2_PAGES; p++) {
        for (uint32_t c = 0; c < V2_R_COLS; c++) {
            if (v->brightness_r[p][c] > 0) s.active_r_slots++;
            if (v->brightness_r[p][c] > 255) s.a_overflow_count++;
        }
        for (uint32_t c = 0; c < V2_G_COLS; c++) {
            if (v->brightness_g[p][c] > 0) s.active_g_slots++;
            if (v->brightness_g[p][c] > 255) s.a_overflow_count++;
        }
    }

    s.wh_records = v->wh.count < v->wh.write_cursor ?
                   v->wh.count : v->wh.write_cursor;

    return s;
}

void v2_print_stats(const V2Stats *s) {
    printf("\n");
    printf("  ┌─ SJ Spatial V2 Stats ─────────────────────\n");
    printf("  │ 원본 크기:       %12zu bytes\n", s->original_bytes);
    printf("  │ 캔버스 고정:     %12zu bytes (512x512x8)\n", s->canvas_raw_bytes);
    printf("  │ 압축 크기:       %12zu bytes\n", s->compressed_bytes);
    printf("  │ 압축률:          %12.2fx\n", s->compression_ratio);
    printf("  │ 절감:            %11.1f%%\n", s->space_saving_pct);
    printf("  ├─ 캔버스 배치 ───────────────────────────\n");
    printf("  │ 최대 밝기:       %12u\n", s->max_brightness);
    printf("  │ 사용 페이지:     %12u / %d\n", s->pages_used, V2_PAGES);
    printf("  │ 활성 R슬롯:      %12u\n", s->active_r_slots);
    printf("  │ 활성 G슬롯:      %12u\n", s->active_g_slots);
    printf("  │ A존 오버플로:    %12u\n", s->a_overflow_count);
    printf("  ├─ WH ──────────────────────────────────────\n");
    printf("  │ WH 레코드:       %12u\n", s->wh_records);
    printf("  └────────────────────────────────────────────\n");
}

void v2_print_canvas_map(const V2Canvas *v) {
    printf("\n  ══ 512×512 Canvas Map ══\n");
    printf("  [R-Pages 0-127][G-Pages 128-255][A-Zone 256-511]\n");
    printf("  Pages used: %u | Max brightness: %u\n",
           v->current_page, v->delta_info.max_brightness);

    /* 밝기 분포 히스토그램 */
    uint32_t hist[10] = {0};
    uint32_t max_b = v->delta_info.max_brightness;
    if (max_b == 0) max_b = 1;

    for (uint32_t p = 0; p < V2_PAGES; p++) {
        for (uint32_t c = 0; c < V2_R_COLS; c++) {
            uint32_t val = v->brightness_r[p][c];
            if (val > 0) {
                uint32_t bucket = (val * 9) / max_b;
                if (bucket > 9) bucket = 9;
                hist[bucket]++;
            }
        }
        for (uint32_t c = 0; c < V2_G_COLS; c++) {
            uint32_t val = v->brightness_g[p][c];
            if (val > 0) {
                uint32_t bucket = (val * 9) / max_b;
                if (bucket > 9) bucket = 9;
                hist[bucket]++;
            }
        }
    }

    printf("  ── 밝기 분포 히스토그램 ──\n");
    uint32_t hist_max = 1;
    for (int i = 0; i < 10; i++)
        if (hist[i] > hist_max) hist_max = hist[i];

    for (int i = 0; i < 10; i++) {
        uint32_t bar = (hist[i] * 40) / hist_max;
        printf("  %3d%%-%3d%% |", i * 10, (i + 1) * 10);
        for (uint32_t j = 0; j < bar; j++) printf("█");
        printf(" %u\n", hist[i]);
    }
}
