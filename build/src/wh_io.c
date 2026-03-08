#include "../include/wh_io.h"
#include <string.h>

/*
 * WH_OP_IO_EVENT 2-cell 완전 인코딩
 *
 * C0 (tick/dispatch 셀):
 *   A[31:0]  = apply_tick         (적용 틱)
 *   B        = WH_OP_IO_EVENT     (0x40)
 *   G        = lane_id low8       (실행 lane)
 *   R        = dev                (WhDevice)
 *
 * C1 (payload 셀):
 *   A[31:16] = page_id            (bpage/page 식별자)
 *   A[15:0]  = len high8 << 8 | len low8  (전체 len 16비트)
 *   B        = target_kind        (WH_TGT_PROC or WH_TGT_NONE)
 *   G        = op                 (WhDeviceOp)
 *   R        = ref low8           (슬롯/blob 하위 바이트)
 *
 * 왜 A에 page_id를 넣었나:
 *   - target_addr(A)는 32비트 자유 필드
 *   - page_id(16비트) + len(16비트)을 상하위 16으로 쪼개면
 *     추가 필드 없이 기존 2-cell 포맷 안에 완전히 수납
 *   - Phase 7+에서 ref가 32비트 전부 필요하면 C1.A를 ref로 교체하고
 *     page_id/len을 C0.flags(16비트 확장)로 이동
 */
void wh_push_io_event(EngineContext* ctx,
                      uint64_t apply_tick,
                      uint16_t lane_id,
                      uint16_t page_id,
                      uint8_t dev,
                      uint8_t op,
                      uint16_t len,
                      uint32_t ref)
{
    WhRecord r;
    memset(&r, 0, sizeof(r));

    /* C0 */
    r.tick_or_event = (uint32_t)apply_tick;
    r.opcode_index  = (uint8_t)WH_OP_IO_EVENT;
    r.flags         = (uint8_t)(lane_id & 0xFFu);   /* lane_id low8 */
    r.param0        = dev;

    /* C1: A[31:16]=page_id, A[15:0]=len */
    r.target_addr   = ((uint32_t)page_id << 16u) | (uint32_t)(len & 0xFFFFu);
    r.target_kind   = (uint8_t)WH_TGT_NONE;
    r.arg_state     = op;
    r.param1        = (uint8_t)(ref & 0xFFu);

    wh_write_record(ctx, apply_tick, &r);
}

/* ── 디코더 (읽기 헬퍼) ── */
WH_IO_Event wh_decode_io_event(const WhRecord* r) {
    WH_IO_Event e;
    memset(&e, 0, sizeof(e));
    if (!r || r->opcode_index != WH_OP_IO_EVENT) return e;

    e.apply_tick = r->tick_or_event;
    e.lane_id    = (uint16_t)r->flags;
    e.dev        = r->param0;
    e.page_id    = (uint16_t)(r->target_addr >> 16u);
    e.len        = (uint16_t)(r->target_addr & 0xFFFFu);
    e.op         = r->arg_state;
    e.ref_low8   = r->param1;
    return e;
}
