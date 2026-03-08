#pragma once
/*
 * Tick Boundary Injection (Phase 6+)
 *
 * - WH_OP_IO_EVENT를 특정 tick에서 수집
 * - 결정론 정렬 규칙으로 정렬
 * - lane/page에 주입 (현재는 hook만 제공; 실제 적용은 사용자 구현)
 */
#include <stdint.h>
#include <stdbool.h>
#include "engine_time.h"
#include "wh_io.h"
#include "canvasos_gate_ops.h"
#include "canvas_determinism.h"

typedef struct {
    uint64_t tick;
    uint16_t lane_id;
    uint16_t page_id;
    WhRecord rec; /* raw WH record */
} InjectItem;

typedef struct {
    InjectItem* items;
    uint32_t count;
    uint32_t cap;
} InjectBatch;

/* Collect IO events for 'tick' into batch (no sort). Returns number collected. */
uint32_t inject_collect(EngineContext* ctx, uint64_t tick, InjectBatch* out);

/* Deterministic sort: (tick, lane_id, page_id, dev, op, ref) */
void inject_sort(InjectBatch* b);

/* Apply: user-provided hook will map to lane/page input queues. */
typedef void (*InjectApplyFn)(EngineContext* ctx, const InjectItem* it, void* user);
void inject_apply(EngineContext* ctx, const InjectBatch* b, InjectApplyFn fn, void* user);

/* Utility lifecycle */
void inject_batch_reserve(InjectBatch* b, uint32_t cap);
void inject_batch_free(InjectBatch* b);

/* ── 원샷 API ── */
/* collect → sort → apply 를 한 번에 수행. tick boundary에서 호출. */
uint32_t inject_run_tick(EngineContext* ctx, uint64_t tick,
                         InjectApplyFn fn, void* user);

/* ── 표준 hook 구현 ── */

/* DEV_KBD/KBD_KEYDOWN → gate_id(ref 하위16) OPEN */
void inject_hook_gate_open(EngineContext* ctx, const InjectItem* it, void* user);

/* DEV_FS/FS_{READ,WRITE}_DONE → lane BH 에너지 +64 */
void inject_hook_fs_done(EngineContext* ctx, const InjectItem* it, void* user);

/* 위 모든 hook 체인 (기본 권장) */
void inject_hook_all(EngineContext* ctx, const InjectItem* it, void* user);
