#include "../include/canvasos_opcodes.h"
#include <stddef.h>

/* ---- static tables from SSOT ---- */
typedef struct {
    uint8_t     code;
    OpClass     cls;
    const char *name;
    const char *tags;
    const char *keywords;
    const char *desc;
} OpcodeEntry;

static const OpcodeEntry s_table[] = {
#define X(NAME, CODE, CLASS, TAGS, KEYWORDS, DESC) \
    { (CODE), (CLASS), #NAME, (TAGS), (KEYWORDS), (DESC) },
#include "../include/canvasos_opcodes.def"
#undef X
    { 0, OP_NONE, NULL, NULL, NULL, NULL }
};

static const OpcodeEntry *find(uint8_t code) {
    for (int i = 0; s_table[i].name; i++)
        if (s_table[i].code == code) return &s_table[i];
    return NULL;
}

const char *opcode_name(uint8_t code) {
    const OpcodeEntry *e = find(code); return e ? e->name : "UNKNOWN";
}
const char *opcode_desc(uint8_t code) {
    const OpcodeEntry *e = find(code); return e ? e->desc : "";
}
OpClass opcode_class(uint8_t code) {
    const OpcodeEntry *e = find(code); return e ? e->cls : OP_NONE;
}
const char *opcode_tags(uint8_t code) {
    const OpcodeEntry *e = find(code); return e ? e->tags : "";
}
const char *opcode_keywords(uint8_t code) {
    const OpcodeEntry *e = find(code); return e ? e->keywords : "";
}
