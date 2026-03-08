#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "canvasos_types.h"

/* =====================================================
 * CanvasFS v1.3 — VOLH/VOLT 2-tile volume
 *
 * 변경 사항 (v1.2 → v1.3):
 *   - VOLH (헤더 타일) + VOLT (슬롯 타일 256개) 분리
 *   - 슬롯 256개 전체 사용 가능 (헤더 충돌 없음)
 *   - FreeMap Reserved: Control Region(2080..2275) 자동 예약
 *   - LARGE meta_gate: slot.A[31:16] = meta, [15:0] = head_dat
 *   - Gate check: VOLH + VOLT 모두 열려야 접근 가능
 * ===================================================== */

typedef enum {
    FS_OK            =  0,
    FS_ERR_OOB       = -1,
    FS_ERR_NOTVOL    = -2,
    FS_ERR_NOSLOT    = -3,
    FS_ERR_NOMEM     = -4,
    FS_ERR_NOTDIR    = -5,
    FS_ERR_NOTFOUND  = -6,
    FS_ERR_EXISTS    = -7,
    FS_ERR_GATE      = -8,
    FS_ERR_NOTMETA   = -9,
} FsResult;

typedef enum {
    FS_SLOT_FREE  = 0,
    FS_SLOT_TINY  = 1,   /* ≤4 B   inline in slot.A          */
    FS_SLOT_SMALL = 2,   /* ≤224 B 1 DataTile                 */
    FS_SLOT_LARGE = 3,   /* >224 B chained + MetaTile real_len*/
} FsSlotClass;

/* FileKey: gate_id = VOLH tile */
typedef struct { uint16_t gate_id; uint8_t slot; } FsKey;

typedef struct {
    Cell        *canvas;
    uint32_t     canvas_size;
    uint16_t     freemap_gate;   /* FRE1 tile gate_id, 0xFFFF=unset */
    ActiveSet   *aset;           /* NULL = no gate check */
} CanvasFS;

/* ---- Adapter Chain ---- */
#define FS_CHAIN_MAX 8
typedef struct { uint16_t ids[FS_CHAIN_MAX]; uint8_t len; } FsBpageChain;

FsBpageChain bpchain_make(uint16_t id0);
FsBpageChain bpchain_push(FsBpageChain c, uint16_t id);
void       bpchain_encode(const FsBpageChain *c, uint16_t gate_id, uint8_t *buf, size_t len);
void       bpchain_decode(const FsBpageChain *c, uint16_t gate_id, uint8_t *buf, size_t len);

/* ---- Reserved gate range (Control Region) ---- */
/* tile_x 32..35, tile_y 32..35 → gate_id 2080..2275 */
#define FS_RESERVED_LO  2080u
#define FS_RESERVED_HI  2275u

/* ---- DirectoryBlock ---- */
#define FS_DIR_MAX_ENTRIES 56
#define FS_DIR_ENTRY_CELLS  4

/* ============================================================
 * API
 * ============================================================ */

void fs_init(CanvasFS *fs, Cell *canvas, uint32_t sz, ActiveSet *aset);

/* FreeMap */
FsResult fs_freemap_init(CanvasFS *fs, uint16_t fm_gate);
FsResult fs_freemap_alloc(CanvasFS *fs, uint16_t *out);
FsResult fs_freemap_free(CanvasFS *fs, uint16_t gate);

/* Volume: 2 tiles (VOLH + VOLT auto-allocated from FreeMap) */
FsResult fs_format_volume(CanvasFS *fs, uint16_t volh_gate,
                           uint16_t default_bpage);
FsResult fs_alloc_slot(CanvasFS *fs, uint16_t volh_gate, uint8_t *out_slot);
FsResult fs_free_slot(CanvasFS *fs, FsKey key);

/* bpage */
FsResult fs_set_bpage(CanvasFS *fs, uint16_t volh_gate, uint16_t bp);
FsResult fs_get_bpage(CanvasFS *fs, uint16_t volh_gate, uint16_t *out);
FsResult fs_set_bpage_chain(CanvasFS *fs, uint16_t volh_gate,
                             const FsBpageChain *chain);
/* per-slot override */
FsResult fs_slot_set_bpage(CanvasFS *fs, FsKey key, uint16_t bp);
FsResult fs_slot_get_bpage(CanvasFS *fs, FsKey key, uint16_t *out);

/* Core I/O */
FsResult fs_write(CanvasFS *fs, FsKey key, const uint8_t *data, size_t len);
FsResult fs_read(CanvasFS *fs, FsKey key, uint8_t *out, size_t cap,
                 size_t *out_len);
FsResult fs_stat(CanvasFS *fs, FsKey key, FsSlotClass *cls,
                 uint32_t *real_len);

/* MetaTile */
FsResult fs_meta_set_len(CanvasFS *fs, FsKey key, uint32_t v);
FsResult fs_meta_get_len(CanvasFS *fs, FsKey key, uint32_t *out);

/* Directory */
FsResult fs_mkdir(CanvasFS *fs, uint16_t dir_gate);
FsResult fs_dir_create(CanvasFS *fs, uint16_t dir_gate, const char *name,
                        uint16_t file_volh, FsKey *out_key);
FsResult fs_dir_open(CanvasFS *fs, uint16_t dir_gate, const char *name,
                      FsKey *out_key);
FsResult fs_dir_unlink(CanvasFS *fs, uint16_t dir_gate, const char *name);

typedef void (*FsLsCb)(int n, const char *name, FsKey key, void *u);
FsResult fs_dir_ls(CanvasFS *fs, uint16_t dir_gate, FsLsCb cb, void *u);
