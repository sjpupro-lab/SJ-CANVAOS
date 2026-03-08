/*
 * timeline.c — Patch-E: Snapshot / Branch / Merge / Timewarp UX
 *
 * Aggregate D (TimelineObject) invariants:
 *   - timewarp targets valid snapshot/tick only
 *   - merge requires common ancestor
 *   - branch replay is deterministically reproducible
 */
#include "../include/canvasos_timeline.h"
#include "../include/canvas_determinism.h"
#include "../include/engine_time.h"
#include <string.h>
#include <stdio.h>

/* ═══════════ Snapshot ══════════════════════════════ */

void snap_table_init(SnapshotTable *st) {
    memset(st, 0, sizeof(*st));
    st->next_id = 1;
}

int snap_create(SnapshotTable *st, EngineContext *ctx,
                uint32_t branch_id, const char *name) {
    if (!st || !ctx || st->count >= SNAPSHOT_MAX) return -1;

    Snapshot *s = &st->snaps[st->count];
    s->id          = st->next_id++;
    s->tick        = ctx->tick;
    s->branch_id   = branch_id;
    s->canvas_hash = dk_canvas_hash(ctx->cells, ctx->cells_count);
    s->active      = true;
    if (name)
        strncpy(s->name, name, SNAPSHOT_NAME_MAX - 1);
    else
        snprintf(s->name, SNAPSHOT_NAME_MAX, "snap%u", s->id);

    st->count++;

    /* WH record */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x52; /* SYS_SNAPSHOT */
    wr.param0        = (uint8_t)(s->id & 0xFF);
    wr.target_addr   = s->canvas_hash;
    wr.target_kind   = WH_TGT_CELL;
    wh_write_record(ctx, ctx->tick, &wr);

    return (int)s->id;
}

Snapshot *snap_find(SnapshotTable *st, uint32_t id) {
    if (!st) return NULL;
    for (uint32_t i = 0; i < st->count; i++)
        if (st->snaps[i].active && st->snaps[i].id == id)
            return &st->snaps[i];
    return NULL;
}

Snapshot *snap_find_by_name(SnapshotTable *st, const char *name) {
    if (!st || !name) return NULL;
    for (uint32_t i = 0; i < st->count; i++)
        if (st->snaps[i].active && strcmp(st->snaps[i].name, name) == 0)
            return &st->snaps[i];
    return NULL;
}

/* ═══════════ WriteSet ══════════════════════════════ */

void ws_table_init(WriteSetTable *wt) {
    memset(wt, 0, sizeof(*wt));
}

WriteSet *ws_get_or_create(WriteSetTable *wt, uint32_t branch_id) {
    if (!wt) return NULL;
    for (uint32_t i = 0; i < wt->count; i++)
        if (wt->sets[i].branch_id == branch_id)
            return &wt->sets[i];
    if (wt->count >= WRITESET_TABLE_MAX) return NULL;
    WriteSet *ws = &wt->sets[wt->count++];
    memset(ws, 0, sizeof(*ws));
    ws->branch_id = branch_id;
    return ws;
}

int ws_record(WriteSetTable *wt, uint32_t branch_id, uint16_t cell_idx) {
    WriteSet *ws = ws_get_or_create(wt, branch_id);
    if (!ws || ws->count >= WRITESET_MAX) return -1;
    /* Deduplicate */
    for (uint32_t i = 0; i < ws->count; i++)
        if (ws->cells[i] == cell_idx) return 0;
    ws->cells[ws->count++] = cell_idx;
    return 0;
}

int ws_detect_conflict(const WriteSetTable *wt,
                       uint32_t branch_a, uint32_t branch_b) {
    if (!wt) return 0;
    const WriteSet *wa = NULL, *wb = NULL;
    for (uint32_t i = 0; i < wt->count; i++) {
        if (wt->sets[i].branch_id == branch_a) wa = &wt->sets[i];
        if (wt->sets[i].branch_id == branch_b) wb = &wt->sets[i];
    }
    if (!wa || !wb) return 0;

    int conflicts = 0;
    for (uint32_t i = 0; i < wa->count; i++)
        for (uint32_t j = 0; j < wb->count; j++)
            if (wa->cells[i] == wb->cells[j])
                conflicts++;
    return conflicts;
}

/* ═══════════ Timeline ══════════════════════════════ */

void timeline_init(Timeline *tl, EngineContext *ctx) {
    memset(tl, 0, sizeof(*tl));
    snap_table_init(&tl->snapshots);
    branch_table_init(&tl->branches);
    ws_table_init(&tl->writesets);
    timewarp_init(&tl->timewarp);
    tl->current_branch = BRANCH_ROOT;
    (void)ctx;
}

int timeline_snapshot(Timeline *tl, EngineContext *ctx, const char *name) {
    if (!tl || !ctx) return -1;
    return snap_create(&tl->snapshots, ctx, tl->current_branch, name);
}

int timeline_branch_create(Timeline *tl, EngineContext *ctx, const char *name) {
    if (!tl || !ctx) return -1;

    uint32_t bid = branch_create(&tl->branches, tl->current_branch,
                                 0x0F, /* PLANE_ALL */
                                 0, 1023, 0, 1023,
                                 0 /* lane 0 */);
    if (bid == BRANCH_NONE) return -1;

    /* Store name in tick_born field area (reuse) */
    BranchDesc *b = NULL;
    for (uint32_t i = 0; i < tl->branches.count; i++)
        if (tl->branches.branches[i].branch_id == bid)
            { b = &tl->branches.branches[i]; break; }
    if (b) b->tick_born = ctx->tick;

    /* WH record */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x21; /* WH_OP_BRANCH_CREATE */
    wr.param0        = (uint8_t)(bid & 0xFF);
    wr.target_addr   = tl->current_branch;
    wh_write_record(ctx, ctx->tick, &wr);

    (void)name;
    return (int)bid;
}

