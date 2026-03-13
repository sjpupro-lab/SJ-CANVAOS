/*
 * gui_engine_bridge.c — GUI ↔ Engine 연결 구현
 *
 * Cell ABGR → GuiPixel 시각화
 * Gate 격자 오버레이
 * WH/BH 타임라인
 * GUI 이벤트 → WH 기록
 */
#include "../include/gui_engine_bridge.h"
#include "../include/canvas_determinism.h"
#include <string.h>
#include <stdio.h>

/* ── Cell → GuiPixel 변환 헬퍼 ── */

static GuiPixel cell_to_pixel_abgr(const Cell *c) {
    /* A의 하위 8비트 → R, B → G, G → B(파랑), R → 알파 */
    return (GuiPixel){
        .r = (uint8_t)(c->A & 0xFF),
        .g = c->B,
        .b = c->G,
        .a = c->R ? c->R : 255
    };
}

static GuiPixel energy_heatmap(uint8_t energy) {
    /* 0=파랑(차가움), 128=초록, 255=빨강(뜨거움) */
    if (energy < 128) {
        return (GuiPixel){
            .r = 0,
            .g = (uint8_t)(energy * 2),
            .b = (uint8_t)(255 - energy * 2),
            .a = 255
        };
    }
    return (GuiPixel){
        .r = (uint8_t)((energy - 128) * 2),
        .g = (uint8_t)(255 - (energy - 128) * 2),
        .b = 0,
        .a = 255
    };
}

static GuiPixel opcode_color(uint8_t op) {
    /* 주요 opcode별 고유 색상 */
    switch (op) {
    case 0x00: return (GuiPixel){20,20,20,255};    /* NOP: 어두운 회색 */
    case 0x01: return (GuiPixel){0,255,128,255};   /* PRINT: 초록 */
    case 0x02: return (GuiPixel){255,0,0,255};     /* HALT: 빨강 */
    case 0x10: return (GuiPixel){0,200,255,255};   /* GATE_ON: 하늘색 */
    case 0x11: return (GuiPixel){255,100,0,255};   /* GATE_OFF: 주황 */
    case 0x30: return (GuiPixel){255,255,0,255};   /* ENERGY: 노랑 */
    default:   return (GuiPixel){128,0,255,255};   /* 기타: 보라 */
    }
}

/* lane_id → 고유 색상 (최대 16색 순환) */
static GuiPixel lane_color(uint8_t lane_id) {
    static const GuiPixel palette[16] = {
        {255,80,80,255},  {80,255,80,255},  {80,80,255,255},  {255,255,80,255},
        {255,80,255,255}, {80,255,255,255}, {255,160,80,255}, {160,80,255,255},
        {80,160,255,255}, {255,200,160,255},{160,255,200,255},{200,160,255,255},
        {200,200,80,255}, {80,200,200,255}, {200,80,200,255}, {180,180,180,255}
    };
    return palette[lane_id & 0x0F];
}

static GuiPixel cell_to_pixel(const Cell *c, CellVisMode mode) {
    switch (mode) {
    case CELL_VIS_ABGR:     return cell_to_pixel_abgr(c);
    case CELL_VIS_ENERGY:   return energy_heatmap(c->G);
    case CELL_VIS_OPCODE:   return opcode_color(c->B);
    case CELL_VIS_LANE:     return lane_color((uint8_t)(c->A >> 24));
    case CELL_VIS_ACTIVITY:
        /* 비활성 셀(B==0, G==0) → 어둡게, 활성 → 밝게 */
        if (c->B == 0 && c->G == 0)
            return (GuiPixel){10,10,10,255};
        return (GuiPixel){
            .r = (uint8_t)(100 + (c->G * 155 / 255)),
            .g = (uint8_t)(50 + c->B),
            .b = (uint8_t)(c->R / 2),
            .a = 255
        };
    default:
        return cell_to_pixel_abgr(c);
    }
}

/* ══════════════════════════════════════════════ */

void bridge_init(GuiEngineBridge *br, EngineContext *engine, GuiContext *gui) {
    memset(br, 0, sizeof(*br));
    br->engine = engine;
    br->gui    = gui;

    /* 기본 뷰포트: 원점 중심 64×64, 셀당 4px */
    br->viewport = (CanvasViewport){
        .view_x  = ORIGIN_X - 32,
        .view_y  = ORIGIN_Y - 32,
        .view_w  = 64,
        .view_h  = 64,
        .cell_px = 4,
        .show_gates  = 1,
        .show_energy = 0,
        .show_grid   = 1,
    };
    br->vis_mode = CELL_VIS_ABGR;

    /* 타임라인: 화면 하단 */
    br->timeline = (TimelineView){
        .bar_x = 0,
        .bar_y = gui->framebuffer.h - 32,
        .bar_w = gui->framebuffer.w,
        .bar_h = 24,
        .visible_ticks = 128,
        .show_wh = 1,
        .show_bh = 1,
    };

    br->dirty_flags = BRIDGE_DIRTY_ALL;
}

