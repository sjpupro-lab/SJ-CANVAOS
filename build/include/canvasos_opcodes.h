#pragma once
#include <stdint.h>

/* Opcode classes (behavior families) */
typedef enum {
    OP_NONE   = 0,
    OP_GATE   = 1,   /* gate open/close */
    OP_TIME   = 2,   /* WH write, BH decay */
    OP_FS     = 3,   /* CanvasFS (future) */
    OP_IPC    = 4,   /* WH IPC relay */
    OP_CVP    = 5,   /* CVP I/O */
    OP_ENGINE = 6,   /* engctx lifecycle */
    OP_COMMIT = 7,   /* delta write / RuleTable (future) */
} OpClass;

/* Stable opcode codes (from SSOT) */
typedef enum {
#define X(NAME, CODE, CLASS, TAGS, KEYWORDS, DESC) OP_##NAME = (CODE),
#include "canvasos_opcodes.def"
#undef X
} OpcodeCode;

const char *opcode_name(uint8_t code);
const char *opcode_desc(uint8_t code);
OpClass     opcode_class(uint8_t code);
const char *opcode_tags(uint8_t code);
const char *opcode_keywords(uint8_t code);