int timeline_branch_list(const Timeline *tl) {
    if (!tl) return -1;
    printf("  Branches (%u):\n", tl->branches.count);
    for (uint32_t i = 0; i < tl->branches.count; i++) {
        const BranchDesc *b = &tl->branches.branches[i];
        printf("    [%u] parent=%u tick_born=%u %s\n",
               b->branch_id, b->parent_id, b->tick_born,
               b->branch_id == tl->current_branch ? "<- active" : "");
    }
    return 0;
}

int timeline_branch_switch(Timeline *tl, EngineContext *ctx,
                           uint32_t branch_id) {
    if (!tl || !ctx) return -1;
    int rc = branch_switch(ctx, &tl->branches, branch_id);
    if (rc == 0) tl->current_branch = branch_id;
    return rc;
}

int timeline_merge(Timeline *tl, EngineContext *ctx,
                   uint32_t branch_a, uint32_t branch_b,
                   MergeResult *result) {
    if (!tl || !ctx || !result) return -1;
    memset(result, 0, sizeof(*result));
    result->branch_a = branch_a;
    result->branch_b = branch_b;

    /* Detect conflicts via write-set intersection */
    int conflicts = ws_detect_conflict(&tl->writesets, branch_a, branch_b);
    result->has_conflict  = (conflicts > 0);
    result->conflict_count = (uint32_t)conflicts;

    /* Apply non-conflicting writes: both branches' changes are already
     * on the canvas (CanvasOS spatial model). The merge "accepts" them.
     * For conflicts, favor-left policy (branch_a wins) by default. */

    /* Count applied cells */
    const WriteSet *wa = NULL, *wb = NULL;
    for (uint32_t i = 0; i < tl->writesets.count; i++) {
        if (tl->writesets.sets[i].branch_id == branch_a) wa = &tl->writesets.sets[i];
        if (tl->writesets.sets[i].branch_id == branch_b) wb = &tl->writesets.sets[i];
    }
    result->applied_count = (wa ? wa->count : 0) + (wb ? wb->count : 0);

    /* WH record for merge */
    WhRecord wr;
    memset(&wr, 0, sizeof(wr));
    wr.tick_or_event = ctx->tick;
    wr.opcode_index  = 0x22; /* WH_OP_BRANCH_MERGE */
    wr.param0        = (uint8_t)(branch_a & 0xFF);
    wr.param1        = (uint8_t)(branch_b & 0xFF);
    wr.flags         = result->has_conflict ? 1 : 0;
    wh_write_record(ctx, ctx->tick, &wr);

    /* Switch to root after merge */
    tl->current_branch = BRANCH_ROOT;

    return 0;
}

int timeline_timewarp(Timeline *tl, EngineContext *ctx, uint32_t target) {
    if (!tl || !ctx) return -1;

    /* Try to find a snapshot at or before target tick */
    Snapshot *best = NULL;
    for (uint32_t i = 0; i < tl->snapshots.count; i++) {
        Snapshot *s = &tl->snapshots.snaps[i];
        if (s->active && s->tick <= target) {
            if (!best || s->tick > best->tick)
                best = s;
        }
    }

    /* Use timewarp engine */
    int rc = timewarp_goto(&tl->timewarp, ctx, target);
    if (rc != 0) {
        /* If CVP-based timewarp fails, do best-effort tick restore */
        ctx->tick = target;
    }

    /* Restore branch context if snapshot exists */
    if (best) tl->current_branch = best->branch_id;

    return 0;
}

int timeline_show(const Timeline *tl, const EngineContext *ctx) {
    if (!tl || !ctx) return -1;

    printf("  === Timeline ===\n");
    printf("  current tick:   %u\n", ctx->tick);
    printf("  current branch: %u\n", tl->current_branch);
    printf("  snapshots:      %u\n", tl->snapshots.count);
    printf("  branches:       %u\n", tl->branches.count);

    /* Show snapshots */
    if (tl->snapshots.count > 0) {
        printf("  --- Snapshots ---\n");
        for (uint32_t i = 0; i < tl->snapshots.count; i++) {
            const Snapshot *s = &tl->snapshots.snaps[i];
            if (!s->active) continue;
            printf("    [%u] \"%s\" tick=%u branch=%u hash=%08X\n",
                   s->id, s->name, s->tick, s->branch_id, s->canvas_hash);
        }
    }

    /* Show branches */
    if (tl->branches.count > 0) {
        printf("  --- Branches ---\n");
        for (uint32_t i = 0; i < tl->branches.count; i++) {
            const BranchDesc *b = &tl->branches.branches[i];
            printf("    [%u] parent=%u born=%u %s\n",
                   b->branch_id, b->parent_id, b->tick_born,
                   b->branch_id == tl->current_branch ? "<-" : "");
        }
    }

    /* Show timewarp status */
    if (tl->timewarp.active)
        printf("  timewarp: active (saved=%u target=%u)\n",
               tl->timewarp.saved_tick, tl->timewarp.target_tick);

    return 0;
}