void bridge_set_viewport(GuiEngineBridge *br,
                         uint16_t x, uint16_t y,
                         uint16_t w, uint16_t h,
                         uint8_t cell_px) {
    br->viewport.view_x = x;
    br->viewport.view_y = y;
    br->viewport.view_w = w;
    br->viewport.view_h = h;
    br->viewport.cell_px = cell_px ? cell_px : 4;
    br->dirty_flags |= BRIDGE_DIRTY_CANVAS | BRIDGE_DIRTY_GATES;
}

void bridge_set_vis_mode(GuiEngineBridge *br, CellVisMode mode) {
    br->vis_mode = mode;
    br->dirty_flags |= BRIDGE_DIRTY_CANVAS;
}

/* ── 캔버스 렌더링 ── */
void bridge_render_canvas(GuiEngineBridge *br) {
    if (!br || !br->engine || !br->gui) return;
    const CanvasViewport *vp = &br->viewport;
    GuiBuffer *fb = &br->gui->framebuffer;
    const Cell *cells = br->engine->cells;
    uint32_t cpx = vp->cell_px;

    for (uint16_t vy = 0; vy < vp->view_h; vy++) {
        uint16_t cy = (uint16_t)(vp->view_y + vy);
        if (cy >= CANVAS_H) continue;
        for (uint16_t vx = 0; vx < vp->view_w; vx++) {
            uint16_t cx = (uint16_t)(vp->view_x + vx);
            if (cx >= CANVAS_W) continue;

            uint32_t idx = (uint32_t)cy * CANVAS_W + cx;
            GuiPixel px = cell_to_pixel(&cells[idx], br->vis_mode);

            /* 셀을 cpx × cpx 블록으로 그리기 */
            int32_t bx = (int32_t)(vx * cpx);
            int32_t by = (int32_t)(vy * cpx);
            gui_buffer_fill_rect(fb, bx, by, cpx, cpx, px);
        }
    }

    /* 셀 격자선 */
    if (vp->show_grid && cpx >= 3) {
        GuiPixel grid_c = {40, 40, 40, 128};
        for (uint16_t vy = 0; vy <= vp->view_h; vy++) {
            int32_t py = (int32_t)(vy * cpx);
            for (uint32_t x = 0; x < vp->view_w * cpx && x < fb->w; x++)
                gui_buffer_set_pixel(fb, x, (uint32_t)py, grid_c);
        }
        for (uint16_t vx = 0; vx <= vp->view_w; vx++) {
            int32_t px = (int32_t)(vx * cpx);
            for (uint32_t y = 0; y < vp->view_h * cpx && y < fb->h; y++)
                gui_buffer_set_pixel(fb, (uint32_t)px, y, grid_c);
        }
    }
}

/* ── Gate 오버레이 ── */
void bridge_render_gates(GuiEngineBridge *br) {
    if (!br || !br->engine || !br->gui) return;
    if (!br->viewport.show_gates) return;
    const CanvasViewport *vp = &br->viewport;
    GuiBuffer *fb = &br->gui->framebuffer;
    uint32_t cpx = vp->cell_px;

    /* 뷰포트 내 타일 경계에 Gate 상태 표시 */
    uint16_t tx0 = (uint16_t)(vp->view_x / TILE);
    uint16_t ty0 = (uint16_t)(vp->view_y / TILE);
    uint16_t tx1 = (uint16_t)((vp->view_x + vp->view_w - 1) / TILE);
    uint16_t ty1 = (uint16_t)((vp->view_y + vp->view_h - 1) / TILE);

    for (uint16_t ty = ty0; ty <= ty1 && ty < TILES_Y; ty++) {
        for (uint16_t tx = tx0; tx <= tx1 && tx < TILES_X; tx++) {
            uint16_t gid = (uint16_t)(ty * TILES_X + tx);
            GateState gs = br->engine->gates[gid];

            /* 타일 영역 → 뷰포트 픽셀 좌표 */
            int32_t tile_x0 = (int32_t)((tx * TILE - vp->view_x) * cpx);
            int32_t tile_y0 = (int32_t)((ty * TILE - vp->view_y) * cpx);
            uint32_t tile_w = TILE * cpx;
            uint32_t tile_h = TILE * cpx;

            GuiPixel border_c;
            if (gs == GATE_OPEN) {
                border_c = (GuiPixel){0, 255, 0, 100};  /* 초록 = 열림 */
            } else {
                border_c = (GuiPixel){255, 0, 0, 80};   /* 빨강 = 닫힘 */
                /* 닫힌 게이트: 반투명 빨간 오버레이 */
                GuiPixel overlay = {255, 0, 0, 40};
                gui_buffer_fill_rect(fb, tile_x0, tile_y0,
                                     tile_w, tile_h, overlay);
            }
            gui_buffer_rect_outline(fb, tile_x0, tile_y0,
                                    tile_w, tile_h, border_c, 1);
        }
    }
}

