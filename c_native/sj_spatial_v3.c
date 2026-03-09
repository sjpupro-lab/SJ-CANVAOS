/**
 * SJ Spatial Compression Engine V3 — Streaming Implementation
 * =============================================================
 *
 * B값 페이지 구조:
 *   B [0,63]    (25%) = R페이지. RG걷기 중 R걸음의 페이지.
 *   B [64,127]  (25%) = G페이지. RG걷기 중 G걸음의 페이지.
 *   B [128,255] (50%) = A정수존. 큰 누산값을 R/G 페이지와 나눠 저장.
 *
 * RG 걷기:
 *   매 256바이트(1페이지) = R 128바이트 + G 128바이트.
 *   R바이트 → col = byte & 0x7F → brightness_r[page][col]++
 *   G바이트 → col = byte & 0x7F → brightness_g[page][col]++
 *   page++ (0~63 순환, 64 도달 시 0으로, cycles++)
 *
 * A존 오버플로:
 *   brightness > 255일 때, 상위 부분을 A존에 분할 저장.
 *   a_store[a_page][col] = brightness >> 8  (상위 비트)
 *   캔버스 셀에는 하위 8비트만 남김 → 델타 압축 효율 극대화.
 *
 * 델타 압축:
 *   max_brightness에서 각 값을 빼서 delta만 저장.
 *   → 밝기가 비슷한 영역은 delta≈0 → varint 1바이트.
 *
 * 스트리밍:
 *   인코더: feed(chunk) 반복 → 캔버스 누산 + WH 파일 기록.
 *   디코더: WH 파일에서 순차 읽기 → 원본 복원.
 *   메모리: 캔버스(~3MB) + WH 버퍼(4KB) = 고정.
 */

#include "sj_spatial_v3.h"
#include <stdlib.h>
#include <string.h>

/* ─── varint (LEB128) ─── */

static size_t write_varint(uint8_t *out, uint64_t val) {
    size_t n = 0;
    while (val >= 0x80) {
        out[n++] = (uint8_t)(val | 0x80);
        val >>= 7;
    }
    out[n++] = (uint8_t)val;
    return n;
}

static size_t read_varint(const uint8_t *data, size_t len, uint64_t *val) {
    *val = 0;
    size_t n = 0;
    unsigned shift = 0;
    while (n < len && n < 10) {
        uint8_t b = data[n];
        *val |= (uint64_t)(b & 0x7F) << shift;
        n++;
        if (!(b & 0x80)) break;
        shift += 7;
    }
    return n;
}

/* ─── WH 스트리밍: RLE 압축 기록 ─── */

/* WH 스트림 형식:
 *   연속 동일 바이트: [0xFF][len_hi][len_lo][value] (4바이트, 최대 65535)
 *   단일 바이트:      [value] (1바이트, value != 0xFF)
 *   0xFF 리터럴:      [0xFF][0x00][0x01][0xFF] (4바이트)
 */

static void wh_stream_write(FILE *f, const uint8_t *data, size_t len) {
    size_t i = 0;
    while (i < len) {
        uint8_t val = data[i];
        uint32_t run = 1;
        while (i + run < len && run < 65535 && data[i + run] == val)
            run++;

        if (run >= 3 || val == 0xFF) {
            uint8_t hdr[4];
            hdr[0] = 0xFF;
            hdr[1] = (uint8_t)(run >> 8);
            hdr[2] = (uint8_t)(run & 0xFF);
            hdr[3] = val;
            fwrite(hdr, 1, 4, f);
        } else {
            fwrite(data + i, 1, run, f);
        }
        i += run;
    }
}

static size_t wh_stream_read(FILE *f, uint8_t *out, size_t want) {
    size_t got = 0;
    while (got < want && !feof(f)) {
        int c = fgetc(f);
        if (c == EOF) break;

        if (c == 0xFF) {
            int hi = fgetc(f);
            int lo = fgetc(f);
            int val = fgetc(f);
            if (hi == EOF || lo == EOF || val == EOF) break;
            uint32_t run = ((uint32_t)hi << 8) | (uint32_t)lo;
            for (uint32_t j = 0; j < run && got < want; j++)
                out[got++] = (uint8_t)val;
        } else {
            out[got++] = (uint8_t)c;
        }
    }
    return got;
}

