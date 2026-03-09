/**
 * SJ Spatial Compression Engine v1.0 — C Native
 * ================================================
 * 512×512 고정 이미지 1장 위에서 밝기 누산으로 초고압축.
 *
 * 구조:
 *   - 캔버스: 512×512 int64 배열 (밝기맵)
 *   - 2세그먼트: SEG0[0-255], SEG1[256-511]
 *   - WH: 순환 이벤트 로그 (모든 누산 기록)
 *   - BH: WH 패턴 압축 (IDLE/LOOP/BURST)
 *   - 양발커널: 결정론 코어 + 비결정론 어댑터
 *
 * CanvasOS 통합:
 *   - Cell.G 채널 = 밝기 (에너지)
 *   - DK 규칙 준수 (정수 전용, 틱 경계, 고정 순서)
 */

#ifndef SJ_SPATIAL_COMPRESS_H
#define SJ_SPATIAL_COMPRESS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ─── 캔버스 상수 ─── */
#define SJ_H           512
#define SJ_W           512
#define SJ_SEG0        0
#define SJ_SEG1        256
#define SJ_TOTAL_CELLS (SJ_H * SJ_W)   /* 262,144 */

/* ─── WH 상수 ─── */
#define WH_CAP         65536
#define WH_OP_TICK     0x01
#define WH_OP_ACCUM    0x20
#define WH_OP_DEACCUM  0x21
#define WH_OP_BH_SUM   0x45

/* ─── BH 상수 ─── */
#define BH_IDLE_MIN       16
#define BH_LOOP_MIN_REP   3
#define BH_BURST_THRESH   8
#define BH_MAX_SUMMARIES  4096

/* ─── DK 매크로 (결정론 커널 규칙) ─── */
#define DK_CLAMP_U8(v)  ((uint8_t)((v) > 255u ? 255u : (v)))
#define DK_CLAMP_I64(v) ((int64_t)((v) < 0 ? 0 : (v)))
#define DK_CELL_IDX(x, y) ((uint32_t)(y) * SJ_W + (uint32_t)(x))

/* ═══ 프론트 라벨 ═══ */
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
} SjFrontLabel;

/* ═══ WH 레코드 ═══ */
typedef struct {
    uint32_t tick;
    uint8_t  opcode;
    SjFrontLabel label;
    uint8_t  b0;    /* SEG0 바이트 */
    uint8_t  b1;    /* SEG1 바이트 */
} SjWhRecord;

/* ═══ BH 요약 ═══ */
typedef enum {
    BH_RULE_NONE  = 0,
    BH_RULE_IDLE  = 1,
    BH_RULE_LOOP  = 2,
    BH_RULE_BURST = 3,
} SjBhRule;

typedef struct {
    SjBhRule  rule;
    uint32_t  from_tick;
    uint32_t  to_tick;
    uint32_t  count;
    uint16_t  stride;
    uint8_t   pattern_b0;
    uint8_t   pattern_b1;
    uint8_t   original_hash[16];
} SjBhSummary;

/* ═══ WhiteHole ═══ */
typedef struct {
    SjWhRecord records[WH_CAP];
    uint32_t   count;         /* 총 기록 수 (min(written, CAP)) */
    uint32_t   write_cursor;  /* 다음 쓰기 위치 */
} SjWhiteHole;

/* ═══ BlackHole ═══ */
typedef struct {
    SjBhSummary summaries[BH_MAX_SUMMARIES];
    uint32_t    summary_count;
    uint64_t    ticks_saved;
} SjBlackHole;

/* ═══ 프론트 로그 엔트리 ═══ */
typedef struct {
    uint8_t b0;
    uint8_t b1;
} SjFrontEntry;

/* ═══ 메인 캔버스 구조체 ═══ */
typedef struct {
    /* 512×512 밝기 캔버스 */
    int64_t canvas[SJ_H * SJ_W];

    /* WH/BH 엔진 */
    SjWhiteHole wh;
    SjBlackHole bh;

    /* 틱 카운터 */
    uint32_t tick;

    /* 프론트 로그 (복원용) */
    SjFrontEntry *front_log;
    uint32_t      front_count;
    uint32_t      front_capacity;
} SjSpatialCanvas;

