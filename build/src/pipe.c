/* pipe.c — Phase-8: Pipe */
#include "../include/canvasos_pipe.h"
#include "../include/engine_time.h"
#include <string.h>

static uint8_t g_pipe_buf[PIPE_MAX][PIPE_BUF_SIZE];

static uint16_t pipe_used(const Pipe *p) {
    if (p->w_cursor >= p->r_cursor) return (uint16_t)(p->w_cursor - p->r_cursor);
    return (uint16_t)(p->capacity - (p->r_cursor - p->w_cursor));
}

static uint16_t pipe_free_space(const Pipe *p) {
    return (uint16_t)(p->capacity - 1u - pipe_used(p));
}

static void pipe_wh(EngineContext *ctx, uint8_t opcode, int pipe_id, uint16_t len) {
    if (!ctx) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index = opcode;
    r.target_addr = (uint32_t)pipe_id;
    r.target_kind = WH_TGT_FS_SLOT;
    r.param1 = (uint8_t)(len & 0xFFu);
    wh_write_record(ctx, ctx->tick, &r);
}

void pipe_table_init(PipeTable *pt) {
    memset(pt, 0, sizeof(*pt));
    memset(g_pipe_buf, 0, sizeof(g_pipe_buf));
}

int pipe_create(PipeTable *pt, EngineContext *ctx,
                uint32_t writer_pid, uint32_t reader_pid) {
    (void)ctx;
    if (!pt) return -1;
    for (int i = 0; i < PIPE_MAX; i++) {
        Pipe *p = &pt->pipes[i];
        if (p->active) continue;
        memset(p, 0, sizeof(*p));
        p->slot.gate_id = (uint16_t)i;
        p->slot.slot = 0;
        p->writer_pid = writer_pid;
        p->reader_pid = reader_pid;
        p->capacity = PIPE_BUF_SIZE;
        p->active = true;
        pt->count++;
        return i;
    }
    return -1;
}

int pipe_write(PipeTable *pt, EngineContext *ctx,
               int pipe_id, const uint8_t *data, uint16_t len) {
    if (!pt || pipe_id < 0 || pipe_id >= PIPE_MAX || !data) return -1;
    Pipe *p = &pt->pipes[pipe_id];
    if (!p->active || p->closed) return -1;

    uint16_t n = len;
    uint16_t free_space = pipe_free_space(p);
    if (n > free_space) n = free_space;
    for (uint16_t i = 0; i < n; i++) {
        g_pipe_buf[pipe_id][p->w_cursor] = data[i];
        p->w_cursor = (uint16_t)((p->w_cursor + 1u) % p->capacity);
    }
    pipe_wh(ctx, WH_OP_PIPE_WRITE, pipe_id, n);
    return (int)n;
}

int pipe_read(PipeTable *pt, EngineContext *ctx,
              int pipe_id, uint8_t *buf, uint16_t len) {
    if (!pt || pipe_id < 0 || pipe_id >= PIPE_MAX || !buf) return -1;
    Pipe *p = &pt->pipes[pipe_id];
    if (!p->active) return -1;

    uint16_t n = len;
    uint16_t used = pipe_used(p);
    if (n > used) n = used;
    for (uint16_t i = 0; i < n; i++) {
        buf[i] = g_pipe_buf[pipe_id][p->r_cursor];
        p->r_cursor = (uint16_t)((p->r_cursor + 1u) % p->capacity);
    }
    pipe_wh(ctx, WH_OP_PIPE_READ, pipe_id, n);
    return (int)n;
}

void pipe_close(PipeTable *pt, EngineContext *ctx, int pipe_id) {
    if (!pt || pipe_id < 0 || pipe_id >= PIPE_MAX) return;
    pt->pipes[pipe_id].closed = true;
    pipe_wh(ctx, WH_OP_PIPE_READ, pipe_id, 0);
}
