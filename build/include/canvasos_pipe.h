#pragma once
/* canvasos_pipe.h — Phase-8: Pipe (R-channel stream) */
#include <stdint.h>
#include <stdbool.h>
#include "canvasfs.h"
#include "canvasos_engine_ctx.h"

#define PIPE_MAX       64
#define PIPE_BUF_SIZE  224    /* = SMALL 슬롯 payload */
#define WH_OP_PIPE_WRITE  0x73
#define WH_OP_PIPE_READ   0x74

typedef struct {
    FsKey     slot;
    uint32_t  writer_pid;
    uint32_t  reader_pid;
    uint16_t  w_cursor;
    uint16_t  r_cursor;
    uint16_t  capacity;
    bool      closed;
    bool      active;
} Pipe;

typedef struct {
    Pipe      pipes[PIPE_MAX];
    uint32_t  count;
} PipeTable;

void pipe_table_init(PipeTable *pt);
int  pipe_create(PipeTable *pt, EngineContext *ctx,
                 uint32_t writer_pid, uint32_t reader_pid);  /* → pipe_id */
int  pipe_write(PipeTable *pt, EngineContext *ctx,
                int pipe_id, const uint8_t *data, uint16_t len);
int  pipe_read(PipeTable *pt, EngineContext *ctx,
               int pipe_id, uint8_t *buf, uint16_t len);
void pipe_close(PipeTable *pt, EngineContext *ctx, int pipe_id);
