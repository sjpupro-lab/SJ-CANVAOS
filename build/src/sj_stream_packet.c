#include "sj_stream_packet.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SJSP_HEADER_CORE_SIZE 56u /* header without header_crc64 */
#define SJSP_TAIL_PREFIX_SIZE 56u /* tail without reserved1 */

static void sjsp_store_u16le(uint8_t *dst, uint16_t v) {
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void sjsp_store_u32le(uint8_t *dst, uint32_t v) {
    dst[0] = (uint8_t)(v & 0xFFu);
    dst[1] = (uint8_t)((v >> 8) & 0xFFu);
    dst[2] = (uint8_t)((v >> 16) & 0xFFu);
    dst[3] = (uint8_t)((v >> 24) & 0xFFu);
}

static void sjsp_store_u64le(uint8_t *dst, uint64_t v) {
    for (size_t i = 0; i < 8; ++i) {
        dst[i] = (uint8_t)((v >> (8u * i)) & 0xFFu);
    }
}

static uint16_t sjsp_load_u16le(const uint8_t *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static uint32_t sjsp_load_u32le(const uint8_t *src) {
    return (uint32_t)src[0]
         | ((uint32_t)src[1] << 8)
         | ((uint32_t)src[2] << 16)
         | ((uint32_t)src[3] << 24);
}

static uint64_t sjsp_load_u64le(const uint8_t *src) {
    uint64_t v = 0;
    for (size_t i = 0; i < 8; ++i) {
        v |= ((uint64_t)src[i]) << (8u * i);
    }
    return v;
}

uint64_t sjsp_crc64_ecma(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t crc = 0ULL;
    const uint64_t poly = 0x42F0E1EBA9EA3693ULL;

    for (size_t i = 0; i < size; ++i) {
        crc ^= ((uint64_t)p[i]) << 56;
        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 0x8000000000000000ULL) {
                crc = (crc << 1) ^ poly;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint64_t sjsp_hash64_fnv1a(const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t h = 1469598103934665603ULL;
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void sjsp_packet_init(SjStreamPacket *pkt) {
    if (!pkt) return;
    memset(pkt, 0, sizeof(*pkt));
    pkt->header.magic = SJSP_MAGIC;
    pkt->header.version = SJSP_VERSION;
}

static void sjsp_header_to_bytes_with_crc(const SjStreamHeader *h, uint8_t out[SJSP_HEADER_SIZE], uint64_t header_crc) {
    memset(out, 0, SJSP_HEADER_SIZE);
    sjsp_store_u32le(out + 0, h->magic);
    sjsp_store_u16le(out + 4, h->version);
    sjsp_store_u16le(out + 6, h->flags);
    sjsp_store_u64le(out + 8, h->stream_id);
    sjsp_store_u64le(out + 16, h->packet_index);
    sjsp_store_u64le(out + 24, h->total_packets_or_zero);
    sjsp_store_u32le(out + 32, h->payload_size);
    sjsp_store_u32le(out + 36, h->logical_type);
    sjsp_store_u64le(out + 40, h->reorder_seed);
    sjsp_store_u64le(out + 48, h->visual_slot);
    sjsp_store_u64le(out + 56, header_crc);
}

static void sjsp_tail_to_bytes(const SjStreamTail *t, uint8_t out[SJSP_TAIL_SIZE]) {
    memset(out, 0, SJSP_TAIL_SIZE);
    sjsp_store_u64le(out + 0, t->payload_crc64);
    sjsp_store_u64le(out + 8, t->packet_crc64);
    sjsp_store_u64le(out + 16, t->prev_packet_hash);
    sjsp_store_u64le(out + 24, t->next_hint);
    sjsp_store_u64le(out + 32, t->visual_hash);
    sjsp_store_u64le(out + 40, t->route_id);
    sjsp_store_u64le(out + 48, t->reserved0);
    sjsp_store_u64le(out + 56, t->reserved1);
}

static void sjsp_header_from_bytes(SjStreamHeader *h, const uint8_t in[SJSP_HEADER_SIZE]) {
    h->magic = sjsp_load_u32le(in + 0);
    h->version = sjsp_load_u16le(in + 4);
    h->flags = sjsp_load_u16le(in + 6);
    h->stream_id = sjsp_load_u64le(in + 8);
    h->packet_index = sjsp_load_u64le(in + 16);
    h->total_packets_or_zero = sjsp_load_u64le(in + 24);
    h->payload_size = sjsp_load_u32le(in + 32);
    h->logical_type = sjsp_load_u32le(in + 36);
    h->reorder_seed = sjsp_load_u64le(in + 40);
    h->visual_slot = sjsp_load_u64le(in + 48);
    h->header_crc64 = sjsp_load_u64le(in + 56);
}

static void sjsp_tail_from_bytes(SjStreamTail *t, const uint8_t in[SJSP_TAIL_SIZE]) {
    t->payload_crc64 = sjsp_load_u64le(in + 0);
    t->packet_crc64 = sjsp_load_u64le(in + 8);
    t->prev_packet_hash = sjsp_load_u64le(in + 16);
    t->next_hint = sjsp_load_u64le(in + 24);
    t->visual_hash = sjsp_load_u64le(in + 32);
    t->route_id = sjsp_load_u64le(in + 40);
    t->reserved0 = sjsp_load_u64le(in + 48);
    t->reserved1 = sjsp_load_u64le(in + 56);
}

void sjsp_packet_finalize(SjStreamPacket *pkt) {
    uint8_t header_bytes[SJSP_HEADER_SIZE];
    uint8_t tail_bytes[SJSP_TAIL_SIZE];
    uint8_t packet_bytes[SJSP_PACKET_SIZE];

    if (!pkt) return;
    if (pkt->header.payload_size > SJSP_PAYLOAD_SIZE) {
        pkt->header.payload_size = SJSP_PAYLOAD_SIZE;
    }

    pkt->tail.payload_crc64 = sjsp_crc64_ecma(pkt->payload, SJSP_PAYLOAD_SIZE);
    pkt->tail.visual_hash = sjsp_hash64_fnv1a(pkt->payload, pkt->header.payload_size);

    sjsp_header_to_bytes_with_crc(&pkt->header, header_bytes, 0ULL);
    pkt->header.header_crc64 = sjsp_crc64_ecma(header_bytes, SJSP_HEADER_CORE_SIZE);
    sjsp_header_to_bytes_with_crc(&pkt->header, header_bytes, pkt->header.header_crc64);

    sjsp_tail_to_bytes(&pkt->tail, tail_bytes);
    memcpy(packet_bytes, header_bytes, SJSP_HEADER_SIZE);
    memcpy(packet_bytes + SJSP_HEADER_SIZE, pkt->payload, SJSP_PAYLOAD_SIZE);
    memcpy(packet_bytes + SJSP_HEADER_SIZE + SJSP_PAYLOAD_SIZE, tail_bytes, SJSP_TAIL_SIZE);

    /* packet crc excludes its own field */
    memset(packet_bytes + SJSP_HEADER_SIZE + SJSP_PAYLOAD_SIZE + 8, 0, 8);
    pkt->tail.packet_crc64 = sjsp_crc64_ecma(packet_bytes, SJSP_PACKET_SIZE);
}

int sjsp_packet_validate(const SjStreamPacket *pkt) {
    uint8_t header_bytes[SJSP_HEADER_SIZE];
    uint8_t tail_bytes[SJSP_TAIL_SIZE];
    uint8_t packet_bytes[SJSP_PACKET_SIZE];
    uint64_t expect_header_crc;
    uint64_t expect_payload_crc;
    uint64_t expect_packet_crc;

    if (!pkt) return -1;
    if (pkt->header.magic != SJSP_MAGIC) return -2;
    if (pkt->header.version != SJSP_VERSION) return -3;
    if (pkt->header.payload_size > SJSP_PAYLOAD_SIZE) return -4;

    sjsp_header_to_bytes_with_crc(&pkt->header, header_bytes, 0ULL);
    expect_header_crc = sjsp_crc64_ecma(header_bytes, SJSP_HEADER_CORE_SIZE);
    if (expect_header_crc != pkt->header.header_crc64) return -5;

    expect_payload_crc = sjsp_crc64_ecma(pkt->payload, SJSP_PAYLOAD_SIZE);
    if (expect_payload_crc != pkt->tail.payload_crc64) return -6;

    sjsp_header_to_bytes_with_crc(&pkt->header, header_bytes, pkt->header.header_crc64);
    sjsp_tail_to_bytes(&pkt->tail, tail_bytes);
    memcpy(packet_bytes, header_bytes, SJSP_HEADER_SIZE);
    memcpy(packet_bytes + SJSP_HEADER_SIZE, pkt->payload, SJSP_PAYLOAD_SIZE);
    memcpy(packet_bytes + SJSP_HEADER_SIZE + SJSP_PAYLOAD_SIZE, tail_bytes, SJSP_TAIL_SIZE);
    memset(packet_bytes + SJSP_HEADER_SIZE + SJSP_PAYLOAD_SIZE + 8, 0, 8);
    expect_packet_crc = sjsp_crc64_ecma(packet_bytes, SJSP_PACKET_SIZE);
    if (expect_packet_crc != pkt->tail.packet_crc64) return -7;

    return 0;
}

void sjsp_packet_serialize(const SjStreamPacket *pkt, uint8_t out[SJSP_PACKET_SIZE]) {
    uint8_t header_bytes[SJSP_HEADER_SIZE];
    uint8_t tail_bytes[SJSP_TAIL_SIZE];
    SjStreamPacket tmp;

    if (!pkt || !out) return;
    tmp = *pkt;
    sjsp_packet_finalize(&tmp);
    sjsp_header_to_bytes_with_crc(&tmp.header, header_bytes, tmp.header.header_crc64);
    sjsp_tail_to_bytes(&tmp.tail, tail_bytes);
    memcpy(out, header_bytes, SJSP_HEADER_SIZE);
    memcpy(out + SJSP_HEADER_SIZE, tmp.payload, SJSP_PAYLOAD_SIZE);
    memcpy(out + SJSP_HEADER_SIZE + SJSP_PAYLOAD_SIZE, tail_bytes, SJSP_TAIL_SIZE);
}

int sjsp_packet_deserialize(SjStreamPacket *pkt, const uint8_t in[SJSP_PACKET_SIZE]) {
    if (!pkt || !in) return -1;
    sjsp_header_from_bytes(&pkt->header, in);
    memcpy(pkt->payload, in + SJSP_HEADER_SIZE, SJSP_PAYLOAD_SIZE);
    sjsp_tail_from_bytes(&pkt->tail, in + SJSP_HEADER_SIZE + SJSP_PAYLOAD_SIZE);
    return sjsp_packet_validate(pkt);
}

int sjsp_write_packet(FILE *fp, const SjStreamPacket *pkt) {
    uint8_t raw[SJSP_PACKET_SIZE];
    if (!fp || !pkt) return -1;
    sjsp_packet_serialize(pkt, raw);
    return fwrite(raw, 1, SJSP_PACKET_SIZE, fp) == SJSP_PACKET_SIZE ? 0 : -1;
}

int sjsp_read_packet(FILE *fp, SjStreamPacket *pkt, int *is_eof) {
    uint8_t raw[SJSP_PACKET_SIZE];
    size_t n;

    if (!fp || !pkt) return -1;
    if (is_eof) *is_eof = 0;

    n = fread(raw, 1, SJSP_PACKET_SIZE, fp);
    if (n == 0) {
        if (is_eof) *is_eof = 1;
        return 0;
    }
    if (n != SJSP_PACKET_SIZE) return -2;
    return sjsp_packet_deserialize(pkt, raw);
}

static uint64_t sjsp_file_size(FILE *fp) {
    long cur = ftell(fp);
    long end;
    if (cur < 0) return 0;
    if (fseek(fp, 0L, SEEK_END) != 0) return 0;
    end = ftell(fp);
    if (end < 0) {
        (void)fseek(fp, cur, SEEK_SET);
        return 0;
    }
    (void)fseek(fp, cur, SEEK_SET);
    return (uint64_t)end;
}

int sjsp_encode_stream(FILE *in,
                       FILE *out,
                       const SjStreamEncodeOptions *opts,
                       uint64_t *out_packet_count,
                       uint64_t *out_total_bytes) {
    SjStreamPacket pkt;
    uint64_t packet_index = 0;
    uint64_t prev_hash = 0;
    uint64_t total_bytes = 0;
    uint64_t total_packets = 0;
    int can_write_total = 0;

    if (!in || !out || !opts) return -1;

    if (opts->write_total_packets) {
        uint64_t file_bytes = sjsp_file_size(in);
        if (file_bytes > 0 || !ferror(in)) {
            total_packets = (file_bytes + SJSP_PAYLOAD_SIZE - 1u) / SJSP_PAYLOAD_SIZE;
            can_write_total = 1;
            rewind(in);
        }
    }

    for (;;) {
        size_t got;
        sjsp_packet_init(&pkt);
        got = fread(pkt.payload, 1, SJSP_PAYLOAD_SIZE, in);
        if (got == 0) {
            if (feof(in)) break;
            return -2;
        }
        if (got < SJSP_PAYLOAD_SIZE) {
            memset(pkt.payload + got, 0, SJSP_PAYLOAD_SIZE - got);
        }

        ++packet_index;
        total_bytes += (uint64_t)got;

        pkt.header.flags = 0u;
        if (got < SJSP_PAYLOAD_SIZE) pkt.header.flags |= SJSP_FLAG_PADDED;
        if (can_write_total) pkt.header.flags |= SJSP_FLAG_HAS_TOTAL;

        pkt.header.stream_id = opts->stream_id;
        pkt.header.packet_index = packet_index;
        pkt.header.total_packets_or_zero = can_write_total ? total_packets : 0u;
        pkt.header.payload_size = (uint32_t)got;
        pkt.header.logical_type = opts->logical_type;
        pkt.header.reorder_seed = opts->reorder_seed;
        pkt.header.visual_slot = opts->visual_slot_base + (packet_index - 1u);

        pkt.tail.prev_packet_hash = prev_hash;
        pkt.tail.next_hint = packet_index + 1u;
        pkt.tail.route_id = opts->route_id;
        pkt.tail.reserved0 = 0u;
        pkt.tail.reserved1 = 0u;

        if (feof(in)) pkt.header.flags |= SJSP_FLAG_EOF;

        sjsp_packet_finalize(&pkt);
        if (sjsp_write_packet(out, &pkt) != 0) return -3;

        {
            uint8_t raw[SJSP_PACKET_SIZE];
            sjsp_packet_serialize(&pkt, raw);
            prev_hash = sjsp_hash64_fnv1a(raw, SJSP_PACKET_SIZE);
        }

        if (pkt.header.flags & SJSP_FLAG_EOF) break;
    }

    if (out_packet_count) *out_packet_count = packet_index;
    if (out_total_bytes) *out_total_bytes = total_bytes;
    return 0;
}

int sjsp_decode_stream(FILE *in,
                       FILE *out,
                       SjStreamDecodeInfo *info) {
    SjStreamPacket pkt;
    uint64_t expected_index = 1;
    uint64_t bytes_written = 0;
    uint64_t prev_hash = 0;
    uint64_t stream_id = 0;
    int eof = 0;

    if (!in || !out) return -1;
    if (info) memset(info, 0, sizeof(*info));

    while (!eof) {
        uint8_t raw[SJSP_PACKET_SIZE];
        size_t n = fread(raw, 1, SJSP_PACKET_SIZE, in);
        if (n == 0) break;
        if (n != SJSP_PACKET_SIZE) return -2;

        if (sjsp_packet_deserialize(&pkt, raw) != 0) return -3;
        if (pkt.header.packet_index != expected_index) return -4;
        if (expected_index == 1) {
            stream_id = pkt.header.stream_id;
        } else if (pkt.header.stream_id != stream_id) {
            return -5;
        }
        if (pkt.tail.prev_packet_hash != prev_hash) return -6;
        if (fwrite(pkt.payload, 1, pkt.header.payload_size, out) != pkt.header.payload_size) {
            return -7;
        }

        bytes_written += pkt.header.payload_size;
        prev_hash = sjsp_hash64_fnv1a(raw, SJSP_PACKET_SIZE);
        eof = ((pkt.header.flags & SJSP_FLAG_EOF) != 0);
        ++expected_index;
    }

    if (info) {
        info->stream_id = stream_id;
        info->packets_seen = expected_index - 1u;
        info->bytes_written = bytes_written;
    }
    return 0;
}
