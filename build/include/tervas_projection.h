#ifndef TERVAS_PROJECTION_H
#define TERVAS_PROJECTION_H
#include "tervas_core.h"
#include "tervas_render_cell.h"

/* 단일 셀 가시성 판정 (R-6: 정수 연산만) */
bool tv_cell_visible(uint32_t x, uint32_t y, const Cell *c,
                     const TvFilter *f, const uint8_t *gates);

bool tv_match_a(uint32_t a, const TvFilter *f);
bool tv_match_b(uint8_t  b, const TvFilter *f);
bool tv_is_wh_cell(uint32_t x, uint32_t y);
bool tv_is_bh_cell(uint32_t x, uint32_t y);

/*
 * tv_build_frame — Projection → TvFrame 생성 (Spec §11)
 *
 * snapshot을 주어진 filter로 투영하여 뷰 해상도(cols×rows)의
 * TvFrame을 생성한다. 모든 renderer는 이 함수 결과만 소비한다.
 *
 * [R-6] 정수 연산만. zoom/pan 모두 정수 좌표 기반.
 */
int tv_build_frame(TvFrame *frame, const TvSnapshot *snap,
                   const TvFilter *f,
                   uint32_t view_cols, uint32_t view_rows);

#endif