/* ═══ 인코더 ═══ */

int v3_enc_init(V3Encoder *enc, const char *wh_path) {
    if (!enc || !wh_path) return -1;
    memset(enc, 0, sizeof(*enc));

    enc->wh_stream = fopen(wh_path, "wb");
    if (!enc->wh_stream) return -1;

    return 0;
}

int v3_enc_feed(V3Encoder *enc, const uint8_t *data, size_t len) {
    if (!enc || !data) return -1;

    V3Canvas *c = &enc->canvas;
    size_t pos = 0;

    while (pos < len) {
        uint32_t page = c->current_page;

        /* ── R 걷기: 최대 128바이트 ── */
        size_t r_avail = len - pos;
        if (r_avail > V3_COLS) r_avail = V3_COLS;

        for (size_t i = 0; i < r_avail; i++) {
            uint8_t byte_val = data[pos + i];
            uint8_t col = byte_val & 0x7F;
            c->brightness_r[page][col]++;

            uint64_t bv = c->brightness_r[page][col];
            if (bv > c->max_brightness)
                c->max_brightness = bv;

            /* A존 오버플로 분할: 상위 비트 */
            if (bv > 255) {
                uint32_t a_page = (page * 2) % V3_A_PAGES;
                c->a_store[a_page][col] = bv >> 8;
            }
        }

        /* WH 스트림 기록 (R 걸음) */
        wh_stream_write(enc->wh_stream, data + pos, r_avail);
        enc->wh_bytes_written += r_avail;
        pos += r_avail;
        c->total_bytes += r_avail;

        if (pos >= len) break;

        /* ── G 걷기: 최대 128바이트 ── */
        size_t g_avail = len - pos;
        if (g_avail > V3_COLS) g_avail = V3_COLS;

        for (size_t i = 0; i < g_avail; i++) {
            uint8_t byte_val = data[pos + i];
            uint8_t col = byte_val & 0x7F;
            c->brightness_g[page][col]++;

            uint64_t bv = c->brightness_g[page][col];
            if (bv > c->max_brightness)
                c->max_brightness = bv;

            if (bv > 255) {
                uint32_t a_page = (page * 2 + 1) % V3_A_PAGES;
                c->a_store[a_page][col] = bv >> 8;
            }
        }

        wh_stream_write(enc->wh_stream, data + pos, g_avail);
        enc->wh_bytes_written += g_avail;
        pos += g_avail;
        c->total_bytes += g_avail;

        /* 페이지 전진 (64에서 순환) */
        c->current_page++;
        if (c->current_page >= V3_R_PAGES) {
            c->current_page = 0;
            c->page_cycles++;
        }
    }

    return 0;
}

int v3_enc_finish(V3Encoder *enc) {
    if (!enc) return -1;
    if (enc->wh_stream) fflush(enc->wh_stream);
    return 0;
}

/* ═══ 델타 압축 출력 ═══ */

