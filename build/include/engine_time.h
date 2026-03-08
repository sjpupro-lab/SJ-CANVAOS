#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"

/* ===== WH/BH Geometry (Q3 Top) ===== */
#define Q3_X0 512
#define Q3_Y0 512
#define Q3_W  512

#define WH_TILES_Y 8
#define BH_TILES_Y 4

#define TILE_SZ 16

#define WH_X0 Q3_X0
#define WH_Y0 Q3_Y0
#define WH_W  Q3_W
#define WH_H  (WH_TILES_Y * TILE_SZ)

#define BH_X0 Q3_X0
#define BH_Y0 (Q3_Y0 + WH_H)
#define BH_W  Q3_W
#define BH_H  (BH_TILES_Y * TILE_SZ)

/* ===== WH Record ===== */
#define WH_RECORD_CELLS 2
#define WH_RECS_PER_ROW (WH_W / WH_RECORD_CELLS)
#define WH_CAP          (WH_RECS_PER_ROW * WH_H)

/* WH opcode indices live in B (Behavior Layer) */
typedef enum {
    WH_OP_NOP    = 0x00,
    WH_OP_TICK   = 0x01, /* heartbeat record */
    WH_OP_DECAY  = 0x02, /* energy decay */
    WH_OP_SLEEP  = 0x03, /* proc sleeping + gate close */
    WH_OP_WAKE   = 0x04, /* proc wake + gate open */
    WH_OP_KILL   = 0x05, /* proc kill */
    WH_OP_IPC    = 0x06, /* ipc stub event */
    WH_OP_EDIT_COMMIT = 0x20, /* sjterm/cell edit commit */
    WH_OP_IO_EVENT    = 0x40, /* nondet adapter captured event */
    WH_OP_GATE_OPEN  = 0x10, /* gate open tile */
    WH_OP_GATE_CLOSE  = 0x11,
    WH_OP_BH_SUMMARY  = 0x45, /* BH compress summary */ /* gate close tile */
} WhOpcode;

/* target kind stored in C1.B */
typedef enum {
    WH_TGT_NONE    = 0,
    WH_TGT_FS_SLOT = 1,
    WH_TGT_TILE    = 2,
    WH_TGT_CELL    = 3,
    WH_TGT_PROC    = 4,
} WhTargetKind;

/* 2-cell fixed record */
typedef struct {
    /* C0 */
    uint32_t tick_or_event;   /* A */
    uint8_t  opcode_index;    /* B */
    uint8_t  flags;           /* G */
    uint8_t  param0;          /* R */

    /* C1 */
    uint32_t target_addr;     /* A */
    uint8_t  target_kind;     /* B */
    uint8_t  arg_state;       /* G */
    uint8_t  param1;          /* R */
} WhRecord;

typedef struct { uint16_t x, y; } WhAddr;

WhAddr wh_addr_of_tick(uint64_t tick);
Cell*  wh_cell0(EngineContext* ctx, WhAddr a);
Cell*  wh_cell1(EngineContext* ctx, WhAddr a);

void   wh_write_record(EngineContext* ctx, uint64_t tick, const WhRecord* r);
bool   wh_read_record(EngineContext* ctx, uint64_t tick, WhRecord* out);

/* ===== BH: pid energy mapping ===== */
typedef struct { uint16_t x, y; } BhAddr;

BhAddr  bh_addr_of_pid(uint16_t pid);
uint8_t bh_get_energy(EngineContext* ctx, uint16_t pid);
void    bh_set_energy(EngineContext* ctx, uint16_t pid, uint8_t energy, uint8_t energy_max);
uint8_t bh_decay_energy(EngineContext* ctx, uint16_t pid, uint8_t dec);

/* Execute a WH opcode deterministically (minimal core). */
int wh_exec_record(EngineContext* ctx, const WhRecord* r);
