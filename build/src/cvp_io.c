#include "../include/cvp_io.h"
#include "../include/engine_time.h"       /* WH/BH geometry + record helpers */
#include "../include/canvasos_gate_ops.h" /* gate_open_tile / gate_close_tile */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* =========================================================
 * CVP v1.1 — lock + TLV + CRC32
 *
 * File layout:
 *   CvpHeader (44 bytes, binary struct)
 *   TLV sections: [type:u16][len:u32][crc:u32][data...]
 *
 * Sections (all mandatory for a complete snapshot):
 *   1  CANVAS_BASE : full Cell dump (CANVAS_W*CANVAS_H*sizeof(Cell))
 *   2  GATE_STATE  : GateState[TILEGATE_COUNT]
 *   3  WH_LANE     : WH region Cell dump (replay source)
 *   4  BH_STATE    : BH region Cell dump (energy state)
 *
 * Lock fields in header are enforced on load/validate.
 * Pass CVP_LOCK_SKIP(0xFFFFFFFF) to skip a specific field.
 * ========================================================= */

enum {
    CVP_MAGIC   = 0x31505643u,  /* 'CVP1' little-endian */
    CVP_VERSION = 0x00010001u,  /* v1.1 */
};

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t engine_major;
    uint32_t engine_minor;
    uint32_t scan_policy;
    uint32_t bpage_version;
    uint32_t contract_hash;
    uint32_t wh_cap;
    uint32_t save_tick;
    uint32_t flags;
    uint32_t header_crc;   /* CRC32 of bytes 0..(sizeof-4) */
} CvpHeader;

typedef struct {
    uint16_t type;
    uint32_t len;
    uint32_t crc;
} __attribute__((packed)) CvpTlv;

enum {
    SEC_CANVAS_BASE = 1,
    SEC_GATE_STATE  = 2,
    SEC_WH_LANE     = 3,
    SEC_BH_STATE    = 4,
};

/* ---- CRC32 (ISO 3309 / zlib) ---- */
static uint32_t s_crc32_table[256];
static int      s_crc32_ready = 0;

static void crc32_init(void) {
    if (s_crc32_ready) return;
    s_crc32_ready = 1;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        s_crc32_table[i] = c;
    }
}

static uint32_t crc32_bytes(const void *data, size_t n) {
    crc32_init();
    uint32_t c = 0xFFFFFFFFu;
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < n; i++)
        c = s_crc32_table[(c ^ p[i]) & 0xFFu] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

const char *cvp_strerror(CvpStatus st) {
    switch (st) {
        case CVP_OK:          return "OK";
        case CVP_ERR_IO:      return "I/O error";
        case CVP_ERR_MAGIC:   return "bad magic";
        case CVP_ERR_VERSION: return "unsupported version";
        case CVP_ERR_LOCK:    return "determinism lock mismatch";
        case CVP_ERR_CRC:     return "CRC mismatch";
        case CVP_ERR_FORMAT:  return "format error";
        default:              return "unknown";
    }
}

/* ---- TLV write helper ---- */
static CvpStatus write_tlv(FILE *fp, uint16_t type,
                           const void *data, uint32_t len) {
    CvpTlv tlv;
    tlv.type = type;
    tlv.len  = len;
    tlv.crc  = crc32_bytes(data, (size_t)len);
    if (fwrite(&tlv, sizeof(tlv), 1, fp) != 1) return CVP_ERR_IO;
    if (len && fwrite(data, 1, (size_t)len, fp) != (size_t)len) return CVP_ERR_IO;
    return CVP_OK;
}

/* ---- TLV read helper (used in load loop) ---- */
static CvpStatus read_tlv(FILE *fp, CvpTlv *out,
                          uint8_t *buf, uint32_t cap) {
    size_t n = fread(out, 1, sizeof(*out), fp);
    if (n == 0 && feof(fp)) return CVP_ERR_IO;   /* EOF sentinel */
    if (n != sizeof(*out))  return CVP_ERR_IO;
    if (out->len > cap)     return CVP_ERR_FORMAT;
    if (out->len && fread(buf, 1, (size_t)out->len, fp) != (size_t)out->len)
        return CVP_ERR_IO;
    if (crc32_bytes(buf, (size_t)out->len) != out->crc)
        return CVP_ERR_CRC;
    return CVP_OK;
}

/* ---- minimal WH replay exec ---- */
static void wh_exec_min(EngineContext *ctx, const WhRecord *r) {
    switch (r->opcode_index) {
        case 0x10: {  /* WH_GATE_OPEN */
            uint16_t gid = (uint16_t)(r->target_addr & 0xFFFFu);
            gate_open_tile(ctx, gid);
        } break;
        case 0x11: {  /* WH_GATE_CLOSE */
            uint16_t gid = (uint16_t)(r->target_addr & 0xFFFFu);
            gate_close_tile(ctx, gid);
        } break;
        default:
            /* IPC, DECAY, SLEEP/WAKE/KILL: no side-effect in core replay.
             * Higher layers (Scheduler) interpret these from WH records. */
            break;
    }
}

