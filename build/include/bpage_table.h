#pragma once
/*
 * Bpage Table (Phase 6+)
 *
 * - B 값(opcode/rule/io-map)을 해석하는 유일한 테이블
 * - 결정론: 동일 B -> 동일 동작/규약
 */
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    BP_KIND_NOP   = 0,
    BP_KIND_OP    = 1,
    BP_KIND_RULE  = 2,
    BP_KIND_IOMAP = 3,
} BpageKind;

typedef struct {
    uint8_t  b;
    uint8_t  kind;  /* BpageKind */
    uint16_t arg;   /* policy id / rule id / dev id */
    uint32_t aux;   /* table index / slot id / function id */
} BpageEntry;

typedef struct {
    BpageEntry e[256];
} BpageTable;

void bpage_init_default(BpageTable* t);
const BpageEntry* bpage_resolve(const BpageTable* t, uint8_t b);

/* 디버그: kind → 문자열 */
const char* bpage_kind_name(BpageKind k);

/* 사용자 정의 엔트리 덮어쓰기 */
void bpage_set(BpageTable* t, uint8_t b,
               BpageKind kind, uint16_t arg, uint32_t aux);
