/**
 * SJ Spatial V3-Final — 로그 없음. 이미지가 전부.
 *
 * 인코딩 (data[p] → canvas[y][x] += 1):
 *   page   = p / 256
 *   offset = p % 256
 *   if offset < 128:  R걸음
 *     y = (page * 128 + offset) % 512
 *     x = data[p]                        (0-255, R존)
 *   else:             G걸음
 *     y = (page * 128 + (offset-128)) % 512
 *     x = 256 + data[p]                  (256-511, G존)
 *   canvas[y][x] += 1
 *
 * 디코딩 (canvas → 원본):
 *   같은 (page, offset) → (y, zone) 계산
 *   해당 row에서 밝기 최대 열 = 바이트값
 *   밝기 -1 하며 사이클 역순 소진
 *
 * 출력: [헤더] + [활성셀: (y_16bit, x_16bit, delta_varint)]
 *   delta = max_brightness - brightness
 *   max인 셀은 delta=0 → varint 1바이트
 */

#include "sj_spatial_v3f.h"
#include <stdlib.h>
#include <string.h>

/* ─── varint ─── */
static size_t wv(uint8_t *o, uint64_t v) {
    size_t n = 0;
    while (v >= 0x80) { o[n++] = (uint8_t)(v | 0x80); v >>= 7; }
    o[n++] = (uint8_t)v;
    return n;
}
static size_t rv(const uint8_t *d, size_t len, uint64_t *v) {
    *v = 0; size_t n = 0; unsigned s = 0;
    while (n < len && n < 10) {
        uint8_t b = d[n]; *v |= (uint64_t)(b & 0x7F) << s; n++;
        if (!(b & 0x80)) break; s += 7;
    }
    return n;
}

/* ═══ 초기화 ═══ */

void v3f_init(V3FCanvas *c) {
    memset(c, 0, sizeof(*c));
}

/* ═══ 인코딩 ═══ */

void v3f_encode_chunk(V3FCanvas *c, const uint8_t *data, size_t len) {
    uint64_t p = c->total_bytes;

    for (size_t i = 0; i < len; i++, p++) {
        uint64_t page   = p / V3F_BYTES_PER_PAGE;
        uint32_t offset = (uint32_t)(p % V3F_BYTES_PER_PAGE);
        uint32_t y, x;

        if (offset < V3F_R_PER_PAGE) {
            /* R 걸음 */
            y = (uint32_t)((page * V3F_R_PER_PAGE + offset) % V3F_H);
            x = data[i];  /* 0-255 R존 */
        } else {
            /* G 걸음 */
            y = (uint32_t)((page * V3F_G_PER_PAGE + (offset - V3F_R_PER_PAGE)) % V3F_H);
            x = 256 + data[i];  /* 256-511 G존 */
        }

        c->cells[y][x]++;

        if (c->cells[y][x] > c->max_brightness)
            c->max_brightness = c->cells[y][x];
    }

    c->total_bytes = p;
    c->cycles = p / V3F_CYCLE_BYTES;
}

/* ═══ 이미지 출력 (캔버스 = 유일한 출력) ═══ */

size_t v3f_write_image(const V3FCanvas *c, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;

    uint64_t max_b = c->max_brightness;
    if (max_b == 0) max_b = 1;

    /* 활성 셀 카운트 */
    uint32_t active = 0;
    for (uint32_t y = 0; y < V3F_H; y++)
        for (uint32_t x = 0; x < V3F_W; x++)
            if (c->cells[y][x] > 0) active++;

    /* 헤더 */
    V3FHeader hdr;
    memcpy(hdr.magic, "SJ3F", 4);
    hdr.original_bytes = c->total_bytes;
    hdr.max_brightness = max_b;
    hdr.active_cells = active;
    hdr.pad = 0;
    fwrite(&hdr, sizeof(hdr), 1, f);

    size_t total = sizeof(hdr);

    /* 활성 셀만 출력: (y_16, x_16, delta_varint) */
    uint8_t buf[16];
    for (uint32_t y = 0; y < V3F_H; y++) {
        for (uint32_t x = 0; x < V3F_W; x++) {
            uint64_t val = c->cells[y][x];
            if (val == 0) continue;

            uint64_t delta = max_b - val;

            buf[0] = (uint8_t)(y >> 8);
            buf[1] = (uint8_t)(y & 0xFF);
            buf[2] = (uint8_t)(x >> 8);
            buf[3] = (uint8_t)(x & 0xFF);
            size_t vn = wv(buf + 4, delta);
            fwrite(buf, 1, 4 + vn, f);
            total += 4 + vn;
        }
    }

    fclose(f);
    return total;
}

/* ═══ 디코딩: 이미지 → 원본 ═══ */