size_t v3_enc_write_delta(const V3Encoder *enc, const char *delta_path) {
    if (!enc || !delta_path) return 0;

    FILE *f = fopen(delta_path, "wb");
    if (!f) return 0;

    const V3Canvas *c = &enc->canvas;
    uint64_t max_b = c->max_brightness;
    if (max_b == 0) max_b = 1;

    /* 헤더 */
    V3Header hdr;
    memcpy(hdr.magic, "SJS3", 4);
    hdr.original_bytes = c->total_bytes;
    hdr.max_brightness = max_b;
    hdr.page_cycles = c->page_cycles;

    /* WH 파일 크기 */
    if (enc->wh_stream) {
        long cur = ftell(enc->wh_stream);
        hdr.wh_size = (cur > 0) ? (uint64_t)cur : enc->wh_bytes_written;
    } else {
        hdr.wh_size = enc->wh_bytes_written;
    }

    /* 활성 엔트리 카운트 (먼저 세기) */
    uint32_t active = 0;
    for (uint32_t p = 0; p < V3_R_PAGES; p++)
        for (uint32_t col = 0; col < V3_COLS; col++)
            if (c->brightness_r[p][col] > 0) active++;
    for (uint32_t p = 0; p < V3_G_PAGES; p++)
        for (uint32_t col = 0; col < V3_COLS; col++)
            if (c->brightness_g[p][col] > 0) active++;
    hdr.active_entries = active;

    fwrite(&hdr, sizeof(hdr), 1, f);

    /* 델타 엔트리: (page, zone_col, delta_varint) */
    /* zone_col: R존 col = col (0-127), G존 col = col | 0x80 */
    uint8_t buf[16];
    size_t total_written = sizeof(hdr);

    for (uint32_t p = 0; p < V3_R_PAGES; p++) {
        for (uint32_t col = 0; col < V3_COLS; col++) {
            uint64_t val = c->brightness_r[p][col];
            if (val == 0) continue;
            uint64_t delta = max_b - val;

            buf[0] = (uint8_t)p;
            buf[1] = (uint8_t)col;  /* R존: 0-127 */
            size_t vn = write_varint(buf + 2, delta);
            fwrite(buf, 1, 2 + vn, f);
            total_written += 2 + vn;
        }
    }
    for (uint32_t p = 0; p < V3_G_PAGES; p++) {
        for (uint32_t col = 0; col < V3_COLS; col++) {
            uint64_t val = c->brightness_g[p][col];
            if (val == 0) continue;
            uint64_t delta = max_b - val;

            buf[0] = (uint8_t)(V3_R_PAGES + p);  /* G존: 64-127 */
            buf[1] = (uint8_t)col;
            size_t vn = write_varint(buf + 2, delta);
            fwrite(buf, 1, 2 + vn, f);
            total_written += 2 + vn;
        }
    }

    /* A존 오버플로 엔트리 */
    for (uint32_t ap = 0; ap < V3_A_PAGES; ap++) {
        for (uint32_t col = 0; col < V3_COLS; col++) {
            uint64_t val = c->a_store[ap][col];
            if (val == 0) continue;

            buf[0] = (uint8_t)(V3_B_A_START + ap);  /* A존: 128-255 */
            buf[1] = (uint8_t)col;
            size_t vn = write_varint(buf + 2, val);
            fwrite(buf, 1, 2 + vn, f);
            total_written += 2 + vn;
        }
    }

    fclose(f);
    return total_written;
}

void v3_enc_free(V3Encoder *enc) {
    if (enc && enc->wh_stream) {
        fclose(enc->wh_stream);
        enc->wh_stream = NULL;
    }
}

/* ═══ 디코더 (스트리밍) ═══ */

int v3_dec_init(V3Decoder *dec, const char *wh_path, uint64_t total_bytes) {
    if (!dec || !wh_path) return -1;
    memset(dec, 0, sizeof(*dec));

    dec->wh_stream = fopen(wh_path, "rb");
    if (!dec->wh_stream) return -1;
    dec->total_bytes = total_bytes;
    return 0;
}

int v3_dec_read(V3Decoder *dec, uint8_t *out, size_t len, size_t *actual) {
    if (!dec || !out || !actual) return -1;

    size_t remaining = (size_t)(dec->total_bytes - dec->bytes_read);
    size_t want = (len < remaining) ? len : remaining;
    if (want == 0) { *actual = 0; return 0; }

    *actual = wh_stream_read(dec->wh_stream, out, want);
    dec->bytes_read += *actual;
    return 0;
}

void v3_dec_free(V3Decoder *dec) {
    if (dec && dec->wh_stream) {
        fclose(dec->wh_stream);
        dec->wh_stream = NULL;
    }
}

