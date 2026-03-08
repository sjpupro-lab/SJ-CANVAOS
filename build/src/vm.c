/*
 * vm.c — Phase-9: PixelCode VM
 *
 * Fetch-Decode-Execute on canvas cells.
 * B channel = opcode, Y axis = program flow.
 */
#include "../include/canvasos_vm.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvasos_syscall.h"
#include "../include/engine_time.h"
#include <stdio.h>
#include <string.h>

/* ── init ─────────────────────────────────────────────── */
void vm_init(VmState *vm, uint32_t start_x, uint32_t start_y, uint32_t pid) {
    memset(vm, 0, sizeof(*vm));
    vm->pc_x = start_x;
    vm->pc_y = start_y;
    vm->pid  = pid;
    vm->tick_limit = VM_DEFAULT_TICK_LIMIT;
}

void vm_trace_set(VmState *vm, bool enable) {
    vm->trace = enable;
}

/* ── WH logging ───────────────────────────────────────── */
static void vm_wh(EngineContext *ctx, uint8_t opcode, uint32_t pc_addr) {
    if (!ctx) return;
    WhRecord r;
    memset(&r, 0, sizeof(r));
    r.tick_or_event = ctx->tick;
    r.opcode_index  = opcode;
    r.target_addr   = pc_addr;
    r.target_kind   = WH_TGT_CELL;
    wh_write_record(ctx, ctx->tick, &r);
}

/* ── Bresenham line (integer only, DK-2) ─────────────── */
static void vm_draw_line(EngineContext *ctx,
                         uint32_t x0, uint32_t y0,
                         uint32_t x1, uint32_t y1, uint8_t color) {
    int32_t dx = (int32_t)x1 - (int32_t)x0;
    int32_t dy = (int32_t)y1 - (int32_t)y0;
    int32_t sx = dx > 0 ? 1 : -1;
    int32_t sy = dy > 0 ? 1 : -1;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    int32_t err = dx - dy;

    int32_t cx = (int32_t)x0, cy = (int32_t)y0;
    for (int steps = 0; steps < 2048; steps++) { /* safety limit */
        if (cx >= 0 && cx < (int32_t)CANVAS_W &&
            cy >= 0 && cy < (int32_t)CANVAS_H) {
            ctx->cells[(uint32_t)cy * CANVAS_W + (uint32_t)cx].R = color;
        }
        if (cx == (int32_t)x1 && cy == (int32_t)y1) break;
        int32_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 <  dx) { err += dx; cy += sy; }
    }
}

