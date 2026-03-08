/*
 * bpage_table.c — B 채널 해석 테이블 (Phase 6)
 *
 * B 값 0x00..0xFF 를 4개 종류(NOP/OP/RULE/IOMAP)로 분류.
 * 결정론: 동일 B → 동일 동작 (테이블이 유일한 해석 소스).
 *
 * 배치 규칙 (SPEC §2 "B 채널 의미"):
 *   0x00        NOP
 *   0x01..0x0F  OP  (Phase 1~2 legacy opcodes)
 *   0x10..0x1F  OP  (Gate opcodes)
 *   0x20..0x2F  OP  (WH/Time opcodes)
 *   0x30..0x3F  OP  (IPC opcodes)
 *   0x40..0x4F  IOMAP (WH_IO_EVENT 영역 → inject 경로)
 *   0x50..0x5F  OP  (Engine lifecycle)
 *   0x60..0x6F  OP  (SJTerm edit opcodes)
 *   0x70..0xBF  RULE (사용자 정의 규칙 공간)
 *   0xC0..0xFF  IOMAP (디바이스 IO 매핑 공간)
 */
#include "../include/bpage_table.h"
#include <string.h>

void bpage_init_default(BpageTable* t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));

    for (int i = 0; i < 256; i++) {
        BpageEntry* e = &t->e[i];
        e->b   = (uint8_t)i;
        e->arg = 0;
        e->aux = 0;

        /* ── 기본 분류 ── */
        if (i == 0x00) {
            e->kind = BP_KIND_NOP;

        /* Phase 1~2 legacy OP */
        } else if (i >= 0x01 && i <= 0x0F) {
            e->kind = BP_KIND_OP;
            e->arg  = (uint16_t)i;  /* opcode_id = B 값 그대로 */

        /* Gate OP (0x10 GATE_OPEN, 0x11 GATE_CLOSE) */
        } else if (i >= 0x10 && i <= 0x1F) {
            e->kind = BP_KIND_OP;
            e->arg  = (uint16_t)i;

        /* WH/Time OP */
        } else if (i >= 0x20 && i <= 0x2F) {
            e->kind = BP_KIND_OP;
            e->arg  = (uint16_t)i;

        /* IPC OP */
        } else if (i >= 0x30 && i <= 0x3F) {
            e->kind = BP_KIND_OP;
            e->arg  = (uint16_t)i;

        /* IO_EVENT 영역 → IOMAP (inject 경로) */
        } else if (i >= 0x40 && i <= 0x4F) {
            e->kind = BP_KIND_IOMAP;
            e->arg  = (uint16_t)(i - 0x40);  /* dev_class index */
            e->aux  = 0;

        /* Engine lifecycle OP */
        } else if (i >= 0x50 && i <= 0x5F) {
            e->kind = BP_KIND_OP;
            e->arg  = (uint16_t)i;

        /* SJTerm edit opcodes */
        } else if (i >= 0x60 && i <= 0x6F) {
            e->kind = BP_KIND_OP;
            e->arg  = (uint16_t)i;

        /* 사용자 정의 규칙 공간 */
        } else if (i >= 0x70 && i <= 0xBF) {
            e->kind = BP_KIND_RULE;
            e->arg  = (uint16_t)(i - 0x70);  /* rule_id */
            e->aux  = 0;

        /* 디바이스 IO 매핑 공간 */
        } else if (i >= 0xC0) {
            e->kind = BP_KIND_IOMAP;
            e->arg  = (uint16_t)(i - 0xC0);  /* dev_map index */
            e->aux  = 0;
        }
    }

    /* ── 자주 쓰는 값 명시적 오버라이드 ── */

    /* B=0x01 PRINT — OP, arg=print */
    t->e[0x01].kind = BP_KIND_OP; t->e[0x01].arg = 0x001;

    /* B=0x02 HALT — OP, arg=halt */
    t->e[0x02].kind = BP_KIND_OP; t->e[0x02].arg = 0x002;

    /* B=0x10 WH_GATE_OPEN */
    t->e[0x10].kind = BP_KIND_OP; t->e[0x10].arg = 0x010;

    /* B=0x11 WH_GATE_CLOSE */
    t->e[0x11].kind = BP_KIND_OP; t->e[0x11].arg = 0x011;

    /* B=0x20 WH_WRITE */
    t->e[0x20].kind = BP_KIND_OP; t->e[0x20].arg = 0x020;

    /* B=0x21 BH_DECAY */
    t->e[0x21].kind = BP_KIND_OP; t->e[0x21].arg = 0x021;

    /* B=0x30 WH_IPC_SEND */
    t->e[0x30].kind = BP_KIND_OP; t->e[0x30].arg = 0x030;

    /* B=0x40 WH_IO_EVENT → IOMAP → inject 경로 */
    t->e[0x40].kind = BP_KIND_IOMAP; t->e[0x40].arg = 0; t->e[0x40].aux = 0;

    /* B=0x50 ENGCTX_TICK */
    t->e[0x50].kind = BP_KIND_OP; t->e[0x50].arg = 0x050;

    /* B=0x60 WH_EDIT_COMMIT (sjterm) */
    t->e[0x60].kind = BP_KIND_OP; t->e[0x60].arg = 0x060;
}

const BpageEntry* bpage_resolve(const BpageTable* t, uint8_t b) {
    if (!t) return NULL;
    return &t->e[b];
}

/* ── bpage_kind_name: 디버깅/출력용 ── */
const char* bpage_kind_name(BpageKind k) {
    switch (k) {
    case BP_KIND_NOP:   return "NOP";
    case BP_KIND_OP:    return "OP";
    case BP_KIND_RULE:  return "RULE";
    case BP_KIND_IOMAP: return "IOMAP";
    default:            return "?";
    }
}

/* ── bpage_set: 사용자 정의 엔트리 등록 ── */
void bpage_set(BpageTable* t, uint8_t b,
               BpageKind kind, uint16_t arg, uint32_t aux) {
    if (!t) return;
    t->e[b].b    = b;
    t->e[b].kind = (uint8_t)kind;
    t->e[b].arg  = arg;
    t->e[b].aux  = aux;
}
