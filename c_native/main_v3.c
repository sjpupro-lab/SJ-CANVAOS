/**
 * SJ Spatial V3 — 스트리밍 벤치마크
 *
 * 핵심 변경:
 *   누산값 = RG 경로 기록 → 리스트 → 정수 전환
 *   각 페이지의 밝기 히스토그램 = 경로(path)
 *   경로를 정렬된 (col, count) 리스트로 → 정수화 → 델타 압축
 *
 * GB급 데이터 스트리밍 테스트 포함.
 */

#include "sj_spatial_v3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define WH_FILE  "/home/sj_spatial_compress/test_data/wh_stream.bin"
#define DLT_FILE "/home/sj_spatial_compress/test_data/delta_map.bin"

static double time_sec(clock_t s, clock_t e) {
    return (double)(e - s) / CLOCKS_PER_SEC;
}

typedef void (*gen_fn)(uint8_t *buf, size_t len, uint64_t offset);

static void gen_text(uint8_t *buf, size_t len, uint64_t offset) {
    const char *phrase = "CanvasOS SJ Spatial V3 streaming compression. ";
    size_t plen = strlen(phrase);
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)phrase[(offset + i) % plen];
}

static void gen_binary(uint8_t *buf, size_t len, uint64_t offset) {
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(((offset + i) * 7 + 0x55) & 0xFF);
}

static void gen_random(uint8_t *buf, size_t len, uint64_t offset) {
    (void)offset;
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(rand() % 256);
}

static void gen_mono(uint8_t *buf, size_t len, uint64_t offset) {
    (void)offset;
    memset(buf, 0x42, len);
}

static void run_streaming_bench(const char *name, uint64_t total_bytes, gen_fn gen) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BENCHMARK: %s\n", name);
    printf("  Input: %llu bytes (%.2f MB, %.2f GB)\n",
           (unsigned long long)total_bytes,
           (double)total_bytes / 1048576.0,
           (double)total_bytes / (1024.0 * 1048576.0));
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    V3Encoder enc;
    if (v3_enc_init(&enc, WH_FILE) != 0) {
        printf("  ERROR: encoder init failed\n");
        return;
    }

    /* ── 스트리밍 인코딩 ── */
    #define STREAM_BUF (64 * 1024)  /* 64KB 청크 */
    uint8_t *chunk = (uint8_t *)malloc(STREAM_BUF);
    uint64_t fed = 0;

    clock_t t0 = clock();
    while (fed < total_bytes) {
        size_t this_chunk = STREAM_BUF;
        if (fed + this_chunk > total_bytes)
            this_chunk = (size_t)(total_bytes - fed);

        gen(chunk, this_chunk, fed);
        v3_enc_feed(&enc, chunk, this_chunk);
        fed += this_chunk;
    }
    v3_enc_finish(&enc);
    clock_t t1 = clock();

    /* ── 델타 압축 출력 ── */
    clock_t t2 = clock();
    size_t delta_size = v3_enc_write_delta(&enc, DLT_FILE);
    clock_t t3 = clock();

    /* ── 통계 ── */
    V3Stats s = v3_stats(&enc, WH_FILE, DLT_FILE);
    v3_print_stats(&s);

    printf("  ┌─ 성능 ──────────────────────────────────────\n");
    double enc_sec = time_sec(t0, t1);
    double dlt_sec = time_sec(t2, t3);
    double throughput = (enc_sec > 0)
        ? (double)total_bytes / enc_sec / 1048576.0 : 0;
    printf("  │ 인코딩:           %11.3f s\n", enc_sec);
    printf("  │ 델타 출력:        %11.3f s\n", dlt_sec);
    printf("  │ 처리량:           %10.1f MB/s\n", throughput);
    printf("  └──────────────────────────────────────────────\n");

    /* ── 스트리밍 복원 검증 ── */
    printf("\n  [복원 검증] 스트리밍 디코딩...\n");
    v3_enc_free(&enc);  /* WH 파일 닫기 */

    V3Decoder dec;
    if (v3_dec_init(&dec, WH_FILE, total_bytes) == 0) {
        /* 첫 64KB만 검증 */
        size_t verify_len = (total_bytes < STREAM_BUF) ? (size_t)total_bytes : STREAM_BUF;
        uint8_t *verify_buf = (uint8_t *)malloc(verify_len);
        uint8_t *expect_buf = (uint8_t *)malloc(verify_len);
        size_t actual = 0;

        v3_dec_read(&dec, verify_buf, verify_len, &actual);
        gen(expect_buf, verify_len, 0);

        int match = (actual == verify_len && memcmp(verify_buf, expect_buf, verify_len) == 0);
        printf("  [복원 검증] 첫 %zuKB: %s\n", verify_len / 1024,
               match ? "PASS" : "FAIL");

        free(verify_buf);
        free(expect_buf);
        v3_dec_free(&dec);
    }

    /* ── 경로 리스트 → 정수 변환 예시 출력 ── */
    printf("\n  [경로 정수화] 페이지 0 R존 경로:\n");
    printf("  │ col:count → ");
    int shown = 0;
    for (uint32_t col = 0; col < V3_COLS && shown < 12; col++) {
        if (enc.canvas.brightness_r[0][col] > 0) {
            printf("(%u:%llu) ", col,
                   (unsigned long long)enc.canvas.brightness_r[0][col]);
            shown++;
        }
    }
    if (shown == 0) printf("(empty)");
    printf("\n");

    /* 경로를 정수로: 활성 col 리스트의 누산값 합 = 경로 정수 */
    uint64_t path_int = 0;
    for (uint32_t col = 0; col < V3_COLS; col++)
        path_int += enc.canvas.brightness_r[0][col] * (col + 1);
    printf("  │ 경로 정수 (가중합): %llu\n", (unsigned long long)path_int);
    printf("  │ 최대밝기 대비 델타: max(%llu) - int(%llu) = %lld\n",
           (unsigned long long)enc.canvas.max_brightness,
           (unsigned long long)path_int,
           (long long)(enc.canvas.max_brightness - path_int));

    free(chunk);
    /* 삭제하지 않음: WH/delta 파일은 테스트 데이터로 보존 */
}

