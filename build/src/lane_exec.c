/*
 * lane_exec.c — Phase 6: Lane 실행 + Tick Boundary Merge
 *
 * [W-1] lane_exec_tick: (lane_id, page_id) 단위로 canvas 스캔 + bpage 해석
 * [W-2] merge_tick: tick 경계에서만 호출, lane_id 오름차순 고정
 * [DK-1] TickBoundaryGuard로 경계 강제
 * [DK-2] 정수 연산만
 * [DK-3] cell_index 오름차순 정렬
 */
#include "../include/lane_exec.h"
#include "../include/canvas_lane.h"
#include "../include/canvas_merge.h"
#include "../include/canvas_determinism.h"
#include "../include/bpage_table.h"
#include "../include/engine_time.h"
#include "../include/inject.h"

#include <string.h>
#include <stdlib.h>

/* ── 전역 Bpage 테이블 (엔진 초기화 시 한 번 세팅) ── */
static BpageTable g_bpage;
static bool       g_bpage_ready = false;

static const BpageTable* get_bpage(void) {
    if (!g_bpage_ready) {
        bpage_init_default(&g_bpage);
        g_bpage_ready = true;
    }
    return &g_bpage;
}

/* ── 틱-로컬 Δ 수집 버퍼 (lane_exec_tick → merge_tick 전달) ── */
#define LANE_DELTA_MAX 1024u
typedef struct {
    Delta    buf[LANE_DELTA_MAX];
    uint32_t count;
    uint8_t  lane_id;
    uint64_t tick;
} LaneDeltaBuf;

/* thread-local이 없으면 전역 static으로 (단일 스레드 / Phase 6 기본) */
static LaneDeltaBuf s_deltas[256];   /* lane_id 0..255 */

/* ── 셀 실행 + Δ 생성 ── */
static void exec_cell_lane(EngineContext* ctx,
                           LaneDeltaBuf* db,
                           uint16_t cx, uint16_t cy,
                           uint8_t lane_id) {
    if (db->count >= LANE_DELTA_MAX) return;

    uint32_t idx = DK_INT(cy) * CANVAS_W + DK_INT(cx);
    Cell* c = &ctx->cells[idx];

    const BpageEntry* bp = bpage_resolve(get_bpage(), c->B);
    if (!bp) return;

    uint8_t new_G = c->G;
    uint8_t new_R = c->R;
    uint8_t new_B = c->B;
    uint32_t new_A = c->A;
    bool changed = false;

    switch (bp->kind) {
    case BP_KIND_NOP:
        break;

    case BP_KIND_OP:
        /* opcode 실행: G 감쇠 (DK-2 정수, DK-4 clamp) */
        if (c->G > 0) {
            new_G = (uint8_t)DK_CLAMP_U8((uint32_t)DK_INT(c->G) - 1u);
            changed = true;
        }
        /* G=0 → lane sleep (B를 NOP으로) */
        if (new_G == 0 && c->B != 0) {
            new_B   = 0;
            changed = true;
        }
        break;

    case BP_KIND_RULE:
        /* 규칙 기반: arg=rule_id 처리 (Phase 6: 기본 identity) */
        (void)bp->arg;
        break;

    case BP_KIND_IOMAP:
        /* IO 매핑: WH에 IO 이벤트 기록 (비결정론 어댑터 호출 전용) */
        /* Phase 6: no-op, 실제 IO는 inject.c가 처리 */
        (void)bp->aux;
        break;
    }

    if (!changed) return;

    /* Δ 기록 */
    Delta* d = &db->buf[db->count++];
    d->tick     = (uint32_t)db->tick;
    d->x        = cx;
    d->y        = cy;
    d->lane_id  = lane_id;
    d->before_A = c->A; d->after_A = new_A;
    d->before_B = c->B; d->after_B = new_B;
    d->before_G = c->G; d->after_G = new_G;
    d->before_R = c->R; d->after_R = new_R;
    d->flags    = 0;
}

/* ════════════════════════════════════════════
   lane_exec_tick
   - (lane_id, page_id) 로 식별되는 영역 스캔
   - bpage 해석 → Δ 생성 (직접 overwrite 안 함)
   - inject_run_tick으로 IO 이벤트 처리
   ════════════════════════════════════════════ */
