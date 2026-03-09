/**
 * SJ Spatial Compression Engine V3-Final
 * ========================================
 * 512×512 고정 이미지 = 출력물 그 자체. 로그 없음.
 *
 * ┌─────────────────────────────────────────────┐
 * │         512×512 Canvas = 압축 결과물         │
 * │                                              │
 * │  X: 0-255   = R존 (바이트값 직접 매핑)       │
 * │  X: 256-511 = G존 (바이트값 직접 매핑)       │
 * │  Y: 0-511   = 시간축 (페이지 내 오프셋)      │
 * │                                              │
 * │  brightness[y][x] = 해당 시간/바이트의 횟수   │
 * │  → 밝기값 상태 자체가 원본의 모든 정보        │
 * └─────────────────────────────────────────────┘
 *
 * B값 페이지:
 *   256바이트/페이지 (R 128 + G 128)
 *   4페이지 = 1 Y-사이클 (512행)
 *   B[0-63]   → R 페이지 (25%)
 *   B[64-127] → G 페이지 (25%)
 *   B[128-255]→ A 정수 (50%) — 밝기 상위비트
 *
 * 인코딩:
 *   data[p] → y = (page*128 + offset) % 512
 *           → x = byte_value (R존) 또는 256+byte_value (G존)
 *           → canvas[y][x] += 1
 *
 * 디코딩:
 *   y/x 스캔 → 열 위치 = 바이트값, 밝기 = 반복 횟수
 *   밝기가 가장 높은 열 = 해당 시간의 원본 바이트
 *
 * 출력:
 *   [SJS3 헤더 20B] + [활성셀 델타 압축]
 *   → 로그 없음. 이미지가 전부.
 */

#ifndef SJ_SPATIAL_V3F_H
#define SJ_SPATIAL_V3F_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define V3F_H             512
#define V3F_W             512
#define V3F_R_COLS        256     /* R존: x [0,255] = 바이트값 직접 */
#define V3F_G_COLS        256     /* G존: x [256,511] */
#define V3F_BYTES_PER_PAGE 256    /* R 128바이트 + G 128바이트 */
#define V3F_R_PER_PAGE    128
#define V3F_G_PER_PAGE    128
#define V3F_PAGES_PER_CYCLE 4     /* 4페이지 × 128 = 512행 = 1 Y사이클 */
#define V3F_CYCLE_BYTES   (V3F_PAGES_PER_CYCLE * V3F_BYTES_PER_PAGE) /* 1024 */

/* ─── 캔버스 ─── */
typedef struct {
    uint64_t cells[V3F_H][V3F_W];   /* brightness[y][x] */
    uint64_t total_bytes;
    uint64_t max_brightness;
    uint64_t cycles;                 /* Y-사이클 완료 횟수 */
} V3FCanvas;

/* ─── 헤더 ─── */
typedef struct {
    char     magic[4];         /* "SJ3F" */
    uint64_t original_bytes;
    uint64_t max_brightness;
    uint32_t active_cells;     /* 비제로 셀 수 */
    uint32_t pad;
} V3FHeader;

/* ─── 통계 ─── */
typedef struct {
    uint64_t original_bytes;
    uint64_t image_bytes;       /* 압축 이미지 크기 (= 유일한 출력) */
    double   compression_ratio;
    double   saving_pct;
    uint64_t max_brightness;
    uint32_t active_cells;
    uint32_t total_cells;
    double   sparsity;
    uint64_t cycles;
} V3FStats;

/* ═══ API ═══ */

void   v3f_init(V3FCanvas *c);

/** 스트리밍 인코딩: 청크 단위 입력 */
void   v3f_encode_chunk(V3FCanvas *c, const uint8_t *data, size_t len);

/** 이미지 출력: 캔버스 → 델타 압축 파일 (= 유일한 출력물) */
size_t v3f_write_image(const V3FCanvas *c, const char *path);

/** 이미지 로드 + 디코딩: 파일 → 원본 복원 */
int    v3f_read_and_decode(const char *path, uint8_t *out, size_t out_cap,
                           size_t *actual);

/** 통계 */
V3FStats v3f_stats(const V3FCanvas *c, const char *image_path);
void     v3f_print_stats(const V3FStats *s);

#ifdef __cplusplus
}
#endif

#endif
