#pragma once
/*
 * CanvasOS Channel Contract (Immutable)
 *
 * A (u32): Spatial Address Layer (Where)   — pointers, links, space/page/volume ids
 * B (u8) : Behavior Layer (What)          — opcode/type/bpage/microstep index
 * G (u8) : State Layer (How it is)        — flags/len/version/energy-state
 * R (u8) : Stream Layer (Data body)       — payload stream bytes / Δ bytes
 *
 * 복합 의미(주소+명령+상태 혼합)는 금지.
 * 조합은 RuleTable(B-page)에서만 수행.
 */
#include <stdint.h>

static inline uint16_t lo16(uint32_t a) { return (uint16_t)(a & 0xFFFFu); }
static inline uint16_t hi16(uint32_t a) { return (uint16_t)((a >> 16) & 0xFFFFu); }
static inline uint32_t pack16(uint16_t lo, uint16_t hi) { return ((uint32_t)hi << 16) | (uint32_t)lo; }

/* Slot.A pointer contract */
static inline uint16_t slot_head_gate(uint32_t a_word) { return lo16(a_word); }
static inline uint16_t slot_meta_gate(uint32_t a_word) { return hi16(a_word); }
static inline uint32_t slot_pack_ptrs(uint16_t head_gate, uint16_t meta_gate) { return pack16(head_gate, meta_gate); }