/* ---- lock check helper ----
 * BUG-1 fix: use CVP_LOCK_SKIP(0xFFFFFFFF) as "don't care" sentinel.
 * Passing 0 now means "enforce hdr field == 0", not "skip".
 */
static bool lock_mismatch(uint32_t expected, uint32_t actual) {
    if (expected == CVP_LOCK_SKIP) return false;  /* skip this field */
    return expected != actual;
}

/* ========================================================= */

CvpStatus cvp_save_ctx(const EngineContext *ctx, const char *path,
                       uint32_t scan_policy, uint32_t bpage_version,
                       uint32_t contract_hash, uint32_t flags) {
    if (!ctx || !ctx->cells || !ctx->gates || !path) return CVP_ERR_IO;

    FILE *fp = fopen(path, "wb");
    if (!fp) return CVP_ERR_IO;

    /* build header */
    CvpHeader hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic         = CVP_MAGIC;
    hdr.version       = CVP_VERSION;
    hdr.engine_major  = 1;
    hdr.engine_minor  = 1;
    hdr.scan_policy   = scan_policy;
    hdr.bpage_version = bpage_version;
    hdr.contract_hash = contract_hash;
    hdr.wh_cap        = (uint32_t)WH_CAP;
    hdr.save_tick     = ctx->tick;
    hdr.flags         = flags;
    hdr.header_crc    = 0;
    hdr.header_crc    = crc32_bytes(&hdr, sizeof(hdr) - sizeof(uint32_t));

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) { fclose(fp); return CVP_ERR_IO; }

    /* SEC 1: CANVAS_BASE */
    uint32_t cell_bytes = ctx->cells_count * (uint32_t)sizeof(Cell);
    CvpStatus st = write_tlv(fp, SEC_CANVAS_BASE, ctx->cells, cell_bytes);
    if (st != CVP_OK) { fclose(fp); return st; }

    /* SEC 2: GATE_STATE */
    st = write_tlv(fp, SEC_GATE_STATE, ctx->gates,
                   (uint32_t)(TILEGATE_COUNT * sizeof(GateState)));
    if (st != CVP_OK) { fclose(fp); return st; }

    /* SEC 3: WH_LANE — dump WH region cells */
    uint32_t wh_bytes = (uint32_t)(WH_W * WH_H * sizeof(Cell));
    uint8_t *wh_buf   = (uint8_t *)malloc(wh_bytes);
    if (!wh_buf) { fclose(fp); return CVP_ERR_IO; }
    uint32_t k = 0;
    for (uint16_t y = WH_Y0; y < (uint16_t)(WH_Y0 + WH_H); y++) {
        for (uint16_t x = WH_X0; x < (uint16_t)(WH_X0 + WH_W); x++) {
            memcpy(&wh_buf[k], &ctx->cells[(uint32_t)y * CANVAS_W + x], sizeof(Cell));
            k += (uint32_t)sizeof(Cell);
        }
    }
    st = write_tlv(fp, SEC_WH_LANE, wh_buf, wh_bytes);
    free(wh_buf);
    if (st != CVP_OK) { fclose(fp); return st; }

    /* SEC 4: BH_STATE */
    uint32_t bh_bytes = (uint32_t)(BH_W * BH_H * sizeof(Cell));
    uint8_t *bh_buf   = (uint8_t *)malloc(bh_bytes);
    if (!bh_buf) { fclose(fp); return CVP_ERR_IO; }
    k = 0;
    for (uint16_t y = BH_Y0; y < (uint16_t)(BH_Y0 + BH_H); y++) {
        for (uint16_t x = BH_X0; x < (uint16_t)(BH_X0 + BH_W); x++) {
            memcpy(&bh_buf[k], &ctx->cells[(uint32_t)y * CANVAS_W + x], sizeof(Cell));
            k += (uint32_t)sizeof(Cell);
        }
    }
    st = write_tlv(fp, SEC_BH_STATE, bh_buf, bh_bytes);
    free(bh_buf);
    if (st != CVP_OK) { fclose(fp); return st; }

    fclose(fp);
    return CVP_OK;
}