int v3f_read_and_decode(const char *path, uint8_t *out, size_t out_cap,
                        size_t *actual) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    /* 헤더 읽기 */
    V3FHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return -1; }
    if (memcmp(hdr.magic, "SJ3F", 4) != 0) { fclose(f); return -1; }

    uint64_t orig = hdr.original_bytes;
    uint64_t max_b = hdr.max_brightness;
    uint32_t active = hdr.active_cells;

    if (out_cap < orig) {
        *actual = (size_t)orig;
        fclose(f);
        return -1;
    }

    /* 캔버스 복원 */
    uint64_t (*cells)[V3F_W] = calloc(V3F_H, sizeof(uint64_t[V3F_W]));
    if (!cells) { fclose(f); return -1; }

    uint8_t buf[16];
    for (uint32_t i = 0; i < active; i++) {
        if (fread(buf, 1, 4, f) != 4) break;
        uint32_t y = ((uint32_t)buf[0] << 8) | buf[1];
        uint32_t x = ((uint32_t)buf[2] << 8) | buf[3];

        /* varint 읽기 */
        uint64_t delta = 0;
        uint8_t vbuf[10];
        size_t vn = 0;
        for (;;) {
            if (fread(&vbuf[vn], 1, 1, f) != 1) break;
            if (!(vbuf[vn] & 0x80)) { vn++; break; }
            vn++;
            if (vn >= 10) break;
        }
        rv(vbuf, vn, &delta);

        if (y < V3F_H && x < V3F_W)
            cells[y][x] = max_b - delta;
    }
    fclose(f);

    /* 디코딩: 각 데이터 위치에서 최대 밝기 열 찾기 */
    memset(out, 0, (size_t)orig);
    size_t decoded = 0;

    for (uint64_t p = 0; p < orig; p++) {
        uint64_t page   = p / V3F_BYTES_PER_PAGE;
        uint32_t offset = (uint32_t)(p % V3F_BYTES_PER_PAGE);
        uint32_t y;
        uint32_t zone_start, zone_end;

        if (offset < V3F_R_PER_PAGE) {
            y = (uint32_t)((page * V3F_R_PER_PAGE + offset) % V3F_H);
            zone_start = 0;
            zone_end = 256;
        } else {
            y = (uint32_t)((page * V3F_G_PER_PAGE + (offset - V3F_R_PER_PAGE)) % V3F_H);
            zone_start = 256;
            zone_end = 512;
        }

        /* 최대 밝기 열 = 바이트값 */
        uint64_t best_val = 0;
        uint32_t best_x = zone_start;
        for (uint32_t x = zone_start; x < zone_end; x++) {
            if (cells[y][x] > best_val) {
                best_val = cells[y][x];
                best_x = x;
            }
        }

        out[p] = (uint8_t)(best_x - zone_start);

        /* 밝기 소진: -1 (이 사이클분 차감) */
        if (best_val > 0)
            cells[y][best_x]--;

        decoded++;
    }

    free(cells);
    *actual = decoded;
    return 0;
}

/* ═══ 통계 ═══ */

V3FStats v3f_stats(const V3FCanvas *c, const char *image_path) {
    V3FStats s;
    memset(&s, 0, sizeof(s));

    s.original_bytes = c->total_bytes;
    s.max_brightness = c->max_brightness;
    s.total_cells = V3F_H * V3F_W;
    s.cycles = c->cycles;

    uint32_t active = 0;
    for (uint32_t y = 0; y < V3F_H; y++)
        for (uint32_t x = 0; x < V3F_W; x++)
            if (c->cells[y][x] > 0) active++;
    s.active_cells = active;
    s.sparsity = 1.0 - (double)active / (double)s.total_cells;

    if (image_path) {
        FILE *f = fopen(image_path, "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            s.image_bytes = (uint64_t)ftell(f);
            fclose(f);
        }
    }

    s.compression_ratio = (s.image_bytes > 0)
        ? (double)s.original_bytes / (double)s.image_bytes : 0;
    s.saving_pct = (s.original_bytes > 0)
        ? (1.0 - (double)s.image_bytes / (double)s.original_bytes) * 100.0 : 0;

    return s;
}

void v3f_print_stats(const V3FStats *s) {
    printf("\n");
    printf("  ┌─ SJ Spatial V3-Final ─────────────────────────\n");
    printf("  │ 원본:           %14llu bytes (%7.2f MB)\n",
           (unsigned long long)s->original_bytes,
           (double)s->original_bytes / 1048576.0);
    printf("  │ 이미지:         %14llu bytes (%7.2f MB)\n",
           (unsigned long long)s->image_bytes,
           (double)s->image_bytes / 1048576.0);
    printf("  │ 압축률:         %14.2fx\n", s->compression_ratio);
    printf("  │ 절감:           %13.1f%%\n", s->saving_pct);
    printf("  ├─ 캔버스 (512×512) ─────────────────────────\n");
    printf("  │ 활성 셀:        %10u / %u\n", s->active_cells, s->total_cells);
    printf("  │ 희소율:         %13.1f%%\n", s->sparsity * 100.0);
    printf("  │ 최대 밝기:      %14llu\n", (unsigned long long)s->max_brightness);
    printf("  │ Y-사이클:       %14llu\n", (unsigned long long)s->cycles);
    printf("  │ 로그:                         없음 (이미지=전부)\n");
    printf("  └────────────────────────────────────────────────\n");
}
