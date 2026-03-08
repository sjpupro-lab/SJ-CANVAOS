#pragma once
/*
 * canvasos_mprotect.h — Phase-8: Tile Memory Protection
 */
#include <stdint.h>
#include "canvasos_types.h"
#include "canvasos_engine_ctx.h"

#define PERM_NONE    0x00
#define PERM_READ    0x01
#define PERM_WRITE   0x02
#define PERM_EXEC    0x04
#define PERM_SHARED  0x08
#define PERM_RWX     (PERM_READ | PERM_WRITE | PERM_EXEC)

#define TILE_OWNER_FREE  0xFFFF

#define WH_OP_MPROTECT  0x71
#define WH_OP_FAULT     0x72

typedef struct {
    uint16_t  owner[TILE_COUNT];    /* pid, TILE_OWNER_FREE=미할당 */
    uint8_t   perm[TILE_COUNT];     /* PERM_* 비트 조합 */
} TileProtection;

void tprot_init(TileProtection *tp);
int  tile_alloc(TileProtection *tp, EngineContext *ctx,
                uint16_t pid, uint16_t count);   /* returns start tile, -1=full */
void tile_free(TileProtection *tp, EngineContext *ctx,
               uint16_t pid, uint16_t start, uint16_t count);
int  tile_check(const TileProtection *tp, uint16_t pid,
                uint16_t tile_id, uint8_t perm);  /* 0=OK, -1=FAULT */
void tile_set_perm(TileProtection *tp, EngineContext *ctx,
                   uint16_t pid, uint16_t tile_id, uint8_t perm);
