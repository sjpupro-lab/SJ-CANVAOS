#pragma once
/*
 * WH I/O Capture Contract (Phase 6+)
 *
 * 목적:
 *  - 외부 세계(키보드/파일/네트워크/GPU)의 비결정론 입력을 WH에 "캡처"한다.
 *  - 결정론 코어는 tick boundary에서만 주입(inject)한다.
 *
 * 표현:
 *  - 기존 2-cell WhRecord 포맷을 그대로 사용한다. (engine_time.h)
 *
 * WH_OP_IO_EVENT record encoding:
 *   C0:
 *     A = tick_or_event  (권장: 적용 tick, 보통 "다음 tick")
 *     B = opcode_index   (= WH_OP_IO_EVENT)
 *     G = flags          (reserved)
 *     R = param0         (dev: WhDevice)
 *   C1:
 *     A = target_addr    (ref: CanvasFS slot id 또는 WH blob id)
 *     B = target_kind    (= WH_TGT_PROC or NONE; reserved)
 *     G = arg_state      (op: device-opcode)
 *     R = param1         (len low8 or small arg)
 */
#include <stdint.h>
#include <stdbool.h>
#include "engine_time.h"

typedef enum {
    DEV_KBD = 1,
    DEV_FS  = 2,
    DEV_NET = 3,
    DEV_GPU = 4,
} WhDevice;

typedef enum {
    /* keyboard */
    KBD_KEYDOWN = 1,
    KBD_KEYUP   = 2,
    /* filesystem */
    FS_READ_REQ   = 10,
    FS_READ_DONE  = 11,
    FS_WRITE_REQ  = 12,
    FS_WRITE_DONE = 13,
    /* network */
    NET_RX = 20,
    NET_TX = 21,
} WhDeviceOp;

/* High-level helper to push an I/O event into WH (adapter -> WH). */
void wh_push_io_event(EngineContext* ctx,
                      uint64_t apply_tick,
                      uint16_t lane_id,
                      uint16_t page_id,
                      uint8_t dev,
                      uint8_t op,
                      uint16_t len,
                      uint32_t ref);

/* Predicate: is this record a WH_OP_IO_EVENT? */
static inline bool wh_is_io_event(const WhRecord* r) {
    return r && r->opcode_index == WH_OP_IO_EVENT;
}

/* ── 디코딩된 IO 이벤트 구조체 ── */
typedef struct {
    uint32_t apply_tick;
    uint16_t lane_id;
    uint16_t page_id;   /* C1.A[31:16] */
    uint16_t len;       /* C1.A[15:0]  */
    uint8_t  dev;       /* C0.R        */
    uint8_t  op;        /* C1.G        */
    uint8_t  ref_low8;  /* C1.R        */
} WH_IO_Event;

/* C1.A = (page_id << 16) | len ── 인코딩 헬퍼 */
static inline uint32_t wh_io_pack_addr(uint16_t page_id, uint16_t len) {
    return ((uint32_t)page_id << 16u) | (uint32_t)len;
}
static inline uint16_t wh_io_page_id(const WhRecord* r) {
    return (uint16_t)(r->target_addr >> 16u);
}
static inline uint16_t wh_io_len(const WhRecord* r) {
    return (uint16_t)(r->target_addr & 0xFFFFu);
}

/* 전체 디코드 */
WH_IO_Event wh_decode_io_event(const WhRecord* r);
