#include "../include/inject.h"
#include "../include/wh_io.h"
#include <stdlib.h>
#include <string.h>

/* ── 결정론 정렬 키: (tick, lane_id, page_id, dev, op, ref) ── */
static int inj_cmp(const void* a, const void* b) {
    const InjectItem* x = (const InjectItem*)a;
    const InjectItem* y = (const InjectItem*)b;
#define CMP(f) if ((x->f) != (y->f)) return ((x->f) < (y->f)) ? -1 : 1
    CMP(tick);
    CMP(lane_id);
    CMP(page_id);
    CMP(rec.param0);      /* dev */
    CMP(rec.arg_state);   /* op  */
    CMP(rec.target_addr); /* ref/packed */
#undef CMP
    return 0;
}

/* ── 배치 버퍼 관리 ── */
void inject_batch_reserve(InjectBatch* b, uint32_t cap) {
    if (!b || b->cap >= cap) return;
    uint32_t nc = b->cap ? b->cap : 16;
    while (nc < cap) nc *= 2;
    b->items = (InjectItem*)realloc(b->items, nc * sizeof(InjectItem));
    b->cap   = nc;
}

void inject_batch_free(InjectBatch* b) {
    if (!b) return;
    free(b->items);
    b->items = NULL;
    b->count = b->cap = 0;
}

/*
 * inject_collect — tick에 해당하는 WH IO 이벤트를 모두 수집.
 *
 * WH는 circular buffer: 같은 tick 슬롯에 1개만 저장된다.
 * 하지만 실제 OS에서는 같은 틱에 IO 이벤트가 여러 개일 수 있다.
 *
 * 해결책 — "범위 스캔(window scan)":
 *   tick이 적용 틱(apply_tick)으로 기록되었으므로,
 *   WH의 [tick - INJECT_WINDOW, tick] 구간 전체를 스캔하여
 *   apply_tick == tick 인 WH_OP_IO_EVENT를 모두 수집한다.
 *
 * INJECT_WINDOW = 이벤트가 선발행될 수 있는 최대 틱 수.
 *   - 키보드/마우스 이벤트: 보통 1~2틱 선행
 *   - 파일 IO 완료 콜백: 수십 틱 선행 가능
 *   - 기본값 64: 충분히 큰 윈도우
 */
#define INJECT_WINDOW 64u

uint32_t inject_collect(EngineContext* ctx, uint64_t tick, InjectBatch* out) {
    if (!ctx || !out) return 0;
    out->count = 0;

    /* WH 가용 구간 */
    uint64_t wh_lo = (ctx->tick > (uint32_t)WH_CAP)
                     ? (uint64_t)ctx->tick - WH_CAP : 0;

    /* 스캔 시작점: tick보다 INJECT_WINDOW 앞 (단, wh_lo 이상) */
    uint64_t scan_start = (tick > INJECT_WINDOW) ? tick - INJECT_WINDOW : 0;
    if (scan_start < wh_lo) scan_start = wh_lo;

    for (uint64_t t = scan_start; t <= tick; t++) {
        WhRecord r;
        if (!wh_read_record(ctx, t, &r)) continue;
        if (!wh_is_io_event(&r)) continue;

        /* apply_tick 필터: C0.A == tick */
        if ((uint64_t)r.tick_or_event != tick) continue;

        inject_batch_reserve(out, out->count + 1);

        InjectItem it;
        memset(&it, 0, sizeof(it));
        it.tick    = tick;
        it.lane_id = (uint16_t)r.flags;          /* C0.G = lane_id low8 */
        it.page_id = wh_io_page_id(&r);           /* C1.A[31:16] */
        it.rec     = r;
        out->items[out->count++] = it;
    }
    return out->count;
}

/* ── inject_sort: 결정론 정렬 ── */
void inject_sort(InjectBatch* b) {
    if (!b || b->count < 2) return;
    qsort(b->items, b->count, sizeof(InjectItem), inj_cmp);
}

/* ── inject_apply: hook 순차 적용 ── */
void inject_apply(EngineContext* ctx, const InjectBatch* b,
                  InjectApplyFn fn, void* user) {
    if (!ctx || !b || !fn) return;
    for (uint32_t i = 0; i < b->count; i++)
        fn(ctx, &b->items[i], user);
}

/* ═══════════════════════════════════════════════
 * 표준 hook 구현
 * ═══════════════════════════════════════════════ */

/*
 * inject_hook_gate_open:
 *   DEV_KBD / KBD_KEYDOWN → ref 하위 16비트를 gate_id로 열기.
 *   실제 키보드 입력 → 해당 gate 활성화 → lane 실행 시작의 표준 경로.
 */
void inject_hook_gate_open(EngineContext* ctx,
                           const InjectItem* it, void* user) {
    (void)user;
    if (it->rec.param0 != DEV_KBD) return;
    if (it->rec.arg_state != KBD_KEYDOWN) return;
    uint16_t gate_id = (uint16_t)(it->rec.target_addr & 0xFFFFu);
    if (gate_id < TILE_COUNT) {
        if (ctx->gates)       ctx->gates[gate_id]      = GATE_OPEN;
        if (ctx->active_open) ctx->active_open[gate_id] = 1;
    }
}

/*
 * inject_hook_fs_done:
 *   DEV_FS / FS_READ_DONE or FS_WRITE_DONE → lane 에너지 충전.
 *   파일 IO 완료 후 해당 lane의 BH 에너지를 보충.
 */
void inject_hook_fs_done(EngineContext* ctx,
                         const InjectItem* it, void* user) {
    (void)user;
    if (it->rec.param0 != DEV_FS) return;
    uint8_t op = it->rec.arg_state;
    if (op != FS_READ_DONE && op != FS_WRITE_DONE) return;
    uint16_t pid = it->lane_id;   /* lane_id를 pid로 사용 */
    uint8_t cur  = bh_get_energy(ctx, pid);
    /* FS 완료 → 에너지 64 보충 (DK-4 clamp) */
    uint8_t add  = DK_CLAMP_U8((uint32_t)cur + 64u);
    bh_set_energy(ctx, pid, add, 255);
}

/*
 * inject_hook_all: 모든 표준 hook을 체인으로 실행.
 * 이것을 inject_apply의 fn으로 넘기면 모든 기본 동작 처리.
 */
void inject_hook_all(EngineContext* ctx,
                     const InjectItem* it, void* user) {
    inject_hook_gate_open(ctx, it, user);
    inject_hook_fs_done(ctx, it, user);
}

/*
 * inject_run_tick: collect → sort → apply 원샷.
 * tick boundary에서 호출하면 된다.
 */
uint32_t inject_run_tick(EngineContext* ctx, uint64_t tick,
                         InjectApplyFn fn, void* user) {
    InjectBatch b;
    memset(&b, 0, sizeof(b));
    uint32_t n = inject_collect(ctx, tick, &b);
    if (n > 0) {
        inject_sort(&b);
        inject_apply(ctx, &b, fn ? fn : inject_hook_all, user);
    }
    inject_batch_free(&b);
    return n;
}