/* ── 타임라인 렌더링 ── */
void bridge_render_timeline(GuiEngineBridge *br) {
    if (!br || !br->engine || !br->gui) return;
    const TimelineView *tv = &br->timeline;
    GuiBuffer *fb = &br->gui->framebuffer;
    uint32_t tick = br->engine->tick;

    /* 배경 */
    GuiPixel bg = {20, 20, 30, 220};
    gui_buffer_fill_rect(fb, (int32_t)tv->bar_x, (int32_t)tv->bar_y,
                         tv->bar_w, tv->bar_h, bg);

    /* 타임라인 바: 각 tick당 1px 너비 막대 */
    uint32_t start_tick = tick > tv->visible_ticks ? tick - tv->visible_ticks : 0;
    uint32_t bar_max_h = tv->bar_h - 4;

    for (uint32_t t = start_tick; t <= tick && t < start_tick + tv->visible_ticks; t++) {
        uint32_t px_x = tv->bar_x + (t - start_tick) * tv->bar_w / tv->visible_ticks;
        if (px_x >= tv->bar_x + tv->bar_w) break;

        /* WH 레코드 읽기 */
        if (tv->show_wh) {
            WhRecord wr;
            if (wh_read_record(br->engine, (uint64_t)t, &wr) && wr.opcode_index != 0) {
                /* opcode별 색상 */
                GuiPixel wh_c = {0, 180, 255, 200}; /* 기본 하늘색 */
                if (wr.opcode_index == WH_OP_TICK)
                    wh_c = (GuiPixel){60, 60, 80, 150};
                else if (wr.opcode_index >= 0x70)
                    wh_c = (GuiPixel){255, 200, 0, 200}; /* syscall: 노랑 */
                else if (wr.opcode_index >= 0x10 && wr.opcode_index <= 0x11)
                    wh_c = (GuiPixel){0, 255, 100, 200}; /* gate: 초록 */
                else if (wr.opcode_index == WH_OP_BH_SUMMARY)
                    wh_c = (GuiPixel){255, 0, 255, 200}; /* BH: 보라 */

                uint32_t h = bar_max_h / 2 + 2;
                gui_buffer_fill_rect(fb, (int32_t)px_x,
                                     (int32_t)(tv->bar_y + tv->bar_h - h - 2),
                                     1, h, wh_c);
            }
        }
    }

    /* 현재 tick 위치 표시 (밝은 세로선) */
    uint32_t now_x = tv->bar_x + tv->bar_w - 1;
    GuiPixel now_c = {255, 255, 255, 255};
    gui_buffer_fill_rect(fb, (int32_t)now_x, (int32_t)tv->bar_y,
                         2, tv->bar_h, now_c);

    /* tick 숫자 표시 */
    char tick_str[32];
    snprintf(tick_str, sizeof(tick_str), "T:%u", tick);
    GuiPixel txt_c = {200, 200, 200, 255};
    gui_font_draw_string(fb, tv->bar_x + 4, tv->bar_y + 2,
                         tick_str, txt_c, 1);
}

/* ── 상태바 렌더링 ── */
void bridge_render_status(GuiEngineBridge *br) {
    if (!br || !br->engine || !br->gui) return;
    GuiBuffer *fb = &br->gui->framebuffer;
    const EngineContext *eng = br->engine;

    /* 상태바 영역: 화면 상단 20px */
    GuiPixel bar_bg = {10, 10, 40, 230};
    gui_buffer_fill_rect(fb, 0, 0, fb->w, 20, bar_bg);

    /* 정보 텍스트 */
    char status[128];
    uint32_t hash = 0;
    if (eng->cells)
        hash = dk_canvas_hash(eng->cells, eng->cells_count);

    /* Gate 열림 수 세기 */
    uint32_t open_gates = 0;
    if (eng->gates) {
        for (int i = 0; i < TILEGATE_COUNT; i++)
            if (eng->gates[i] == GATE_OPEN) open_gates++;
    }

    snprintf(status, sizeof(status),
             "Tick:%u  Hash:%08X  Gates:%u/%u  Mode:%s",
             eng->tick, hash, open_gates, TILEGATE_COUNT,
             br->vis_mode == CELL_VIS_ENERGY   ? "Energy" :
             br->vis_mode == CELL_VIS_OPCODE   ? "Opcode" :
             br->vis_mode == CELL_VIS_LANE     ? "Lane" :
             br->vis_mode == CELL_VIS_ACTIVITY ? "Active" : "ABGR");

    GuiPixel txt = {220, 220, 255, 255};
    gui_font_draw_string(fb, 4, 4, status, txt, 1);
}

