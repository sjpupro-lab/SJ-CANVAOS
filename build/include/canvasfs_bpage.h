#pragma once
#include <stdint.h>
#include <stddef.h>

/* CanvasFS Format Adapter (B-page)
 *
 * bpage_id에 따라 byte stream을 가역 변환한다.
 * - encode: write 시 적용 (stored form)
 * - decode: read 시 적용 (presented form)
 *
 * 규칙은 결정론/가역이어야 한다.
 */

enum {
    FS_BPAGE_IDENTITY = 0,
    FS_BPAGE_XOR8     = 1,
    FS_BPAGE_NIBBLE   = 2,
    FS_BPAGE_ROTL1    = 3,
};

/* key는 bpage 규칙에 보조로 사용되는 u8 seed.
 * v1.2에서는 (uint8_t)(0x5A ^ (bpage_id & 0xFF) ^ (volume_gate_id & 0xFF)) 를 기본 키로 사용.
 */
uint8_t fs_bpage_default_key(uint16_t volume_gate_id, uint16_t bpage_id);

void fs_bpage_encode(uint16_t bpage_id, uint8_t key, uint8_t* buf, size_t len);
void fs_bpage_decode(uint16_t bpage_id, uint8_t key, uint8_t* buf, size_t len);
