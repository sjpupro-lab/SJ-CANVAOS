/**
 * SJ Spatial Compression Engine V3
 * ==================================
 * 512×512 고정 이미지. 스트리밍 I/O. GB급 데이터 지원.
 *
 * B값 페이지 구조:
 *   B = 0~63    (25%) → R 페이지 (128열 × 64페이지 = 8,192 슬롯)
 *   B = 64~127  (25%) → G 페이지 (128열 × 64페이지 = 8,192 슬롯)
 *   B = 128~255 (50%) → A 정수 존 (큰 누산값 분할 저장)
 *
 * RG 걷기:
 *   R걷기 128바이트 → col = byte & 0x7F → brightness_r[page][col]++
 *   G걷기 128바이트 → col = byte & 0x7F → brightness_g[page][col]++
 *   256바이트마다 page++ (B값 자동 증가)
 *   page가 64 도달 → page=0으로 돌아감 (순환, 밝기 누산 계속)
 *
 * 델타 압축:
 *   max_brightness = 캔버스 전체 최대밝기
 *   각 셀: delta = max_brightness - value
 *   → 대부분 delta가 작은 값 → varint로 극소 저장
 *
 * 스트리밍:
 *   인코딩: 256바이트 청크 단위로 입력 → 메모리 고정
 *   디코딩: WH 스트림에서 순차 읽기 → 출력
 *   → GB급 데이터도 캔버스 크기(2MB) + WH 버퍼만 사용
 */

#ifndef SJ_SPATIAL_V3_H
#define SJ_SPATIAL_V3_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 캔버스 상수 ─── */
#define V3_H              512
#define V3_W              512

/* ─── B값 페이지 분할 ─── */
#define V3_B_R_START      0       /* R 페이지: B [0, 63] */
#define V3_B_R_END        64
#define V3_B_G_START      64      /* G 페이지: B [64, 127] */
#define V3_B_G_END        128
#define V3_B_A_START      128     /* A 정수 존: B [128, 255] */
#define V3_B_A_END        256

#define V3_R_PAGES        64      /* R 페이지 수 (25%) */
#define V3_G_PAGES        64      /* G 페이지 수 (25%) */
#define V3_A_PAGES        128     /* A 존 페이지 수 (50%) */
#define V3_COLS           128     /* 페이지당 열 수 (byte & 0x7F) */

/* 1 페이지 = R 128바이트 + G 128바이트 = 256바이트 */
#define V3_BYTES_PER_PAGE 256
/* R+G 페이지 사이클 = 64페이지 × 256바이트 = 16,384바이트 */
#define V3_CYCLE_BYTES    (V3_R_PAGES * V3_BYTES_PER_PAGE)

/* ─── 스트리밍 청크 ─── */
#define V3_CHUNK_SIZE     V3_BYTES_PER_PAGE  /* 256바이트 = 1페이지 */

/* ─── 메인 캔버스 ─── */
typedef struct {
    /* 밝기 누산: brightness[page][col] */
    uint64_t brightness_r[V3_R_PAGES][V3_COLS];
    uint64_t brightness_g[V3_G_PAGES][V3_COLS];

    /* A존: 큰 누산값의 상위 비트 저장 */
    /* a_store[a_page][col] = overflow 정수 */
    uint64_t a_store[V3_A_PAGES][V3_COLS];

    /* 상태 */
    uint64_t total_bytes;        /* 인코딩된 총 바이트 */
    uint64_t max_brightness;     /* 전체 최대 밝기 */
    uint32_t current_page;       /* 현재 RG 페이지 (순환) */
    uint64_t page_cycles;        /* 전체 순환 횟수 */
} V3Canvas;

/* ─── 스트리밍 인코더 ─── */
typedef struct {
    V3Canvas canvas;
    FILE    *wh_stream;          /* WH 출력 스트림 (파일) */
    uint64_t wh_bytes_written;
} V3Encoder;

/* ─── 스트리밍 디코더 ─── */
typedef struct {
    FILE    *wh_stream;          /* WH 입력 스트림 */
    uint64_t total_bytes;
    uint64_t bytes_read;
} V3Decoder;

/* ─── 델타 압축 패키지 헤더 ─── */
typedef struct {
    char     magic[4];           /* "SJS3" */
    uint64_t original_bytes;
    uint64_t max_brightness;
    uint32_t active_entries;     /* 비제로 (page,col) 수 */
    uint64_t page_cycles;
    uint64_t wh_size;            /* WH 로그 파일 크기 */
} V3Header;

/* ─── 통계 ─── */
typedef struct {
    uint64_t original_bytes;
    uint64_t canvas_memory;      /* 캔버스 RAM 사용량 */
    uint64_t delta_compressed;   /* 델타맵 크기 */
    uint64_t wh_compressed;      /* WH 로그 크기 */
    uint64_t total_compressed;   /* delta + wh */
    double   compression_ratio;
    double   saving_pct;
    uint64_t max_brightness;
    uint32_t active_r_slots;
    uint32_t active_g_slots;
    uint32_t a_overflow_count;
    uint64_t page_cycles;
} V3Stats;

/* ═══ API: 인코더 (스트리밍) ═══ */

/** 인코더 초기화. wh_path = WH 로그 파일 경로 */
int  v3_enc_init(V3Encoder *enc, const char *wh_path);

/** 청크 입력 (256바이트 단위, 부분 청크 허용) */
int  v3_enc_feed(V3Encoder *enc, const uint8_t *data, size_t len);

/** 인코딩 완료. 캔버스 확정, WH 플러시 */
int  v3_enc_finish(V3Encoder *enc);

/** 델타 압축 출력: 캔버스 → 파일 */
size_t v3_enc_write_delta(const V3Encoder *enc, const char *delta_path);

/** 해제 */
void v3_enc_free(V3Encoder *enc);

/* ═══ API: 디코더 (스트리밍) ═══ */

/** 디코더 초기화 */
int  v3_dec_init(V3Decoder *dec, const char *wh_path, uint64_t total_bytes);

/** 청크 출력 (요청한 만큼 복원) */
int  v3_dec_read(V3Decoder *dec, uint8_t *out, size_t len, size_t *actual);

/** 해제 */
void v3_dec_free(V3Decoder *dec);

/* ═══ API: 통계 ═══ */

V3Stats v3_stats(const V3Encoder *enc, const char *wh_path, const char *delta_path);
void    v3_print_stats(const V3Stats *s);

#ifdef __cplusplus
}
#endif

#endif