int main(void) {
    srand(42);

    printf("══════════════════════════════════════════════════════════════════\n");
    printf("  SJ Spatial Compression Engine V3 (Streaming)\n");
    printf("  512×512 Fixed Canvas | B-Page RG Walking | Delta Compress\n");
    printf("  B[0-63]=R(25%%) B[64-127]=G(25%%) B[128-255]=A(50%%)\n");
    printf("  누산값 = RG경로 → 리스트 → 정수 → 델타 (최대밝기 기준)\n");
    printf("══════════════════════════════════════════════════════════════════\n");

    /* 1. 1KB (소형 기준선) */
    run_streaming_bench("1KB 텍스트 (기준선)",
                        1024, gen_text);

    /* 2. 4MB 텍스트 (임계점) */
    run_streaming_bench("4MB 텍스트 (임계점)",
                        4ULL * 1024 * 1024, gen_text);

    /* 3. 8MB 바이너리 */
    run_streaming_bench("8MB 바이너리 (OS 실행파일 유사)",
                        8ULL * 1024 * 1024, gen_binary);

    /* 4. 16MB 텍스트 */
    run_streaming_bench("16MB 텍스트 (스케일)",
                        16ULL * 1024 * 1024, gen_text);

    /* 5. 64MB 랜덤 */
    run_streaming_bench("64MB 랜덤 (스트레스)",
                        64ULL * 1024 * 1024, gen_random);

    /* 6. 128MB 텍스트 */
    run_streaming_bench("128MB 텍스트 (대규모)",
                        128ULL * 1024 * 1024, gen_text);

    /* 7. 256MB 단일바이트 (최상) */
    run_streaming_bench("256MB 단일바이트 (최상)",
                        256ULL * 1024 * 1024, gen_mono);

    /* 8. 512MB 바이너리 */
    run_streaming_bench("512MB 바이너리 (반GB)",
                        512ULL * 1024 * 1024, gen_binary);

    /* 9. 1GB 텍스트 */
    run_streaming_bench("1GB 텍스트 (GB 스케일)",
                        1024ULL * 1024 * 1024, gen_text);

    printf("\n══════════════════════════════════════════════════════════════════\n");
    printf("  완료. 핵심:\n");
    printf("  - 캔버스 RAM 고정 (~3MB) → GB 데이터도 메모리 일정\n");
    printf("  - 파일 클수록 → 밝기 높아짐 → 델타 효율 상승\n");
    printf("  - 누산값 = 경로 리스트 → 정수 → 최대밝기 델타\n");
    printf("══════════════════════════════════════════════════════════════════\n");

    return 0;
}
