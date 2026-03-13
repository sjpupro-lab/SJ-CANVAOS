#ifndef GUI_ENGINE_BRIDGE_H
#define GUI_ENGINE_BRIDGE_H
/*
 * gui_engine_bridge.h — GUI ↔ Engine 연결
 *
 * 기능:
 *   1. Cell ABGR → GuiPixel 변환 (캔버스 시각화)
 *   2. Gate 상태 → 격자 오버레이
 *   3. WH/BH 타임라인 → 막대그래프 렌더링
 *   4. VM 상태 → 상태바 표시
 *   5. GUI 이벤트 → WH 기록
 *   6. 에너지 히트맵 렌더링
 */

#include "canvasos_gui.h"
#include "canvasos_engine_ctx.h"
#include "engine_time.h"
#include "canvasos_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── 뷰포트: 캔버스의 어떤 영역을 GUI에 표시할지 ── */
typedef struct {
    uint16_t view_x, view_y;     /* 캔버스 좌상단 */
    uint16_t view_w, view_h;     /* 보이는 셀 수 */
    uint8_t  cell_px;            /* 1 cell = cell_px × cell_px 픽셀 */
    uint8_t  show_gates;         /* 1=Gate 격자 표시 */
    uint8_t  show_energy;        /* 1=에너지 히트맵 모드 */
    uint8_t  show_grid;          /* 1=셀 경계선 표시 */
} CanvasViewport;

/* ── Cell → Pixel 변환 모드 ── */
typedef enum {
    CELL_VIS_ABGR = 0,   /* Cell.A→R, Cell.B→G, Cell.G→B, Cell.R→A */
    CELL_VIS_ENERGY,      /* G값 기반 히트맵 (0=파랑, 255=빨강) */
    CELL_VIS_OPCODE,      /* B값 기반 색상 맵 */
    CELL_VIS_LANE,        /* A[31:24] lane_id 색상 */
    CELL_VIS_ACTIVITY,    /* 최근 수정된 셀 하이라이트 */
} CellVisMode;

/* ── 타임라인 뷰 ── */
typedef struct {
    uint32_t bar_x, bar_y;
    uint32_t bar_w, bar_h;
    uint32_t visible_ticks;    /* 타임라인에 표시할 tick 수 */
    uint8_t  show_wh;          /* WH 이벤트 표시 */
    uint8_t  show_bh;          /* BH 압축 표시 */
} TimelineView;

/* ── 브릿지 컨텍스트 ── */
typedef struct {
    EngineContext  *engine;
    GuiContext     *gui;

    CanvasViewport  viewport;
    CellVisMode     vis_mode;
    TimelineView    timeline;

    /* 캐시 */
    uint32_t        last_rendered_tick;
    uint32_t        dirty_flags;
} GuiEngineBridge;

#define BRIDGE_DIRTY_CANVAS   (1u << 0)
#define BRIDGE_DIRTY_GATES    (1u << 1)
#define BRIDGE_DIRTY_TIMELINE (1u << 2)
#define BRIDGE_DIRTY_STATUS   (1u << 3)
#define BRIDGE_DIRTY_ALL      0x0Fu

/* ── API ── */

/* 초기화: engine + gui 연결 */
void bridge_init(GuiEngineBridge *br, EngineContext *engine, GuiContext *gui);

/* 뷰포트 설정 */
void bridge_set_viewport(GuiEngineBridge *br,
                         uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint8_t cell_px);

/* 셀 시각화 모드 변경 */
void bridge_set_vis_mode(GuiEngineBridge *br, CellVisMode mode);

/* 캔버스 → GUI 렌더링 (뷰포트 영역만) */
void bridge_render_canvas(GuiEngineBridge *br);

/* Gate 격자 오버레이 렌더링 */
void bridge_render_gates(GuiEngineBridge *br);

/* WH/BH 타임라인 렌더링 */
void bridge_render_timeline(GuiEngineBridge *br);

/* 상태바 업데이트 (tick, hash, gate_count, cell count) */
void bridge_render_status(GuiEngineBridge *br);

/* 에너지 히트맵 렌더링 */
void bridge_render_energy_heatmap(GuiEngineBridge *br);

/* 전체 프레임 렌더링 (dirty 체크 포함) */
void bridge_render_frame(GuiEngineBridge *br);

/* GUI 이벤트 → 엔진 전달 (셀 클릭 → WH 기록) */
int bridge_dispatch_event(GuiEngineBridge *br, const GuiEvent *ev);

/* 1 tick 진행 + GUI 업데이트 */
int bridge_tick(GuiEngineBridge *br);

/* Cell 좌표 ↔ GUI 픽셀 좌표 변환 */
void bridge_cell_to_pixel(const GuiEngineBridge *br,
                          uint16_t cx, uint16_t cy,
                          int32_t *px, int32_t *py);
int  bridge_pixel_to_cell(const GuiEngineBridge *br,
                          int32_t px, int32_t py,
                          uint16_t *cx, uint16_t *cy);

#ifdef __cplusplus
}
#endif

#endif