/* ---- shared load/validate core ---- */
static CvpStatus do_load(EngineContext *ctx, const char *path,
                         bool dry_run, bool want_apply,
                         uint32_t exp_scan, uint32_t exp_bpage,
                         uint32_t exp_hash) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return CVP_ERR_IO;

    /* read + verify header */
    CvpHeader hdr;
    if (fread(&hdr, sizeof(hdr), 1, fp) != 1) { fclose(fp); return CVP_ERR_IO; }
    if (hdr.magic   != CVP_MAGIC)   { fclose(fp); return CVP_ERR_MAGIC; }
    if (hdr.version != CVP_VERSION) { fclose(fp); return CVP_ERR_VERSION; }

    uint32_t hc = crc32_bytes(&hdr, sizeof(hdr) - sizeof(uint32_t));
    if (hc != hdr.header_crc) { fclose(fp); return CVP_ERR_CRC; }

    /* BUG-1 fix: CVP_LOCK_SKIP skips field, 0 enforces exact 0 match */
    if (lock_mismatch(exp_scan,  hdr.scan_policy))   { fclose(fp); return CVP_ERR_LOCK; }
    if (lock_mismatch(exp_bpage, hdr.bpage_version))  { fclose(fp); return CVP_ERR_LOCK; }
    if (lock_mismatch(exp_hash,  hdr.contract_hash))  { fclose(fp); return CVP_ERR_LOCK; }
    if (hdr.wh_cap != (uint32_t)WH_CAP)              { fclose(fp); return CVP_ERR_LOCK; }

    /* allocate section buffer */
    uint32_t max_sec = (uint32_t)(CANVAS_W * CANVAS_H * sizeof(Cell));
    uint8_t *buf = (uint8_t *)malloc(max_sec);
    if (!buf) { fclose(fp); return CVP_ERR_IO; }

    int seen_canvas = 0, seen_gate = 0;
    CvpStatus st = CVP_OK;

    for (;;) {
        CvpTlv tlv;
        /* BUG-6 fix: use read_tlv helper consistently */
        CvpStatus rs = read_tlv(fp, &tlv, buf, max_sec);
        if (rs == CVP_ERR_IO && feof(fp)) break;   /* clean EOF */
        if (rs != CVP_OK) { st = rs; break; }

        if (!want_apply || dry_run) continue;

        switch (tlv.type) {
            case SEC_CANVAS_BASE: {
                if (tlv.len != ctx->cells_count * (uint32_t)sizeof(Cell)) {
                    st = CVP_ERR_FORMAT; break;
                }
                memcpy(ctx->cells, buf, tlv.len);
                seen_canvas = 1;
            } break;

            case SEC_GATE_STATE: {
                if (tlv.len != (uint32_t)(TILEGATE_COUNT * sizeof(GateState))) {
                    st = CVP_ERR_FORMAT; break;
                }
                memcpy(ctx->gates, buf, tlv.len);
                /* BUG-3 fix: sync active_open bitmap after gate load */
                if (ctx->active_open) {
                    for (uint32_t gi = 0; gi < TILE_COUNT; gi++)
                        ctx->active_open[gi] = (ctx->gates[gi] == GATE_OPEN) ? 1u : 0u;
                }
                seen_gate = 1;
            } break;

            case SEC_WH_LANE:
            case SEC_BH_STATE:
                /* CANVAS_BASE already includes these regions.
                 * Kept as separate sections for tooling/verification. */
                break;

            default:
                break;
        }
        if (st != CVP_OK) break;
    }

    free(buf);
    fclose(fp);
    if (st != CVP_OK) return st;
    if (want_apply && !dry_run) {
        if (!seen_canvas || !seen_gate) return CVP_ERR_FORMAT;
        ctx->tick = hdr.save_tick;  /* restore tick from header */
    }
    return CVP_OK;
}

CvpStatus cvp_load_ctx(EngineContext *ctx, const char *path,
                       bool dry_run,
                       uint32_t exp_scan, uint32_t exp_bpage, uint32_t exp_hash) {
    if (!ctx) return CVP_ERR_IO;
    return do_load(ctx, path, dry_run, true, exp_scan, exp_bpage, exp_hash);
}

CvpStatus cvp_validate(const char *path,
                       uint32_t exp_scan, uint32_t exp_bpage, uint32_t exp_hash) {
    return do_load(NULL, path, true, false, exp_scan, exp_bpage, exp_hash);
}

CvpStatus cvp_replay_ctx(EngineContext *ctx, const char *path,
                         uint32_t from_tick, uint32_t to_tick,
                         uint32_t exp_scan, uint32_t exp_bpage, uint32_t exp_hash) {
    /* BUG-2 fix: replay enforces locks (caller uses CVP_LOCK_SKIP to relax) */
    CvpStatus st = cvp_load_ctx(ctx, path, false, exp_scan, exp_bpage, exp_hash);
    if (st != CVP_OK) return st;

    /* clamp to retained WH window */
    uint32_t save_tick = ctx->tick;
    uint32_t window    = (save_tick > (uint32_t)WH_CAP)
                         ? (uint32_t)WH_CAP : save_tick;
    uint32_t min_tick  = save_tick - window;
    if (from_tick < min_tick) from_tick = min_tick;
    if (to_tick   > save_tick) to_tick  = save_tick;
    if (from_tick > to_tick) return CVP_OK;

    for (uint32_t t = from_tick; t <= to_tick; t++) {
        WhRecord r;
        wh_read_record(ctx, (uint64_t)t, &r);
        wh_exec_min(ctx, &r);
    }
    return CVP_OK;
}
