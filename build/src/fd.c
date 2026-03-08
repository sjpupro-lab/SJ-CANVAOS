/*
 * fd.c — Patch-B/C: File Descriptor with real CanvasFS + Pipe bridge
 *
 * Aggregate C (RuntimeObject) invariants:
 *   - fd 0/1/2 are valid default entries (stdin/stdout/stderr)
 *   - FD_PIPE read/write go through actual pipe_read/pipe_write
 *   - FD_FILE read/write go through CanvasFS bridge
 */
#include "../include/canvasos_fd.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_pipe.h"
#include <string.h>
#include <stdio.h>

static FileDesc  g_fds[PROC8_MAX][FD_MAX_PER_PROC];
static uint8_t   g_stdout_buf[4096];
static uint16_t  g_stdout_len = 0;

/* PipeTable pointer for FD_PIPE operations */
static PipeTable *g_fd_pipes = NULL;

void fd_table_init(void) {
    memset(g_fds, 0, sizeof(g_fds));
    g_stdout_len = 0;
    g_fd_pipes = NULL;
}

void fd_set_pipe_table(void *pt) {
    g_fd_pipes = (PipeTable *)pt;
}

static FileDesc *fd_get(uint32_t pid, int fd) {
    if (pid >= PROC8_MAX || fd < 0 || fd >= FD_MAX_PER_PROC) return NULL;
    FileDesc *f = &g_fds[pid][fd];
    return f->active ? f : NULL;
}

static int fd_find_free(uint32_t pid) {
    if (pid >= PROC8_MAX) return -1;
    for (int i = 3; i < FD_MAX_PER_PROC; i++)
        if (!g_fds[pid][i].active) return i;
    return -1;
}

int fd_open(void *ctx_v, uint32_t pid, const char *path, uint8_t flags) {
    (void)ctx_v; (void)path;
    if (pid >= PROC8_MAX) return -1;
    int fd = fd_find_free(pid);
    if (fd < 0) return -1;

    FileDesc *f = &g_fds[pid][fd];
    memset(f, 0, sizeof(*f));
    f->flags  = flags;
    f->type   = FD_FILE;
    f->active = true;
    return fd;
}

/* ── Bind FsKey to an already-opened fd ──────────────── */
void fd_bind_key(uint32_t pid, int fd, FsKey key) {
    if (pid >= PROC8_MAX || fd < 0 || fd >= FD_MAX_PER_PROC) return;
    g_fds[pid][fd].key = key;
}

/* ── Create pipe and return two fds (read + write) ───── */
int fd_pipe_create(void *ctx_v, void *pipes_v,
                   uint32_t pid, int *read_fd, int *write_fd) {
    EngineContext *ctx = (EngineContext *)ctx_v;
    PipeTable *pipes = (PipeTable *)pipes_v;
    if (!pipes || !read_fd || !write_fd) return -1;

    int rfd = fd_find_free(pid);
    if (rfd < 0) return -1;
    g_fds[pid][rfd].active = true; /* reserve */
    g_fds[pid][rfd].type = FD_PIPE;

    int wfd = fd_find_free(pid);
    if (wfd < 0) { g_fds[pid][rfd].active = false; return -1; }

    int pipe_id = pipe_create(pipes, ctx, pid, pid);
    if (pipe_id < 0) {
        g_fds[pid][rfd].active = false;
        return -1;
    }

    /* Read end */
    g_fds[pid][rfd].type    = FD_PIPE;
    g_fds[pid][rfd].pipe_id = pipe_id;
    g_fds[pid][rfd].flags   = O_READ;
    g_fds[pid][rfd].active  = true;

    /* Write end */
    g_fds[pid][wfd].type    = FD_PIPE;
    g_fds[pid][wfd].pipe_id = pipe_id;
    g_fds[pid][wfd].flags   = O_WRITE;
    g_fds[pid][wfd].active  = true;

    /* Store PipeTable for later read/write */
    g_fd_pipes = pipes;

    *read_fd  = rfd;
    *write_fd = wfd;
    return 0;
}

/* ── Read ────────────────────────────────────────────── */
int fd_read(void *ctx_v, uint32_t pid, int fd, uint8_t *buf, uint16_t len) {
    if (!buf || len == 0) return -1;

    if (fd == FD_STDIN) {
        if (!fgets((char *)buf, len, stdin)) return 0;
        return (int)strlen((char *)buf);
    }

    FileDesc *f = fd_get(pid, fd);
    if (!f) return -1;
    if (!(f->flags & O_READ)) return -1;

    if (f->type == FD_PIPE) {
        if (!g_fd_pipes) return 0;
        return pipe_read(g_fd_pipes, (EngineContext *)ctx_v,
                         f->pipe_id, buf, len);
    }

    /* FD_FILE: CanvasFS bridge */
    extern int fd_file_read_slot(void*, FileDesc*, uint8_t*, uint16_t);
    return fd_file_read_slot(ctx_v, f, buf, len);
}

/* ── Write ───────────────────────────────────────────── */
int fd_write(void *ctx_v, uint32_t pid, int fd, const uint8_t *data, uint16_t len) {
    if (!data || len == 0) return -1;

    if (fd == FD_STDOUT || fd == FD_STDERR) {
        for (uint16_t i = 0; i < len; i++) {
            putchar(data[i]);
            if (g_stdout_len < sizeof(g_stdout_buf))
                g_stdout_buf[g_stdout_len++] = data[i];
        }
        fflush(stdout);
        return len;
    }

    FileDesc *f = fd_get(pid, fd);
    if (!f) return -1;
    if (!(f->flags & O_WRITE)) return -1;

    if (f->type == FD_PIPE) {
        if (!g_fd_pipes) return 0;
        return pipe_write(g_fd_pipes, (EngineContext *)ctx_v,
                          f->pipe_id, data, len);
    }

    extern int fd_file_write_slot(void*, FileDesc*, const uint8_t*, uint16_t);
    return fd_file_write_slot(ctx_v, f, data, len);
}

int fd_close(void *ctx_v, uint32_t pid, int fd) {
    (void)ctx_v;
    if (fd < 3) return -1;
    FileDesc *f = fd_get(pid, fd);
    if (!f) return -1;

    /* If pipe, close the pipe end */
    if (f->type == FD_PIPE && g_fd_pipes) {
        pipe_close(g_fd_pipes, (EngineContext *)ctx_v, f->pipe_id);
    }

    memset(f, 0, sizeof(*f));
    return 0;
}

int fd_seek(void *ctx_v, uint32_t pid, int fd, uint16_t offset) {
    (void)ctx_v;
    FileDesc *f = fd_get(pid, fd);
    if (!f) return -1;
    f->cursor = offset;
    return 0;
}

int fd_dup(void *ctx_v, uint32_t pid, int old_fd, int new_fd) {
    (void)ctx_v;
    if (pid >= PROC8_MAX) return -1;
    if (old_fd < 0 || old_fd >= FD_MAX_PER_PROC) return -1;
    if (new_fd < 0 || new_fd >= FD_MAX_PER_PROC) return -1;
    g_fds[pid][new_fd] = g_fds[pid][old_fd];
    return 0;
}

uint16_t fd_stdout_get(uint8_t *buf, uint16_t max) {
    uint16_t n = g_stdout_len < max ? g_stdout_len : max;
    if (buf && n > 0) memcpy(buf, g_stdout_buf, n);
    return n;
}

void fd_stdout_clear(void) { g_stdout_len = 0; }