/* ── step: single Fetch-Decode-Execute ────────────────── */
int vm_step(EngineContext *ctx, VmState *vm) {
    if (!ctx || !vm) return -1;
    if (!vm->running) return -1;
    if (vm->tick_count >= vm->tick_limit) {
        vm->running = false;
        return -1;
    }

    /* bounds check */
    if (vm->pc_y >= CANVAS_H || vm->pc_x >= CANVAS_W) {
        vm->running = false;
        return -2;
    }

    /* 1. FETCH */
    uint32_t pc_idx = vm->pc_y * CANVAS_W + vm->pc_x;
    Cell *cell = &ctx->cells[pc_idx];

    /* 2. DECODE */
    vm->reg_A = cell->A;
    vm->reg_B = cell->B;
    vm->reg_G = cell->G;
    vm->reg_R = cell->R;

    if (vm->trace) {
        printf("  [VM] PC(%u,%u) A=%08X B=%02X G=%03u R=%02X('%c')\n",
               vm->pc_x, vm->pc_y,
               vm->reg_A, vm->reg_B, vm->reg_G, vm->reg_R,
               (vm->reg_R >= 0x20 && vm->reg_R < 0x7F) ? (char)vm->reg_R : '.');
    }

    vm->tick_count++;

    /* 3. EXECUTE — dispatch on B channel */
    switch (vm->reg_B) {

    case VM_NOP:
        break;

    case VM_PRINT: {
        /* Route through fd_write(FD_STDOUT) for capture */
        uint8_t ch = vm->reg_R;
        extern int fd_write(void*, uint32_t, int, const uint8_t*, uint16_t);
        fd_write(ctx, vm->pid, 1 /* FD_STDOUT */, &ch, 1);
        break;
    }

    case VM_HALT:
        vm->running = false;
        vm_wh(ctx, WH_OP_VM_HALT, pc_idx);
        return 0; /* don't advance PC */

    case VM_SET: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE) {
            ctx->cells[addr].G = vm->reg_G;
            ctx->cells[addr].R = vm->reg_R;
        }
        break;
    }

    case VM_COPY: {
        uint32_t src = vm->reg_A;
        uint32_t dst = src + (uint32_t)vm->reg_G;
        if (src < CANVAS_SIZE && dst < CANVAS_SIZE)
            ctx->cells[dst] = ctx->cells[src];
        break;
    }

    case VM_ADD: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE) {
            uint16_t sum = (uint16_t)ctx->cells[addr].G + vm->reg_R;
            ctx->cells[addr].G = (uint8_t)(sum > 255 ? 255 : sum);
        }
        break;
    }

    case VM_SUB: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE) {
            int16_t diff = (int16_t)ctx->cells[addr].G - vm->reg_R;
            ctx->cells[addr].G = (uint8_t)(diff < 0 ? 0 : diff);
        }
        break;
    }

    case VM_CMP: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE)
            vm->flag = (ctx->cells[addr].G == vm->reg_R) ? 1 : 0;
        else
            vm->flag = 0;
        break;
    }

    case VM_JMP: {
        vm->pc_y = vm->reg_A / CANVAS_W;
        vm->pc_x = vm->reg_A % CANVAS_W;
        return 0; /* manual PC, skip auto-increment */
    }

    case VM_JZ: {
        if (vm->flag == 1) { /* equal → jump */
            vm->pc_y = vm->reg_A / CANVAS_W;
            vm->pc_x = vm->reg_A % CANVAS_W;
            return 0;
        }
        break;
    }

    case VM_JNZ: {
        if (vm->flag == 0) { /* not equal → jump */
            vm->pc_y = vm->reg_A / CANVAS_W;
            vm->pc_x = vm->reg_A % CANVAS_W;
            return 0;
        }
        break;
    }

    case VM_CALL: {
        if (vm->sp >= VM_CALL_STACK_DEPTH) break;
        /* save next PC */
        vm->call_stack[vm->sp++] = (vm->pc_y + 1) * CANVAS_W + vm->pc_x;
        vm->pc_y = vm->reg_A / CANVAS_W;
        vm->pc_x = vm->reg_A % CANVAS_W;
        return 0;
    }

    case VM_RET: {
        if (vm->sp == 0) { vm->running = false; break; }
        uint32_t ret = vm->call_stack[--vm->sp];
        vm->pc_y = ret / CANVAS_W;
        vm->pc_x = ret % CANVAS_W;
        return 0;
    }

    case VM_LOAD: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE) {
            vm->data_G = ctx->cells[addr].G;
            vm->data_R = ctx->cells[addr].R;
        }
        break;
    }

    case VM_STORE: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE) {
            ctx->cells[addr].G = vm->data_G;
            ctx->cells[addr].R = vm->data_R;
        }
        break;
    }

    case VM_GATE_ON: {
        uint16_t tid = (uint16_t)(vm->reg_A & 0xFFFF);
        if (tid < TILE_COUNT)
            gate_open_tile(ctx, tid);
        break;
    }

    case VM_GATE_OFF: {
        uint16_t tid = (uint16_t)(vm->reg_A & 0xFFFF);
        if (tid < TILE_COUNT)
            gate_close_tile(ctx, tid);
        break;
    }

    case VM_SEND: {
        /* A=pipe_id (low 8 bits), R=byte to send */
        /* Use runtime bridge if available, else WH-only fallback */
        extern bool vm_bridge_is_active(void);
        extern int  vm_exec_send(void*, EngineContext*, VmState*);
        if (vm_bridge_is_active()) {
            vm_exec_send(NULL, ctx, vm);
        } else {
            WhRecord wr;
            memset(&wr, 0, sizeof(wr));
            wr.tick_or_event = ctx->tick;
            wr.opcode_index  = 0x73;
            wr.param0        = vm->reg_R;
            wr.target_addr   = vm->reg_A;
            wr.target_kind   = WH_TGT_FS_SLOT;
            wh_write_record(ctx, ctx->tick, &wr);
        }
        break;
    }

    case VM_RECV: {
        /* Use runtime bridge if available */
        extern bool vm_bridge_is_active(void);
        extern int  vm_exec_recv(void*, EngineContext*, VmState*);
        if (vm_bridge_is_active()) {
            vm_exec_recv(NULL, ctx, vm);
        } else {
            vm->reg_R = 0;
        }
        break;
    }

    case VM_SPAWN: {
        /* A=code_tile, G=energy — use bridge if available */
        extern bool vm_bridge_is_active(void);
        extern int  vm_exec_spawn(void*, EngineContext*, VmState*);
        if (vm_bridge_is_active()) {
            vm_exec_spawn(NULL, ctx, vm);
        } else {
            WhRecord wr;
            memset(&wr, 0, sizeof(wr));
            wr.tick_or_event = ctx->tick;
            wr.opcode_index  = 0x75;
            wr.param0        = vm->reg_G;
            wr.target_addr   = vm->reg_A;
            wh_write_record(ctx, ctx->tick, &wr);
        }
        break;
    }

    case VM_EXIT: {
        vm->running = false;
        vm_wh(ctx, WH_OP_VM_HALT, pc_idx);
        return 0;
    }

    case VM_DRAW: {
        uint32_t addr = vm->reg_A;
        if (addr < CANVAS_SIZE)
            ctx->cells[addr].R = vm->reg_R;
        break;
    }

    case VM_LINE: {
        /* A=start addr (packed), G=end Y*64+X offset, R=color */
        uint32_t x0 = vm->reg_A % CANVAS_W;
        uint32_t y0 = vm->reg_A / CANVAS_W;
        /* G encodes relative end: high nibble=dy, low nibble=dx */
        uint32_t x1 = x0 + (vm->reg_G & 0x0F);
        uint32_t y1 = y0 + (vm->reg_G >> 4);
        vm_draw_line(ctx, x0, y0, x1, y1, vm->reg_R);
        break;
    }

    case VM_RECT: {
        uint32_t x0 = vm->reg_A % CANVAS_W;
        uint32_t y0 = vm->reg_A / CANVAS_W;
        uint32_t w  = (vm->reg_G >> 4) + 1;
        uint32_t h  = (vm->reg_G & 0x0F) + 1;
        for (uint32_t dy = 0; dy < h && (y0+dy) < CANVAS_H; dy++)
            for (uint32_t dx = 0; dx < w && (x0+dx) < CANVAS_W; dx++)
                ctx->cells[(y0+dy) * CANVAS_W + (x0+dx)].R = vm->reg_R;
        break;
    }

    case VM_SYSCALL: {
        int result = syscall_dispatch(ctx, vm->pid, (uint8_t)(vm->reg_A & 0xFF),
                                      vm->reg_G, vm->reg_R, 0);
        vm->reg_R = (uint8_t)(result & 0xFF);
        break;
    }

    case VM_BREAKPOINT: {
        vm->running = false;
        vm_wh(ctx, WH_OP_VM_BREAKPOINT, pc_idx);
        return 0;
    }

    default:
        break;
    }

    /* 4. PC auto-increment (Y↓) */
    vm->pc_y++;
    if (vm->pc_y >= CANVAS_H) vm->running = false;
    return 0;
}

/* ── run: continuous execution ────────────────────────── */
int vm_run(EngineContext *ctx, VmState *vm) {
    vm->running = true;
    vm_wh(ctx, WH_OP_VM_START, vm->pc_y * CANVAS_W + vm->pc_x);
    while (vm->running) {
        if (vm_step(ctx, vm) != 0) break;
    }
    return 0;
}
