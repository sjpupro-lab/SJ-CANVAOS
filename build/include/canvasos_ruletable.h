#pragma once
/*
 * RuleTable (B-page) — Behavior composition table.
 *
 * B is an index. Meaning is defined by RuleTable[B].
 * Must be deterministic and reversible (encode/decode 1:1).
 */
#include <stdint.h>

#define RULE_CHAIN_MAX 8

typedef enum {
    ADDR_NONE = 0,
    ADDR_SLOT = 1,
    ADDR_TILE = 2,
    ADDR_CELL = 3,
} AddrMode;

typedef enum {
    EXEC_NOP = 0,
    EXEC_FS  = 1,   /* CanvasFS op */
    EXEC_GATE= 2,   /* gate op */
    EXEC_COMMIT = 3,/* Δ-commit write */
} ExecMode;

typedef struct {
    uint8_t ids[RULE_CHAIN_MAX];
    uint8_t len;
} BpageChain;

typedef struct {
    AddrMode  addr_mode;        /* how A is interpreted (Where) */
    ExecMode  exec_mode;        /* what to do */
    uint8_t   state_policy;     /* how G affects execution */
    uint8_t   stream_policy;    /* how R stream is treated */
    BpageChain adapter_chain;   /* reversible format stack */
    uint8_t   flags;
} RuleEntry;

typedef struct {
    RuleEntry entry[256];
} RuleTable;
