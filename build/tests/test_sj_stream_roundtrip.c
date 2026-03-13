#include "sj_stream_packet.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int write_fixture(const char *path, size_t n) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    for (size_t i = 0; i < n; ++i) {
        unsigned char b = (unsigned char)((i * 37u + 11u) & 0xFFu);
        if (fwrite(&b, 1, 1, fp) != 1) {
            fclose(fp);
            return -1;
        }
    }
    fclose(fp);
    return 0;
}

static int files_equal(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb");
    FILE *fb = fopen(b, "rb");
    int ok = 1;
    int ca, cb;
    if (!fa || !fb) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 0;
    }
    for (;;) {
        ca = fgetc(fa);
        cb = fgetc(fb);
        if (ca != cb) { ok = 0; break; }
        if (ca == EOF) break;
    }
    fclose(fa);
    fclose(fb);
    return ok;
}

int main(void) {
    const char *input_path = "test_input.bin";
    const char *stream_path = "test_output.sjsp";
    const char *restore_path = "test_restored.bin";
    SjStreamEncodeOptions opts;
    SjStreamDecodeInfo info;
    FILE *in = NULL;
    FILE *out = NULL;
    FILE *stream = NULL;
    FILE *restored = NULL;
    uint64_t packet_count = 0;
    uint64_t total_bytes = 0;
    int rc;

    if (write_fixture(input_path, 5000u) != 0) {
        fprintf(stderr, "fixture write failed\n");
        return 1;
    }

    memset(&opts, 0, sizeof(opts));
    opts.stream_id = 0x1122334455667788ULL;
    opts.logical_type = SJSP_TYPE_RAW;
    opts.reorder_seed = 0xABCDEF0011223344ULL;
    opts.visual_slot_base = 100u;
    opts.route_id = 7u;
    opts.write_total_packets = 1;

    in = fopen(input_path, "rb");
    out = fopen(stream_path, "wb");
    if (!in || !out) {
        fprintf(stderr, "open failed\n");
        return 2;
    }

    rc = sjsp_encode_stream(in, out, &opts, &packet_count, &total_bytes);
    fclose(in);
    fclose(out);
    if (rc != 0) {
        fprintf(stderr, "encode failed: %d\n", rc);
        return 3;
    }

    stream = fopen(stream_path, "rb");
    restored = fopen(restore_path, "wb");
    if (!stream || !restored) {
        fprintf(stderr, "re-open failed\n");
        return 4;
    }

    rc = sjsp_decode_stream(stream, restored, &info);
    fclose(stream);
    fclose(restored);
    if (rc != 0) {
        fprintf(stderr, "decode failed: %d\n", rc);
        return 5;
    }

    if (!files_equal(input_path, restore_path)) {
        fprintf(stderr, "roundtrip mismatch\n");
        return 6;
    }

    if (packet_count == 0 || total_bytes != 5000u || info.bytes_written != 5000u) {
        fprintf(stderr, "unexpected counters\n");
        return 7;
    }

    remove(input_path);
    remove(stream_path);
    remove(restore_path);

    printf("OK packet_count=%llu bytes=%llu stream_id=%llu\n",
           (unsigned long long)packet_count,
           (unsigned long long)info.bytes_written,
           (unsigned long long)info.stream_id);
    return 0;
}