/* ── 에너지 히트맵 (전체 뷰포트) ── */
void bridge_render_energy_heatmap(GuiEngineBridge *br) {
    CellVisMode saved = br->vis_mode;
    br->vis_mode = CELL_VIS_ENERGY;
    bridge_render_canvas(br);
    br->vis_mode = saved;
}

/* ── 전체 프레임 렌더링 ── */
void bridge_render_frame(GuiEngineBridge *br) {
    if (!br || !br->engine || !br->gui) return;

    /* dirty 체크: tick이 바뀌었으면 전체 dirty */
    if (br->engine->tick != br->last_rendered_tick)
        br->dirty_flags = BRIDGE_DIRTY_ALL;

    if (br->dirty_flags == 0) return;

    /* 배경 클리어 */
    GuiPixel bg = {5, 5, 15, 255};
    gui_buffer_clear(&br->gui->framebuffer, bg);

    /* 레이어 순서대로 렌더링 */
    if (br->dirty_flags & BRIDGE_DIRTY_CANVAS)
        bridge_render_canvas(br);
    if (br->dirty_flags & BRIDGE_DIRTY_GATES)
        bridge_render_gates(br);
    if (br->dirty_flags & BRIDGE_DIRTY_STATUS)
        bridge_render_status(br);
    if (br->dirty_flags & BRIDGE_DIRTY_TIMELINE)
        bridge_render_timeline(br);

    br->last_rendered_tick = br->engine->tick;
    br->dirty_flags = 0;
    br->gui->frame_count++;
    br->gui->dirty = 0;
}

/* ── GUI 이벤트 → 엔진 ── */
int bridge_dispatch_event(GuiEngineBridge *br, const GuiEvent *ev) {
    if (!br || !ev || !br->engine) return -1;

    /* 터치/클릭 → 셀 좌표 변환 */
    if (ev->type == GUI_EVT_TOUCH_DOWN || ev->type == GUI_EVT_TOUCH_UP) {
        uint16_t cx, cy;
        if (bridge_pixel_to_cell(br, ev->x, ev->y, &cx, &cy) == 0) {
            /* WH에 IO_EVENT 기록 */
            WhRecord wr;
            memset(&wr, 0, sizeof(wr));
            wr.tick_or_event = br->engine->tick;
            wr.opcode_index  = WH_OP_IO_EVENT;
            wr.target_addr   = ((uint32_t)cx << 16) | cy;
            wr.target_kind   = WH_TGT_CELL;
            wr.param0        = (uint8_t)(ev->type == GUI_EVT_TOUCH_DOWN ? 1 : 2);
            wr.flags         = (uint8_t)(ev->button & 0xFF);
            wh_write_record(br->engine, br->engine->tick, &wr);

            br->dirty_flags |= BRIDGE_DIRTY_CANVAS;
            return 0;
        }
    }

    /* GUI 내부 이벤트 디스패치도 수행 */
    return gui_event_dispatch(br->gui, ev);
}

/* ── 1 tick + 렌더 ── */
int bridge_tick(GuiEngineBridge *br) {
    if (!br || !br->engine) return -1;
    engctx_tick(br->engine);
    br->dirty_flags = BRIDGE_DIRTY_ALL;
    bridge_render_frame(br);
    return 0;
}

/* ── 좌표 변환 ── */
void bridge_cell_to_pixel(const GuiEngineBridge *br,
                          uint16_t cx, uint16_t cy,
                          int32_t *px, int32_t *py) {
    const CanvasViewport *vp = &br->viewport;
    *px = (int32_t)((cx - vp->view_x) * vp->cell_px);
    *py = (int32_t)((cy - vp->view_y) * vp->cell_px);
}

int bridge_pixel_to_cell(const GuiEngineBridge *br,
                         int32_t px, int32_t py,
                         uint16_t *cx, uint16_t *cy) {
    const CanvasViewport *vp = &br->viewport;
    if (px < 0 || py < 0) return -1;
    if (vp->cell_px == 0) return -1;

    int32_t rel_x = px / (int32_t)vp->cell_px;
    int32_t rel_y = py / (int32_t)vp->cell_px;

    int32_t cell_x = vp->view_x + rel_x;
    int32_t cell_y = vp->view_y + rel_y;

    if (cell_x < 0 || cell_x >= CANVAS_W) return -1;
    if (cell_y < 0 || cell_y >= CANVAS_H) return -1;

    *cx = (uint16_t)cell_x;
    *cy = (uint16_t)cell_y;
    return 0;
}
