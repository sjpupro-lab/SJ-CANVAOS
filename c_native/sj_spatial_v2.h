/**
 * SJ Spatial Compression Engine V2
 * ==================================
 * 512×512 고정 이미지 1장 = OS 실행 단위.
 *
 * ┌──────────────────────────────────────────────────┐
 * │  512×512 Canvas (고정 크기)                       │
 * │                                                   │
 * │  x: 0-127     │  R-Pages (B값이 페이지 선택)      │
 * │  (전면 25%)   │  128열 × 256페이지 = 32K 슬롯     │
 * │               │                                   │
 * │  x: 128-255   │  G-Pages (B값이 페이지 선택)      │
 * │  (전면 25%)   │  128열 × 256페이지 = 32K 슬롯     │
 * │               │                                   │
 * │  x: 256-511   │  A-Integer Zone (큰값 분할 저장)  │
 * │  (후면 50%)   │  256열 × 512행 = 131K 셀          │
 * │               │                                   │
 * │  Y축 = 시간축 (0-511 = 틱/시퀀스)                │
 * └──────────────────────────────────────────────────┘
 *
 * 핵심 인사이트:
 *   집 그린 512×512 이미지 = 도심숲 512×512 이미지 = 같은 크기
 *   → 4~8MB 이상 데이터를 고정 크기 캔버스로 재배열
 *   → 파일 클수록 압축률 극대화
 *   → 이것은 "OS 데이터 구조화 엔진"
 *
 * ABGR 채널 역할:
 *   A (32bit) : 누산 정수값 (밝기 카운트를 정수 리스트화)
 *   B (8bit)  : 페이지 인덱스 (RG 공간을 256페이지로 분할)
 *   G (8bit)  : 에너지/상태 (걷기용)
 *   R (8bit)  : 데이터 스트림 (걷기용)
 */

#ifndef SJ_SPATIAL_V2_H
#define SJ_SPATIAL_V2_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 캔버스 상수 ─── */
#define V2_H              512
#define V2_W              512
#define V2_TOTAL_CELLS    (V2_H * V2_W)  /* 262,144 */

/* ─── 존 경계 ─── */
#define V2_R_ZONE_X0      0       /* R-Pages: x [0, 127] */
#define V2_R_ZONE_X1      128
#define V2_G_ZONE_X0      128     /* G-Pages: x [128, 255] */
#define V2_G_ZONE_X1      256
#define V2_A_ZONE_X0      256     /* A-Integer: x [256, 511] */
#define V2_A_ZONE_X1      512

#define V2_R_COLS         128     /* R존 열 수 */
#define V2_G_COLS         128     /* G존 열 수 */
#define V2_A_COLS         256     /* A존 열 수 */
#define V2_PAGES          256     /* B값에 의한 총 페이지 수 */

/* ─── 셀 구조 (ABGR, 8바이트) ─── */
typedef struct {
    uint32_t A;     /* 누산 정수값 / 주소 */
    uint8_t  B;     /* 페이지 인덱스 */
    uint8_t  G;     /* 에너지/상태 */
    uint8_t  R;     /* 데이터 바이트 */
    uint8_t  pad;   /* 예약 */
} V2Cell;

/* ─── WH 레코드 (최소화) ─── */
typedef struct {
    uint32_t tick;
    uint8_t  zone;     /* 0=R, 1=G */
    uint8_t  page;     /* B 페이지 */
    uint8_t  col;      /* 존 내 열 (0-127) */
    uint8_t  value;    /* 원본 바이트값 */
} V2WhRecord;

#define V2_WH_CAP  131072   /* 128K 레코드 */

typedef struct {
    V2WhRecord *records;
    uint32_t    count;
    uint32_t    write_cursor;
    uint32_t    capacity;
} V2WhiteHole;

/* ─── BH 델타 압축 ─── */
typedef struct {
    uint32_t max_brightness;     /* 최대 밝기 (델타 기준) */
    uint32_t unique_positions;   /* 활성 위치 수 */
    uint32_t pages_used;         /* 사용된 페이지 수 */
    uint64_t total_accumulated;  /* 총 누산량 */
} V2BhDeltaInfo;

/* ─── 메인 V2 캔버스 ─── */
typedef struct {
    /* 512×512 캔버스 (셀 배열) */
    V2Cell cells[V2_TOTAL_CELLS];

    /* 밝기 누산 맵 (A채널 정수 뷰) */
    /* R존: brightness_r[page][col] = 누산 카운트 */
    uint32_t brightness_r[V2_PAGES][V2_R_COLS];
    /* G존: brightness_g[page][col] = 누산 카운트 */
    uint32_t brightness_g[V2_PAGES][V2_G_COLS];

    /* A존: 큰 누산값 분할 저장 */
    /* a_overflow[row][col] — row는 페이지, col은 분할 인덱스 */
    uint32_t a_overflow[V2_H][V2_A_COLS];

    /* WH */
    V2WhiteHole wh;

    /* 상태 */
    uint32_t tick;
    uint32_t current_page;   /* 현재 B 페이지 */
    uint32_t bytes_encoded;  /* 인코딩된 총 바이트 */

    /* 델타 압축 정보 */
    V2BhDeltaInfo delta_info;
} V2Canvas;

/* ─── 통계 ─── */
typedef struct {
    size_t   original_bytes;
    size_t   canvas_raw_bytes;    /* 항상 2MB (512×512×8) */
    size_t   compressed_bytes;    /* 델타 압축 후 */
    double   compression_ratio;
    double   space_saving_pct;
    uint32_t max_brightness;
    uint32_t pages_used;
    uint32_t active_r_slots;
    uint32_t active_g_slots;
    uint32_t a_overflow_count;
    uint32_t wh_records;
} V2Stats;

/* ═══ API ═══ */

int  v2_init(V2Canvas *v);
void v2_free(V2Canvas *v);

/**
 * 인코딩: 임의 데이터 → 512×512 캔버스 재배열.
 * RG 걷기 + B 페이지 + A 정수 누산.
 * 4MB 이상 데이터에서 최적.
 */
int  v2_encode(V2Canvas *v, const uint8_t *data, size_t len);

/**
 * 디코딩: 캔버스 + WH 로그 → 원본 데이터.
 */
int  v2_decode(const V2Canvas *v, uint8_t *out, size_t *out_len);

/**
 * 델타 압축: 최대밝기 기준으로 전체 캔버스를 델타 인코딩.
 * 출력: 고정 크기 캔버스의 압축 이미지.
 */
size_t v2_delta_compress(const V2Canvas *v, uint8_t *out, size_t out_cap);

/**
 * 델타 복원 → 캔버스 복구.
 */
int  v2_delta_decompress(V2Canvas *v, const uint8_t *compressed, size_t comp_len);

/**
 * 완전 압축 패키지: 캔버스 + WH 최소 로그 → 바이트 스트림.
 */
size_t v2_compress_full(const V2Canvas *v, uint8_t *out, size_t out_cap);
int    v2_decompress_full(const uint8_t *compressed, size_t comp_len,
                          uint8_t *out, size_t *out_len);

V2Stats v2_stats(const V2Canvas *v);
void    v2_print_stats(const V2Stats *s);
void    v2_print_canvas_map(const V2Canvas *v);

#ifdef __cplusplus
}
#endif

#endif /* SJ_SPATIAL_V2_H */
