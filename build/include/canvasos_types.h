#pragma once
#include <stdint.h>
#include <stdbool.h>

/* ==========================================
 * CanvasOS Core Types — Phase 1
 * REF: SPEC_ControlRegion_Page_Bpage_v1
 * ========================================== */

enum { CANVAS_W = 1024, CANVAS_H = 1024 };
enum { ORIGIN_X = 512,  ORIGIN_Y = 512  };

/* ---- Cell: ABGR Contract ---- */
typedef struct {
    uint32_t A;   /* Spatial Address Layer (Where) */
    uint8_t  B;   /* Behavior Layer (opcode/type/bpage index) */
    uint8_t  G;   /* State Layer (flags/len/energy) */
    uint8_t  R;   /* Stream Layer (payload byte) */
    uint8_t  pad; /* reserved v1=0                         */
} Cell;

/* ---- A_word ---- */
/* LEGACY (Phase1): packed cond/op/arg helpers. New contract: opcode lives in B. */
static inline uint8_t  A_cond(uint32_t A) { return (uint8_t)(A >> 24); }
static inline uint16_t A_op  (uint32_t A) { return (uint16_t)((A >> 12) & 0x0FFF); }
static inline uint16_t A_arg0(uint32_t A) { return (uint16_t)(A & 0x0FFF); }
static inline uint32_t A_make(uint8_t cond, uint16_t op, uint16_t arg) {
    return ((uint32_t)cond << 24) | (((uint32_t)op & 0xFFF) << 12) | (arg & 0xFFF);
}

/* ---- Opcodes ---- */
typedef enum {
    OP_NOP      = 0x000,
    OP_PRINT    = 0x001,
    OP_HALT     = 0x002,
    OP_GATE_ON  = 0x010,
    OP_GATE_OFF = 0x011,
    OP_MKFILE   = 0x020,
    OP_ENERGY   = 0x030,
} Opcode;

/* ---- TileGate ---- */
enum { TILE = 16, TILES_X = 64, TILES_Y = 64, TILE_COUNT = 4096 };

typedef enum { GATE_CLOSE = 0, GATE_OPEN = 1 } GateState;
enum { TILEGATE_COUNT = TILE_COUNT };

static inline uint32_t tile_id_of_xy(uint16_t x, uint16_t y) {
    return (uint32_t)(y / TILE) * TILES_X + (uint32_t)(x / TILE);
}

/* ---- Scan Mode ---- */
typedef enum {
    SCAN_RING_MH = 0,
    SCAN_HYBRID  = 1,
    SCAN_SPIRAL  = 2,
} ScanMode;
static inline ScanMode activeset_mode(float d) {
    if (d < 0.02f) return SCAN_RING_MH;
    if (d < 0.30f) return SCAN_HYBRID;
    return SCAN_SPIRAL;
}

/* ---- ActiveSet ---- */
typedef struct {
    uint32_t open_count;
    float    density;
    ScanMode mode;
    uint8_t  open[TILE_COUNT];
} ActiveSet;

/* ---- EnergyConfig ---- */
typedef struct { uint8_t decay_rate; bool enabled; } EnergyConfig;

/* ==============================================
 * Control Region — SPEC §3
 * x ∈ [512..575], y ∈ [512..575]  (64×64 cells)
 * CR0(512,512) CR1(544,512) CR2(512,544) CR3(544,544)
 * ============================================== */
#define CR_X0  512
#define CR_Y0  512
#define CR_W   64
#define CR_H   64

/* CR0: SuperBlock + Registry (512..543, 512..543) */
#define CR0_X  512
#define CR0_Y  512
/* CR1: Branch Table         (544..575, 512..543) */
#define CR1_X  544
#define CR1_Y  512
/* CR2: PageSelector Table   (512..543, 544..575) */
#define CR2_X  512
#define CR2_Y  544
/* CR3: B-page Directory     (544..575, 544..575) */
#define CR3_X  544
#define CR3_Y  544

/* ---- SuperBlock (CR0, SPEC §5) ---- */
typedef struct __attribute__((packed)) {
    uint8_t  magic[8];        /* "CANVAOS1" */
    uint32_t spec_version;    /* e.g. 0x00010000 */
    uint32_t build_id;
    uint8_t  default_scan;    /* 0=RING_WAVEFRONT, 1=SPIRAL_STREAM */
    uint8_t  default_neighbor;/* 4=N4, 8=N8 */
    uint16_t default_bpage;   /* active bpage id */
    uint32_t default_branch;  /* active branch id */
    uint32_t control_crc32;   /* CRC32 of Control Region */
    uint8_t  reserved[28];
} SuperBlock;

/* ---- BranchCommit (CR1, SPEC §6) ---- */
typedef struct __attribute__((packed)) {
    uint32_t branch_id;
    uint32_t parent_id;       /* 0xFFFFFFFF = root */
    uint16_t gate_ref;
    uint16_t bpage_ref;
    uint16_t selector_base;
    uint16_t selector_cnt;
    uint32_t flags;
} BranchCommit;

/* ---- PageSelector (CR2, SPEC §7) ---- */
typedef struct __attribute__((packed)) {
    uint16_t x_min, x_max;
    uint16_t y_min, y_max;
    uint8_t  quadrant_mask;   /* bit0=Q0 bit1=Q1 bit2=Q2 bit3=Q3 */
    uint8_t  plane_mask;      /* bit0=A  bit1=B  bit2=G  bit3=R  */
    uint8_t  gate_policy;     /* 0=follow 1=force_open 2=force_close */
    uint8_t  reserved;
} PageSelector;

/* ---- BPageEntry (CR3, SPEC §8) ---- */
typedef struct __attribute__((packed)) {
    uint16_t bpage_id;
    uint8_t  storage_type;    /* 0=canvas-internal 1=cvp-section */
    uint8_t  microstep_size;  /* bytes per entry */
    uint16_t microstep_cnt;   /* must be 4096 */
    uint32_t data_ref;        /* pidx or CVP offset */
    uint32_t flags;
} BPageEntry;

/* Process → see include/canvasos_sched.h */