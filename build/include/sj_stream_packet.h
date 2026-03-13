#ifndef SJ_STREAM_PACKET_H
#define SJ_STREAM_PACKET_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SJSP_MAGIC 0x50534A53u /* "SJSP" little-endian */
#define SJSP_VERSION 0x0001u

#define SJSP_PACKET_SIZE   1024u
#define SJSP_HEADER_SIZE   64u
#define SJSP_PAYLOAD_SIZE  896u
#define SJSP_TAIL_SIZE     64u

#define SJSP_FLAG_EOF          0x0001u
#define SJSP_FLAG_PADDED       0x0002u
#define SJSP_FLAG_HAS_TOTAL    0x0004u
#define SJSP_FLAG_VISUALIZED   0x0008u
#define SJSP_FLAG_RESERVED0    0x0010u

typedef enum SjStreamLogicalType {
    SJSP_TYPE_RAW        = 0u,
    SJSP_TYPE_TEXT       = 1u,
    SJSP_TYPE_ASSET      = 2u,
    SJSP_TYPE_SJRAW_MAP  = 3u,
    SJSP_TYPE_USER0      = 1024u
} SjStreamLogicalType;

typedef struct SjStreamHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t flags;
    uint64_t stream_id;
    uint64_t packet_index;
    uint64_t total_packets_or_zero;
    uint32_t payload_size;
    uint32_t logical_type;
    uint64_t reorder_seed;
    uint64_t visual_slot;
    uint64_t header_crc64;
} SjStreamHeader;

typedef struct SjStreamTail {
    uint64_t payload_crc64;
    uint64_t packet_crc64;
    uint64_t prev_packet_hash;
    uint64_t next_hint;
    uint64_t visual_hash;
    uint64_t route_id;
    uint64_t reserved0;
    uint64_t reserved1;
} SjStreamTail;

typedef struct SjStreamPacket {
    SjStreamHeader header;
    uint8_t payload[SJSP_PAYLOAD_SIZE];
    SjStreamTail tail;
} SjStreamPacket;

typedef struct SjStreamEncodeOptions {
    uint64_t stream_id;
    uint32_t logical_type;
    uint64_t reorder_seed;
    uint64_t visual_slot_base;
    uint64_t route_id;
    int write_total_packets;
} SjStreamEncodeOptions;

typedef struct SjStreamDecodeInfo {
    uint64_t stream_id;
    uint64_t packets_seen;
    uint64_t bytes_written;
} SjStreamDecodeInfo;

void sjsp_packet_init(SjStreamPacket *pkt);
void sjsp_packet_finalize(SjStreamPacket *pkt);
int  sjsp_packet_validate(const SjStreamPacket *pkt);

void sjsp_packet_serialize(const SjStreamPacket *pkt, uint8_t out[SJSP_PACKET_SIZE]);
int  sjsp_packet_deserialize(SjStreamPacket *pkt, const uint8_t in[SJSP_PACKET_SIZE]);

uint64_t sjsp_crc64_ecma(const void *data, size_t size);
uint64_t sjsp_hash64_fnv1a(const void *data, size_t size);

int sjsp_write_packet(FILE *fp, const SjStreamPacket *pkt);
int sjsp_read_packet(FILE *fp, SjStreamPacket *pkt, int *is_eof);

int sjsp_encode_stream(FILE *in,
                       FILE *out,
                       const SjStreamEncodeOptions *opts,
                       uint64_t *out_packet_count,
                       uint64_t *out_total_bytes);

int sjsp_decode_stream(FILE *in,
                       FILE *out,
                       SjStreamDecodeInfo *info);

#ifdef __cplusplus
}
#endif

#endif
