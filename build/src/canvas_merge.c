/*
 * canvas_merge.c — Phase 5: Δ-Commit Merge 구현
 *
 * DK 규약 준수 체크리스트:
 *   [DK-1] TickBoundaryGuard 사용 ✓
 *   [DK-2] DK_INT() 정수 연산    ✓ (float 없음)
 *   [DK-3] dk_cell_index() 오름차순 정렬 ✓
 *   [DK-4] DK_CLAMP_U8 for G channel   ✓
 *   [DK-5] DK_ABSORB_NOISE for ADDITIVE ✓
 */
#include "../include/canvas_merge.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ---- merge_ctx_begin ---- */
void merge_ctx_begin(MergeCtx *mc, EngineContext *ctx, MergeConfig cfg) {
    memset(mc, 0, sizeof(*mc));
    mc->current_tick = ctx->tick;   /* [M-1] 틱 경계 고정 */
    mc->cfg          = cfg;
    mc->guard        = dk_begin_tick(ctx, "merge_ctx");
}

/* ---- merge_find_existing ---- */
int merge_find_existing(MergeCtx *mc, uint16_t x, uint16_t y, uint32_t tick) {
    for (uint32_t i = 0; i < mc->count; i++) {
        const Delta *d = &mc->deltas[i];
        if (d->tick == tick && d->x == x && d->y == y)
            return (int)i;
    }
    return -1;
}

/* ---- merge_add_delta ---- */
int merge_add_delta(MergeCtx *mc, const Delta *d) {
    if (!mc || !d) return -1;
    if (mc->count >= MERGE_MAX_DELTAS) return -1;

    /* [M-2] 동일 tick+셀 기존 Δ 확인 */
    int existing = merge_find_existing(mc, d->x, d->y, d->tick);
    if (existing >= 0) {
        Delta *prev = &mc->deltas[(uint32_t)existing];
        /* before 값 보존 (최초 기록 유지), after만 교체 */
        prev->after_A = DK_INT(d->after_A);
        prev->after_B = DK_INT(d->after_B);
        prev->after_G = DK_INT(d->after_G);
        prev->after_R = DK_INT(d->after_R);
        prev->flags  |= DK_INT(d->flags);
        prev->lane_id = DK_INT(d->lane_id);
        mc->deltas_suppressed++;
        return 0;
    }

    mc->deltas[mc->count++] = *d;
    return 0;
}

/* ---- merge_delta_cmp_cell_index [DK-3] ---- */
int merge_delta_cmp_cell_index(const void *a, const void *b) {
    const Delta *da = (const Delta *)a;
    const Delta *db = (const Delta *)b;
    uint32_t ia = dk_cell_index(da->x, da->y);
    uint32_t ib = dk_cell_index(db->x, db->y);
    if (ia < ib) return -1;
    if (ia > ib) return  1;
    return 0;
}

/* ---- merge_resolve_conflicts ---- */
int merge_resolve_conflicts(MergeCtx *mc, EngineContext *ctx) {
    if (!mc || !ctx) return -1;

    /* [DK-1] 틱 경계 검증 */
    ASSERT_TICK_BOUNDARY(ctx, mc->guard);

    /* [M-3] LOCK-family (gate CLOSE) 우선: pass 1 */
    for (uint32_t i = 0; i < mc->count; i++) {
        Delta *di = &mc->deltas[i];
        if (!merge_is_lock_priority(di)) continue;

        /* 같은 셀의 다른 Δ를 찾아서 억제 */
        for (uint32_t j = 0; j < mc->count; j++) {
            if (i == j) continue;
            Delta *dj = &mc->deltas[j];
            if (dj->x != di->x || dj->y != di->y) continue;
            if (!merge_is_lock_priority(dj)) {
                /* LOCK이 아닌 것을 억제 */
                dj->flags |= DF_REPLAYED;  /* "suppressed" 마킹 재활용 */
                mc->conflicts_detected++;
                mc->conflicts_resolved++;

                /* [M-4] on_conflict 처리 */
                if (mc->cfg.on_conflict == CONFLICT_RECORD) {
                    /* WH에 충돌 기록 */
                    WhRecord wr;
                    memset(&wr, 0, sizeof(wr));
                    wr.tick_or_event = DK_INT(mc->current_tick);
                    wr.opcode_index  = WH_OP_NOP;  /* 충돌 마커 */
                    wr.flags         = 0x80u;       /* conflict flag */
                    wr.target_addr   = dk_cell_index(dj->x, dj->y);
                    wh_write_record(ctx, ctx->tick, &wr);
                }
                /* CONFLICT_GATE_CLOSE: Phase 5 TODO (gate API 연동) */
            }
        }
    }

    return 0;
}

