#ifndef TERVAS_RENDER_CELL_H
#define TERVAS_RENDER_CELL_H
/*
 * tervas_render_cell.h — Projection 결과 표준 포맷 (Spec §11)
 *
 * 모든 Renderer Backend는 이 포맷을 소비하기만 한다.
 * Projection 결과가 진실 원본이다.
 * Renderer는 판단 로직을 가지지 않는다.
 *
 * RULE:
 *   Projection → TvRenderCell[]  (이 단계에서 가시성/스타일 결정)
 *   Renderer   → TvRenderCell[] 수신 후 그리기만
 *
 * ASCII / NCurses / SDL2 / OpenGL 모두 이 버퍼를 공유한다.
 */
#include <stdint.h>
#include <stdbool.h>

/* ── 스타일 비트 플래그 (renderer가 시각화에 사용) ──────────────── */
#define TV_STYLE_NORMAL   0x00u
#define TV_STYLE_WH       0x01u   /* WhiteHole 강조 */
#define TV_STYLE_BH       0x02u   /* BlackHole 강조 */
#define TV_STYLE_A_MATCH  0x04u   /* A 집합 매칭 */
#define TV_STYLE_B_MATCH  0x08u   /* B 집합 매칭 */
#define TV_STYLE_OVERLAP  0x10u   /* A∩B 동시 매칭 */
#define TV_STYLE_GATE_ON  0x20u   /* 해당 tile gate open */
#define TV_STYLE_INACTIVE 0x40u   /* G=0, B=0 (dim 처리) */

/* ── 셀 단위 렌더 결과 ──────────────────────────────────────────── */
typedef struct {
    uint32_t x;         /* canvas 좌표                              */
    uint32_t y;
    uint8_t  visible;   /* 1 = projection 통과, 0 = 숨김            */
    uint8_t  style;     /* TV_STYLE_* 비트 조합                     */
    uint32_t a;         /* Cell.A 원본값                            */
    uint8_t  b;         /* Cell.B 원본값                            */
    uint8_t  g;         /* Cell.G 원본값 (에너지)                   */
    uint8_t  r;         /* Cell.R 원본값 (페이로드)                 */
    uint8_t  pad;
} TvRenderCell;

/* ── 프레임 버퍼 (Projection → Renderer 전달 단위) ──────────────── */
#define TV_FRAME_MAX_CELLS  (64u * 32u)   /* ASCII 뷰 최대 셀 수  */

typedef struct {
    TvRenderCell cells[TV_FRAME_MAX_CELLS];
    uint32_t     count;       /* 유효 셀 수                         */
    uint32_t     tick;        /* 스냅샷 tick                        */
    uint32_t     total_visible;  /* projection 통과 전체 셀 수      */
    uint32_t     wh_active;      /* WH 영역 G>0 셀 수               */
    uint32_t     bh_active;      /* BH 영역 G>0 셀 수               */
} TvFrame;

#endif /* TERVAS_RENDER_CELL_H */
