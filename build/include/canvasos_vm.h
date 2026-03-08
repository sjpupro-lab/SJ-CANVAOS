#pragma once
/*
 * canvasos_vm.h — Phase-9: PixelCode VM
 *
 * 캔버스 셀이 곧 명령어. Y축 아래로 실행.
 * B 채널 = opcode, A = 주소, G = 상태, R = 데이터.
 */
#include <stdint.h>
#include <stdbool.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"

#define VM_CALL_STACK_DEPTH   32
#define VM_DEFAULT_TICK_LIMIT 1000000
#define CANVAS_SIZE           (CANVAS_W * CANVAS_H)

/* ── VM opcode (B 채널) ─────────────────────────────────── */
#define VM_NOP       0x00
#define VM_PRINT     0x01
#define VM_HALT      0x02
#define VM_SET       0x03
#define VM_COPY      0x04
#define VM_ADD       0x05
#define VM_SUB       0x06
#define VM_CMP       0x07
#define VM_JMP       0x08
#define VM_JZ        0x09
#define VM_JNZ       0x0A
#define VM_CALL      0x0B
#define VM_RET       0x0C
#define VM_LOAD      0x0D
#define VM_STORE     0x0E
#define VM_GATE_ON   0x10
#define VM_GATE_OFF  0x11
#define VM_SEND      0x20
#define VM_RECV      0x21
#define VM_SPAWN     0x30
#define VM_EXIT      0x31
#define VM_DRAW      0x40
#define VM_LINE      0x41
#define VM_RECT      0x42
#define VM_SYSCALL   0xFE
#define VM_BREAKPOINT 0xFF

/* ── WH opcodes (Phase-9) ──────────────────────────────── */
#define WH_OP_VM_START      0x90
#define WH_OP_VM_HALT       0x91
#define WH_OP_VM_STEP       0x92
#define WH_OP_VM_BREAKPOINT 0x93

/* ── VM State ──────────────────────────────────────────── */
typedef struct {
    uint32_t pc_x;
    uint32_t pc_y;
    uint32_t reg_A;
    uint8_t  reg_B;
    uint8_t  reg_G;
    uint8_t  reg_R;
    uint8_t  flag;       /* CMP 결과: 1=equal, 0=not equal */
    uint32_t sp;
    uint32_t call_stack[VM_CALL_STACK_DEPTH];
    bool     running;
    bool     trace;
    uint32_t pid;
    uint32_t tick_count;  /* 실행된 step 수 */
    uint32_t tick_limit;
    uint8_t  data_G;     /* LOAD 결과 보존용 */
    uint8_t  data_R;     /* LOAD 결과 보존용 */
} VmState;

/* ── API ───────────────────────────────────────────────── */
void vm_init(VmState *vm, uint32_t start_x, uint32_t start_y, uint32_t pid);
int  vm_step(EngineContext *ctx, VmState *vm);
int  vm_run(EngineContext *ctx, VmState *vm);
void vm_trace_set(VmState *vm, bool enable);

/* ── 셀에 명령어 심기 헬퍼 ──────────────────────────────── */
static inline void vm_plant(EngineContext *ctx,
                            uint32_t x, uint32_t y,
                            uint32_t a, uint8_t b, uint8_t g, uint8_t r) {
    uint32_t idx = y * CANVAS_W + x;
    if (idx < CANVAS_SIZE) {
        ctx->cells[idx].A = a;
        ctx->cells[idx].B = b;
        ctx->cells[idx].G = g;
        ctx->cells[idx].R = r;
    }
}
