#pragma once
/* canvasos_syscall.h — Phase-8: System Call Table */
#include <stdint.h>
#include "canvasos_engine_ctx.h"

/* Process */
#define SYS_SPAWN       0x01
#define SYS_EXIT        0x02
#define SYS_WAIT        0x03
#define SYS_KILL        0x04
#define SYS_SIGNAL      0x05
/* File */
#define SYS_OPEN        0x10
#define SYS_READ        0x11
#define SYS_WRITE       0x12
#define SYS_CLOSE       0x13
#define SYS_SEEK        0x14
#define SYS_MKDIR       0x15
#define SYS_LS          0x16
#define SYS_RM          0x17
/* IPC */
#define SYS_PIPE        0x20
#define SYS_DUP         0x21
/* Gate */
#define SYS_GATE_OPEN   0x30
#define SYS_GATE_CLOSE  0x31
#define SYS_MPROTECT    0x32
/* Info */
#define SYS_TICK        0x40
#define SYS_GETPID      0x41
#define SYS_GETPPID     0x42
#define SYS_TIME        0x43
#define SYS_HASH        0x44
/* CanvasOS Special */
#define SYS_TIMEWARP    0x50
#define SYS_DET_MODE    0x51
#define SYS_SNAPSHOT    0x52

#define SYS_MAX         0x60

#define WH_OP_SYSCALL   0x80

typedef int (*SyscallHandler)(EngineContext *ctx, uint32_t pid,
                              uint32_t a0, uint32_t a1, uint32_t a2);

void syscall_init(void);
int  syscall_register(uint8_t nr, SyscallHandler handler);
int  syscall_dispatch(EngineContext *ctx, uint32_t pid, uint8_t nr,
                      uint32_t a0, uint32_t a1, uint32_t a2);