/* ═══ 압축 결과 통계 ═══ */
typedef struct {
    uint32_t original_bytes;
    uint32_t compressed_bytes;
    double   compression_ratio;
    uint32_t canvas_nonzero;
    double   canvas_sparsity;
    int64_t  canvas_max_brightness;
    uint32_t wh_records;
    uint32_t bh_summaries;
    uint64_t bh_ticks_saved;
} SjCompressStats;

/* ═══════════════════════════════════════════════
 *  API: 초기화 / 해제
 * ═══════════════════════════════════════════════ */

/** 캔버스 초기화 */
int  sj_init(SjSpatialCanvas *sj);

/** 캔버스 해제 */
void sj_free(SjSpatialCanvas *sj);

/** 캔버스 리셋 (제로) */
void sj_reset(SjSpatialCanvas *sj);

/* ═══════════════════════════════════════════════
 *  API: Foot 1 — 결정론 코어 (누산/역누산)
 * ═══════════════════════════════════════════════ */

/** 프론트 +1 누산 */
void sj_front_step(SjSpatialCanvas *sj, uint8_t b0, uint8_t b1,
                   const SjFrontLabel *label);

/** 프론트 -1 역누산 */
void sj_front_unstep(SjSpatialCanvas *sj, uint8_t b0, uint8_t b1,
                     const SjFrontLabel *label);

/** 캔버스 제로 확인 (가역성 검증) */
bool sj_is_zero(const SjSpatialCanvas *sj);

/* ═══════════════════════════════════════════════
 *  API: Foot 2 — 비결정론 어댑터 (I/O)
 * ═══════════════════════════════════════════════ */

/** 인코딩: 바이트 → 밝기 누산 */
int  sj_encode(SjSpatialCanvas *sj, const uint8_t *data, size_t len);

/** 디코딩: 프론트 로그 → 원본 복원 */
int  sj_decode(const SjSpatialCanvas *sj, uint8_t *out, size_t *out_len);

/** 디코딩 + 역누산 가역성 검증 */
int  sj_decode_verify(SjSpatialCanvas *sj, uint8_t *out, size_t *out_len,
                      bool *reversible);

/* ═══════════════════════════════════════════════
 *  API: WH (WhiteHole)
 * ═══════════════════════════════════════════════ */

void sj_wh_init(SjWhiteHole *wh);
void sj_wh_write(SjWhiteHole *wh, const SjWhRecord *rec);
int  sj_wh_read_range(const SjWhiteHole *wh, uint32_t from_tick,
                      uint32_t to_tick, SjWhRecord *out, uint32_t max_out);

/* ═══════════════════════════════════════════════
 *  API: BH (BlackHole)
 * ═══════════════════════════════════════════════ */

void sj_bh_init(SjBlackHole *bh);
int  sj_bh_compress(SjBlackHole *bh, const SjWhiteHole *wh,
                    uint32_t current_tick, uint32_t window_size);

/* ═══════════════════════════════════════════════
 *  API: 압축 패키지
 * ═══════════════════════════════════════════════ */

/**
 * 완전 압축: 캔버스 → 바이트 패키지
 * 형식: [SJSC magic][front_count][original_size][front_data][bh_log]
 * out 버퍼는 최소 sj_compressed_size_bound(sj) 이상
 */
size_t sj_compress_full(const SjSpatialCanvas *sj, uint8_t *out, size_t out_cap);

/** 압축 결과 크기 상한 */
size_t sj_compressed_size_bound(const SjSpatialCanvas *sj);

/**
 * 완전 복원: 패키지 → 원본 바이트
 * out 버퍼는 최소 original_size 이상
 */
int    sj_decompress_full(const uint8_t *compressed, size_t comp_len,
                          uint8_t *out, size_t *out_len);

/* ═══════════════════════════════════════════════
 *  API: 통계
 * ═══════════════════════════════════════════════ */

SjCompressStats sj_stats(const SjSpatialCanvas *sj);
void sj_print_stats(const SjCompressStats *s);

#ifdef __cplusplus
}
#endif

#endif /* SJ_SPATIAL_COMPRESS_H */