void lane_exec_tick(EngineContext* ctx, LaneExecKey k) {
    if (!ctx || !ctx->cells) return;

    uint8_t lid = (uint8_t)k.lane_id;
    LaneDeltaBuf* db = &s_deltas[lid];
    db->count   = 0;
    db->lane_id = lid;
    db->tick    = k.tick;

    /* [DK-1] tick boundary 선언 */
    TickBoundaryGuard g = dk_begin_tick(ctx, "lane_exec");

    /* [W-1] lane_id에 속하는 열린 타일만 스캔 [DK-3] tile_id 오름차순 */
    for (uint32_t tile_id = 0; tile_id < TILE_COUNT; tile_id++) {
        if (!ctx->active_open || !ctx->active_open[tile_id]) continue;

        uint32_t tx = (tile_id % TILES_X) * TILE;
        uint32_t ty = (tile_id / TILES_X) * TILE;

        for (uint32_t dy = 0; dy < (uint32_t)TILE; dy++) {
            for (uint32_t dx = 0; dx < (uint32_t)TILE; dx++) {
                uint32_t cx = tx + dx;
                uint32_t cy = ty + dy;
                if (cx >= CANVAS_W || cy >= CANVAS_H) continue;

                const Cell* c = &ctx->cells[cy * CANVAS_W + cx];
                /* [DK-2] A[31:24] == lane_id (정수 비교) */
                if ((uint8_t)(DK_INT(c->A) >> LANE_ID_SHIFT) != DK_INT(lid)) continue;
                if (DK_INT(c->B) == 0) continue;   /* B=NOP 스킵 */

                exec_cell_lane(ctx, db, (uint16_t)cx, (uint16_t)cy, lid);
            }
        }
    }

    /* IO 이벤트 주입 (tick boundary 안에서) */
    inject_run_tick(ctx, k.tick, inject_hook_all, NULL);

    dk_end_tick(&g);
}

/* ════════════════════════════════════════════
   merge_tick
   - 모든 lane의 Δ를 lane_id 오름차순으로 merge
   - [DK-1] tick 경계에서만 호출
   - [DK-3] merge_run 내부에서 cell_index 오름차순 정렬
   ════════════════════════════════════════════ */
void merge_tick(EngineContext* ctx, uint64_t tick) {
    if (!ctx) return;

    /* [DK-1] tick 경계 선언 */
    TickBoundaryGuard g = dk_begin_tick(ctx, "merge_tick");

    MergeConfig cfg = merge_config_default();
    cfg.policy = MERGE_LAST_WINS;  /* 기본: 마지막 Δ 우선 */

    /* [W-3] [DK-3] lane_id 0→255 오름차순으로 merge 적용 */
    for (int lid = 0; lid < 256; lid++) {
        LaneDeltaBuf* db = &s_deltas[lid];
        if (db->count == 0 || db->tick != tick) continue;

        /* WH에 lane-tagged 기록 포함한 merge */
        int r = merge_run(ctx, db->buf, db->count, cfg);
        (void)r;  /* 오류는 merge_run 내부에서 처리 */
        db->count = 0;
    }

    /* BH 요약 (IDLE 구간 감지, tick 경계에서만) */
    /* 전체 gate에 대해 BH-IDLE 체크 — 비용이 크므로 매 N틱마다 */
    if ((tick & 0x3Fu) == 0) {  /* 64틱마다 */
        for (int gid = 0; gid < TILE_COUNT; gid += 16) {
            /* 샘플링: 16배수 gate만 체크 (Phase 6 최소) */
            BhSummary s;
            uint32_t from = (uint32_t)(tick > 64 ? tick - 64 : 0);
            if (bh_analyze_window(ctx, from, (uint32_t)tick,
                                  (uint16_t)gid, &s)) {
                bh_compress(ctx, &s, &g);
            }
        }
    }

    dk_end_tick(&g);
}

/* ════════════════════════════════════════════
   lane_exec_full_tick: 공개 원샷 API
   - 모든 active lane에 대해 exec → inject → merge 순서 실행
   - workers.c의 workers_run_tick 대신 단일 스레드 버전으로 사용 가능
   ════════════════════════════════════════════ */
void lane_exec_full_tick(EngineContext* ctx, LaneTable* lt) {
    if (!ctx || !lt) return;
    uint64_t tick = ctx->tick;

    /* [W-1] 각 active lane을 priority 오름차순으로 exec */
    /* priority 정렬: 간단히 0→255 순회 (priority=0이 최고) */
    for (int lid = 0; lid < 256; lid++) {
        const LaneDesc* ld = &lt->lanes[lid];
        if (!(ld->flags & LANE_F_ACTIVE)) continue;
        LaneExecKey k = { .lane_id=(uint16_t)lid, .page_id=0, .tick=tick };
        lane_exec_tick(ctx, k);
    }

    /* [W-2] 모든 lane exec 완료 후 merge */
    merge_tick(ctx, tick);
    engctx_tick(ctx);
}
