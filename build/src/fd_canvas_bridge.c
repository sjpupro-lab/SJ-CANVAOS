/*
 * fd_canvas_bridge.c — Patch-B: FD ↔ CanvasFS Real Bridge
 *
 * Aggregate B (FsObject) invariants:
 *   - payload length and metadata length match
 *   - file create allocates a real CanvasFS slot
 *   - reopen after close returns same data (byte-exact)
 */
#include "../include/canvasos_fd.h"
#include "../include/canvasos_path.h"
#include "../include/canvasfs.h"
#include <string.h>
#include <stdio.h>

/* ── CanvasFS context ────────────────────────────────── */
static CanvasFS *g_bridge_fs   = NULL;
static uint16_t  g_bridge_volh = 0;

/* ── Name→FsKey registry for file persistence ────────── */
#define FILE_REG_MAX  64
#define FILE_NAME_MAX 64

typedef struct {
    char     name[FILE_NAME_MAX];
    FsKey    key;
    bool     active;
} FileRegEntry;

static FileRegEntry g_file_reg[FILE_REG_MAX];
static int          g_file_reg_count = 0;

void fd_bridge_init(CanvasFS *fs) {
    g_bridge_fs = fs;
    memset(g_file_reg, 0, sizeof(g_file_reg));
    g_file_reg_count = 0;
}

void fd_bridge_set_volume(uint16_t volh) {
    g_bridge_volh = volh;
}

static FileRegEntry *reg_find(const char *path) {
    for (int i = 0; i < FILE_REG_MAX; i++)
        if (g_file_reg[i].active && strcmp(g_file_reg[i].name, path) == 0)
            return &g_file_reg[i];
    return NULL;
}

static FileRegEntry *reg_create(const char *path, FsKey key) {
    FileRegEntry *ex = reg_find(path);
    if (ex) return ex;
    for (int i = 0; i < FILE_REG_MAX; i++) {
        if (!g_file_reg[i].active) {
            strncpy(g_file_reg[i].name, path, FILE_NAME_MAX - 1);
            g_file_reg[i].key = key;
            g_file_reg[i].active = true;
            if (i >= g_file_reg_count) g_file_reg_count = i + 1;
            return &g_file_reg[i];
        }
    }
    return NULL;
}

static int alloc_file_slot(FsKey *out) {
    if (!g_bridge_fs || g_bridge_volh == 0) return -1;
    uint8_t slot;
    FsResult r = fs_alloc_slot(g_bridge_fs, g_bridge_volh, &slot);
    if (r != FS_OK) return -1;
    out->gate_id = g_bridge_volh;
    out->slot    = slot;
    return 0;
}

/* ── Bind FsKey to a FileDesc ────────────────────────── */
int fd_file_bind(FileDesc *fd, FsKey key, uint8_t flags) {
    if (!fd) return -1;
    memset(fd, 0, sizeof(*fd));
    fd->key    = key;
    fd->flags  = flags;
    fd->type   = FD_FILE;
    fd->active = true;
    fd->cursor = 0;
    return 0;
}

/* ── Read from CanvasFS slot ─────────────────────────── */
int fd_file_read_slot(void *ctx_v, FileDesc *fd, uint8_t *buf, uint16_t len) {
    (void)ctx_v;
    if (!fd || !buf || len == 0) return -1;
    if (!g_bridge_fs) return 0;

    uint8_t payload[1024];
    size_t actual = 0;
    FsResult rc = fs_read(g_bridge_fs, fd->key, payload, sizeof(payload), &actual);
    if (rc != FS_OK || actual == 0) return 0;

    if (fd->cursor >= (uint16_t)actual) return 0;
    uint16_t avail = (uint16_t)(actual - fd->cursor);
    uint16_t to_read = len < avail ? len : avail;
    memcpy(buf, payload + fd->cursor, to_read);
    fd->cursor += to_read;
    return (int)to_read;
}

/* ── Write to CanvasFS slot ──────────────────────────── */
int fd_file_write_slot(void *ctx_v, FileDesc *fd, const uint8_t *buf, uint16_t len) {
    (void)ctx_v;
    if (!fd || !buf || len == 0) return -1;
    if (!g_bridge_fs) return -1;

    if (fd->flags & O_APPEND) {
        FsSlotClass cls;
        uint32_t cur_len = 0;
        if (fs_stat(g_bridge_fs, fd->key, &cls, &cur_len) == FS_OK)
            fd->cursor = (uint16_t)cur_len;
    }

    uint8_t payload[1024];
    memset(payload, 0, sizeof(payload));
    size_t existing = 0;
    fs_read(g_bridge_fs, fd->key, payload, sizeof(payload), &existing);

    uint16_t write_end = fd->cursor + len;
    if (write_end > (uint16_t)sizeof(payload))
        write_end = (uint16_t)sizeof(payload);
    uint16_t actual_write = write_end - fd->cursor;
    memcpy(payload + fd->cursor, buf, actual_write);

    size_t total = existing > write_end ? existing : write_end;
    FsResult rc = fs_write(g_bridge_fs, fd->key, payload, total);
    if (rc != FS_OK) return -1;

    fd->cursor = write_end;
    return (int)actual_write;
}

/* ═══════════════════════════════════════════════════════
 * fd_open_bridged — Full path-aware file open with
 * real CanvasFS slot allocation
 * ═══════════════════════════════════════════════════════ */
int fd_open_bridged(void *ctx_v, PathContext *pc,
                    uint32_t pid, const char *path, uint8_t flags) {
    EngineContext *ctx = (EngineContext *)ctx_v;
    if (!path) return -1;

    FileRegEntry *entry = reg_find(path);
    FsKey key;

    if (entry) {
        key = entry->key;
    } else if (flags & O_CREATE) {
        if (alloc_file_slot(&key) != 0) return -1;
        entry = reg_create(path, key);
        if (!entry) return -1;
        if (pc) {
            const char *basename = strrchr(path, '/');
            basename = basename ? basename + 1 : path;
            path_mkdir(ctx, pc, basename);
        }
    } else {
        return -1; /* ENOENT */
    }

    int fd = fd_open(ctx, pid, path, flags);
    if (fd < 0) return -1;

    fd_bind_key(pid, fd, key);
    return fd;
}

int fd_bridge_stat(const char *path, PathContext *pc,
                   EngineContext *ctx, size_t *out_len) {
    (void)pc; (void)ctx;
    if (!path || !out_len) return -1;
    FileRegEntry *e = reg_find(path);
    if (!e || !g_bridge_fs) return -1;
    FsSlotClass cls; uint32_t len = 0;
    if (fs_stat(g_bridge_fs, e->key, &cls, &len) != FS_OK) return -1;
    *out_len = (size_t)len;
    return 0;
}
