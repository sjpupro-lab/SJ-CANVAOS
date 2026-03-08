#pragma once
#include <stdint.h>
#include <stdbool.h>

#include "canvasos_engine_ctx.h"

/* =========================================================
 * Phase 4.1 — CVP I/O (Determinism Lock)
 *
 * CVP is a self-contained snapshot + replay capsule.
 * - Determinism is enforced by header locks (policy/version/hash).
 * - Replay source is WH lane (no external journal).
 * - Overflow policy: WH is circular (mod WH_CAP). Replay is valid
 *   only within the retained window.
 * ========================================================= */

/* ---- error codes ---- */
typedef enum {
    CVP_OK          =  0,
    CVP_ERR_IO      = -1,
    CVP_ERR_MAGIC   = -2,
    CVP_ERR_VERSION = -3,
    CVP_ERR_LOCK    = -4,
    CVP_ERR_CRC     = -5,
    CVP_ERR_FORMAT  = -6,
} CvpStatus;

/* ---- lock skip sentinel ----
 * Pass CVP_LOCK_SKIP as expected_xxx to skip enforcement of that field.
 * Needed because SCAN_RING_MH=0 would be indistinguishable from
 * "don't care" if we used 0 as the skip sentinel.
 *
 * Usage:
 *   cvp_validate(path, SCAN_RING_MH, CVP_LOCK_SKIP, CVP_LOCK_SKIP)
 *   → enforces scan_policy only, ignores bpage_version and contract_hash
 */
#define CVP_LOCK_SKIP  0xFFFFFFFFu

/* ---- compile-time contract hash ----
 * Fingerprint of the ABGR channel layout + reserved region + WH_CAP.
 * Update when canvas_contract.h ABI changes.
 * Format: 'A'<<24 | 'B'<<16 | 'G'<<8 | 'R' = 0x41424752
 */
#define CVP_CONTRACT_HASH_V1  0x41424752u   /* ABGR */

/* ---- API ---- */

/* Save EngineContext to .cvp file.
 * scan_policy   : ScanMode value (SCAN_RING_MH etc.)
 * bpage_version : RuleTable/SSOT version tag
 * contract_hash : ABGR ABI fingerprint (use CVP_CONTRACT_HASH_V1)
 * flags         : reserved, pass 0
 */
CvpStatus cvp_save_ctx(const EngineContext *ctx, const char *path,
                       uint32_t scan_policy, uint32_t bpage_version,
                       uint32_t contract_hash, uint32_t flags);

/* Load .cvp into EngineContext.
 * dry_run = true  → validate + CRC only, no memcpy into ctx
 * expected_xxx    → CVP_LOCK_SKIP to skip that field's enforcement
 */
CvpStatus cvp_load_ctx(EngineContext *ctx, const char *path,
                       bool dry_run,
                       uint32_t expected_scan_policy,
                       uint32_t expected_bpage_version,
                       uint32_t expected_contract_hash);

/* Validate file integrity + lock fields. ctx not needed. */
CvpStatus cvp_validate(const char *path,
                       uint32_t expected_scan_policy,
                       uint32_t expected_bpage_version,
                       uint32_t expected_contract_hash);

/* Replay WH records from .cvp into ctx.
 * Respects circular WH window: [save_tick-min(save_tick,WH_CAP), save_tick]
 * from_tick/to_tick are clamped to retained window automatically.
 * Lock fields are still enforced (pass CVP_LOCK_SKIP to skip).
 */
CvpStatus cvp_replay_ctx(EngineContext *ctx, const char *path,
                         uint32_t from_tick, uint32_t to_tick,
                         uint32_t expected_scan_policy,
                         uint32_t expected_bpage_version,
                         uint32_t expected_contract_hash);

/* Human-readable status string */
const char *cvp_strerror(CvpStatus st);