/* ═══ 통계 ═══ */

V3Stats v3_stats(const V3Encoder *enc, const char *wh_path, const char *delta_path) {
    V3Stats s;
    memset(&s, 0, sizeof(s));

    const V3Canvas *c = &enc->canvas;
    s.original_bytes = c->total_bytes;
    s.max_brightness = c->max_brightness;
    s.page_cycles = c->page_cycles;

    /* 캔버스 메모리 */
    s.canvas_memory = sizeof(V3Canvas);

    /* WH 파일 크기 */
    if (wh_path) {
        FILE *f = fopen(wh_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            s.wh_compressed = (uint64_t)ftell(f);
            fclose(f);
        }
    }

    /* 델타 파일 크기 */
    if (delta_path) {
        FILE *f = fopen(delta_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            s.delta_compressed = (uint64_t)ftell(f);
            fclose(f);
        }
    }

    s.total_compressed = s.delta_compressed + s.wh_compressed;
    s.compression_ratio = (s.total_compressed > 0)
        ? (double)s.original_bytes / (double)s.total_compressed : 0;
    s.saving_pct = (s.original_bytes > 0)
        ? (1.0 - (double)s.total_compressed / (double)s.original_bytes) * 100.0 : 0;

    /* 활성 슬롯 */
    for (uint32_t p = 0; p < V3_R_PAGES; p++)
        for (uint32_t col = 0; col < V3_COLS; col++) {
            if (c->brightness_r[p][col] > 0) s.active_r_slots++;
            if (c->brightness_r[p][col] > 255) s.a_overflow_count++;
        }
    for (uint32_t p = 0; p < V3_G_PAGES; p++)
        for (uint32_t col = 0; col < V3_COLS; col++) {
            if (c->brightness_g[p][col] > 0) s.active_g_slots++;
            if (c->brightness_g[p][col] > 255) s.a_overflow_count++;
        }

    return s;
}

void v3_print_stats(const V3Stats *s) {
    printf("\n");
    printf("  ┌─ SJ Spatial V3 (Streaming) ──────────────────\n");
    printf("  │ 원본 크기:        %14llu bytes (%.2f MB)\n",
           (unsigned long long)s->original_bytes,
           (double)s->original_bytes / 1048576.0);
    printf("  │ 캔버스 RAM:       %14llu bytes (%.2f MB)\n",
           (unsigned long long)s->canvas_memory,
           (double)s->canvas_memory / 1048576.0);
    printf("  ├─ 출력 ────────────────────────────────────\n");
    printf("  │ 델타맵:           %14llu bytes (%.2f MB)\n",
           (unsigned long long)s->delta_compressed,
           (double)s->delta_compressed / 1048576.0);
    printf("  │ WH 로그:          %14llu bytes (%.2f MB)\n",
           (unsigned long long)s->wh_compressed,
           (double)s->wh_compressed / 1048576.0);
    printf("  │ 총 압축:          %14llu bytes (%.2f MB)\n",
           (unsigned long long)s->total_compressed,
           (double)s->total_compressed / 1048576.0);
    printf("  │ 압축률:           %14.2fx\n", s->compression_ratio);
    printf("  │ 절감:             %13.1f%%\n", s->saving_pct);
    printf("  ├─ 캔버스 ──────────────────────────────────\n");
    printf("  │ 최대 밝기:        %14llu\n", (unsigned long long)s->max_brightness);
    printf("  │ 순환 횟수:        %14llu\n", (unsigned long long)s->page_cycles);
    printf("  │ 활성 R슬롯:       %14u / %d\n", s->active_r_slots, V3_R_PAGES * V3_COLS);
    printf("  │ 활성 G슬롯:       %14u / %d\n", s->active_g_slots, V3_G_PAGES * V3_COLS);
    printf("  │ A존 오버플로:     %14u\n", s->a_overflow_count);
    printf("  └──────────────────────────────────────────────\n");
}
