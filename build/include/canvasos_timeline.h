#pragma once
/*
 * canvasos_timeline.h — Patch-E: Snapshot / Timeline / Branch UX
 *
 * Aggregate D (TimelineObject) invariants:
 *   - timewarp only to valid snapshot/tick
 *   - merge requires common ancestor tick
 *   - branch replay is deterministically reproducible
 */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_engine_ctx.h"
#include "canvas_branch.h"
#include "canvasos_timewarp.h"

/* ── Snapshot ────────────────────────────────────────── */
#define SNAPSHOT_MAX     16
#define SNAPSHOT_NAME_MAX 16

typedef struct {
    uint32_t  id;
    uint32_t  tick;
    uint32_t  branch_id;       /* branch active at snapshot time */
    uint32_t  canvas_hash;
    char      name[SNAPSHOT_NAME_MAX];
    bool      active;
} Snapshot;

typedef struct {
    Snapshot  snaps[SNAPSHOT_MAX];
    uint32_t  count;
    uint32_t  next_id;
} SnapshotTable;

/* ── Write Set (per-branch mutation tracking) ────────── */
#define WRITESET_MAX  256

typedef struct {
    uint32_t  branch_id;
    uint16_t  cells[WRITESET_MAX]; /* cell indices (low 16 bits) */
    uint32_t  count;
} WriteSet;

#define WRITESET_TABLE_MAX  16

typedef struct {
    WriteSet  sets[WRITESET_TABLE_MAX];
    uint32_t  count;
} WriteSetTable;

/* ── Merge Result ────────────────────────────────────── */
typedef struct {
    bool      has_conflict;
    uint32_t  conflict_count;
    uint32_t  applied_count;
    uint32_t  branch_a;
    uint32_t  branch_b;
} MergeResult;

/* ── Timeline ────────────────────────────────────────── */
typedef struct {
    SnapshotTable  snapshots;
    BranchTable    branches;
    WriteSetTable  writesets;
    TimeWarp       timewarp;
    uint32_t       current_branch;
} Timeline;

/* ── API ─────────────────────────────────────────────── */

/* Snapshot */
void     snap_table_init(SnapshotTable *st);
int      snap_create(SnapshotTable *st, EngineContext *ctx,
                     uint32_t branch_id, const char *name);
Snapshot *snap_find(SnapshotTable *st, uint32_t id);
Snapshot *snap_find_by_name(SnapshotTable *st, const char *name);

/* Timeline (unified interface) */
void     timeline_init(Timeline *tl, EngineContext *ctx);
int      timeline_snapshot(Timeline *tl, EngineContext *ctx, const char *name);
int      timeline_branch_create(Timeline *tl, EngineContext *ctx, const char *name);
int      timeline_branch_list(const Timeline *tl);
int      timeline_branch_switch(Timeline *tl, EngineContext *ctx, uint32_t branch_id);
int      timeline_merge(Timeline *tl, EngineContext *ctx,
                        uint32_t branch_a, uint32_t branch_b,
                        MergeResult *result);
int      timeline_timewarp(Timeline *tl, EngineContext *ctx, uint32_t target);
int      timeline_show(const Timeline *tl, const EngineContext *ctx);

/* Write-set tracking */
void     ws_table_init(WriteSetTable *wt);
WriteSet *ws_get_or_create(WriteSetTable *wt, uint32_t branch_id);
int      ws_record(WriteSetTable *wt, uint32_t branch_id, uint16_t cell_idx);
int      ws_detect_conflict(const WriteSetTable *wt,
                            uint32_t branch_a, uint32_t branch_b);