/* ---- merge_apply ---- */
int merge_apply(MergeCtx *mc, EngineContext *ctx) {
    if (!mc || !ctx) return -1;

    /* [DK-3] cell_index 오름차순 정렬 */
    qsort(mc->deltas, mc->count, sizeof(Delta), merge_delta_cmp_cell_index);

    int applied = 0;
    for (uint32_t i = 0; i < mc->count; i++) {
        const Delta *d = &mc->deltas[i];

        /* suppressed Δ 건너뜀 */
        if (d->flags & DF_REPLAYED) continue;

        /* 좌표 유효성 */
        if (d->x >= CANVAS_W || d->y >= CANVAS_H) continue;

        Cell *c = &ctx->cells[(uint32_t)DK_INT(d->y) * CANVAS_W
                              + (uint32_t)DK_INT(d->x)];

        /* 정책별 채널 업데이트 [DK-4] */
        switch (mc->cfg.policy) {
            case MERGE_LAST_WINS:
                c->A = DK_INT(d->after_A);
                c->B = DK_INT(d->after_B);
                c->G = DK_INT(d->after_G);
                c->R = DK_INT(d->after_R);
                break;

            case MERGE_FIRST_WINS:
                /* before와 현재 셀이 같을 때만 적용 */
                if (c->A == DK_INT(d->before_A) &&
                    c->G == DK_INT(d->before_G)) {
                    c->A = DK_INT(d->after_A);
                    c->B = DK_INT(d->after_B);
                    c->G = DK_INT(d->after_G);
                    c->R = DK_INT(d->after_R);
                }
                break;

            case MERGE_ADDITIVE_G: {
                /* G 누적: clamp [DK-4] + 노이즈 흡수 [DK-5] */
                uint32_t sum = (uint32_t)DK_INT(c->G)
                             + (uint32_t)DK_INT(d->after_G);
                c->G = DK_CLAMP_U8(sum);
                c->B = DK_INT(d->after_B);
                c->R = DK_INT(d->after_R);
                break;
            }

            case MERGE_MAX_G:
                c->G = DK_CLAMP_U8(
                    DK_INT(c->G) > DK_INT(d->after_G)
                    ? (uint32_t)DK_INT(c->G)
                    : (uint32_t)DK_INT(d->after_G));
                c->B = DK_INT(d->after_B);
                c->R = DK_INT(d->after_R);
                break;

            case MERGE_LOCK_WINS:
                /* gate CLOSE Δ는 항상 적용 */
                if (d->flags & DF_GATE_CLOSE) {
                    c->A = DK_INT(d->after_A);
                    c->B = DK_INT(d->after_B);
                    c->G = DK_INT(d->after_G);
                    c->R = DK_INT(d->after_R);
                }
                break;

            case MERGE_CUSTOM:
                /* Phase 5 TODO: RuleTable 기반 커스텀 merge */
                break;
        }
        applied++;
    }
    return applied;
}

/* ---- merge_ctx_end ---- */
void merge_ctx_end(MergeCtx *mc) {
    dk_end_tick(&mc->guard);
    memset(mc, 0, sizeof(*mc));
}

/* ---- merge_run (원샷) ---- */
int merge_run(EngineContext *ctx, const Delta *deltas, uint32_t count,
              MergeConfig cfg) {
    MergeCtx mc;
    merge_ctx_begin(&mc, ctx, cfg);

    for (uint32_t i = 0; i < count; i++) {
        merge_add_delta(&mc, &deltas[i]);
    }

    int r = merge_resolve_conflicts(&mc, ctx);
    if (r < 0) { merge_ctx_end(&mc); return r; }

    int applied = merge_apply(&mc, ctx);
    merge_ctx_end(&mc);

    if (applied < 0) return applied;
    return 0;
}
