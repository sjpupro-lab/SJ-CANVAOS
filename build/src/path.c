/*
 * path.c — Phase-10: Path Resolution
 *
 * "/" 구분 경로 → CanvasFS DIR1 재귀 탐색.
 * 특수 경로: "." ".." "~" "/wh" "/bh" "/proc"
 */
#include "../include/canvasos_path.h"
#include "../include/canvasos_gate_ops.h"
#include <string.h>
#include <stdio.h>

/* 간이 디렉터리: name_hash → FsKey 매핑 테이블 */
#define PATH_DIR_MAX  64
#define PATH_NAME_MAX 16

typedef struct {
    char    name[PATH_NAME_MAX];
    FsKey   key;
    FsKey   parent;
    bool    is_dir;
    bool    active;
} PathEntry;

static PathEntry g_entries[PATH_DIR_MAX];
static int g_entry_count = 0;

/* root 키 */
static FsKey g_root = {0, 0};

void pathctx_init(PathContext *pc, uint32_t pid, FsKey root) {
    pc->pid  = pid;
    pc->cwd  = root;
    pc->root = root;
    g_root   = root;

    /* 루트 엔트리 등록 */
    if (g_entry_count == 0) {
        memset(g_entries, 0, sizeof(g_entries));
        strncpy(g_entries[0].name, "/", PATH_NAME_MAX);
        g_entries[0].key    = root;
        g_entries[0].parent = root;
        g_entries[0].is_dir = true;
        g_entries[0].active = true;
        g_entry_count = 1;
    }
}

static PathEntry *find_entry(const char *name, FsKey parent) {
    for (int i = 0; i < g_entry_count; i++) {
        PathEntry *e = &g_entries[i];
        if (!e->active) continue;
        if (e->parent.gate_id == parent.gate_id &&
            e->parent.slot == parent.slot &&
            strcmp(e->name, name) == 0)
            return e;
    }
    return NULL;
}

static PathEntry *find_by_key(FsKey key) {
    for (int i = 0; i < g_entry_count; i++) {
        if (!g_entries[i].active) continue;
        if (g_entries[i].key.gate_id == key.gate_id &&
            g_entries[i].key.slot == key.slot)
            return &g_entries[i];
    }
    return NULL;
}

static PathEntry *alloc_entry(void) {
    for (int i = 0; i < PATH_DIR_MAX; i++)
        if (!g_entries[i].active) { g_entry_count++; return &g_entries[i]; }
    return NULL;
}

int path_resolve(EngineContext *ctx, PathContext *pc,
                 const char *path, FsKey *out) {
    (void)ctx;
    if (!pc || !path || !out) return -1;

    /* Try virtual path resolution first */
    extern int path_resolve_virtual(EngineContext*, PathContext*, const char*, FsKey*);
    if (path_resolve_virtual(ctx, pc, path, out) == 0)
        return 0;

    FsKey current;

    if (path[0] == '/') {
        current = pc->root;
        path++; /* skip leading / */
    } else if (path[0] == '~') {
        current = pc->cwd; /* 홈 = cwd (간이 구현) */
        path++;
        if (*path == '/') path++;
    } else {
        current = pc->cwd;
    }

    if (*path == '\0') { *out = current; return 0; }

    /* 토큰 분할 */
    char buf[256];
    strncpy(buf, path, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *tok = strtok(buf, "/");
    while (tok) {
        if (strcmp(tok, ".") == 0) {
            /* 현재 디렉터리 — 변화 없음 */
        }
        else if (strcmp(tok, "..") == 0) {
            PathEntry *e = find_by_key(current);
            if (e) current = e->parent;
        }
        else {
            PathEntry *e = find_entry(tok, current);
            if (!e) return -1; /* not found */
            current = e->key;
        }
        tok = strtok(NULL, "/");
    }

    *out = current;
    return 0;
}

int path_mkdir(EngineContext *ctx, PathContext *pc, const char *name) {
    (void)ctx;
    if (!pc || !name || strlen(name) == 0) return -1;
    if (find_entry(name, pc->cwd)) return -2; /* already exists */

    PathEntry *e = alloc_entry();
    if (!e) return -3; /* full */

    strncpy(e->name, name, PATH_NAME_MAX - 1);
    e->name[PATH_NAME_MAX - 1] = '\0';
    e->key.gate_id = (uint16_t)(g_entry_count + 100); /* 가상 gate_id */
    e->key.slot    = 0;
    e->parent      = pc->cwd;
    e->is_dir      = true;
    e->active      = true;
    return 0;
}

int path_ls(EngineContext *ctx, PathContext *pc, FsKey dir,
            char names[][16], FsKey keys[], int max) {
    (void)ctx; (void)pc;
    int count = 0;
    for (int i = 0; i < g_entry_count && count < max; i++) {
        PathEntry *e = &g_entries[i];
        if (!e->active) continue;
        if (e->parent.gate_id == dir.gate_id &&
            e->parent.slot == dir.slot &&
            !(e->key.gate_id == dir.gate_id && e->key.slot == dir.slot)) {
            strncpy(names[count], e->name, 16);
            keys[count] = e->key;
            count++;
        }
    }
    return count;
}

int path_cd(PathContext *pc, EngineContext *ctx, const char *path) {
    FsKey resolved;
    int rc = path_resolve(ctx, pc, path, &resolved);
    if (rc != 0) return rc;
    /* 디렉터리 확인 */
    PathEntry *e = find_by_key(resolved);
    if (e && !e->is_dir) return -1;
    pc->cwd = resolved;
    return 0;
}

int path_rm(EngineContext *ctx, PathContext *pc, const char *path) {
    FsKey resolved;
    int rc = path_resolve(ctx, pc, path, &resolved);
    if (rc != 0) return rc;
    PathEntry *e = find_by_key(resolved);
    if (!e) return -1;
    e->active = false;
    return 0;
}
