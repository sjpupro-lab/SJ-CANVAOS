#pragma once
/* canvasos_fd.h — Phase-8: File Descriptor Abstraction */
#include <stdint.h>
#include "canvasfs.h"

#define FD_MAX_PER_PROC  16
#define FD_STDIN   0
#define FD_STDOUT  1
#define FD_STDERR  2

#define O_READ    0x01
#define O_WRITE   0x02
#define O_APPEND  0x04
#define O_CREATE  0x08

typedef enum { FD_NONE=0, FD_FILE, FD_PIPE, FD_DEVICE } FdType;

typedef struct {
    FsKey     key;
    uint16_t  cursor;
    uint8_t   flags;       /* O_READ | O_WRITE | ... */
    FdType    type;
    int       pipe_id;     /* FD_PIPE인 경우 PipeTable 인덱스 */
    bool      active;
} FileDesc;

/* 프로세스별 fd 테이블은 ProcTable에 임베드 */
/* FileDesc proc_fds[PROC8_MAX][FD_MAX_PER_PROC]; */

int  fd_open(void *ctx, uint32_t pid, const char *path, uint8_t flags);
int  fd_read(void *ctx, uint32_t pid, int fd, uint8_t *buf, uint16_t len);
int  fd_write(void *ctx, uint32_t pid, int fd, const uint8_t *data, uint16_t len);
int  fd_close(void *ctx, uint32_t pid, int fd);
int  fd_seek(void *ctx, uint32_t pid, int fd, uint16_t offset);
int  fd_dup(void *ctx, uint32_t pid, int old_fd, int new_fd);

/* Phase-10 추가 */
void     fd_table_init(void);
uint16_t fd_stdout_get(uint8_t *buf, uint16_t max);
void     fd_stdout_clear(void);

/* Patch-B/C 추가 */
void fd_bind_key(uint32_t pid, int fd, FsKey key);
void fd_set_pipe_table(void *pt);
int  fd_pipe_create(void *ctx, void *pipes,
                    uint32_t pid, int *read_fd, int *write_fd);
