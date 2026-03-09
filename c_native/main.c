/**
 * SJ Spatial Compression Engine — 테스트 + 벤치마크
 * 512×512 고정 이미지 밝기 누산 공간압축 검증
 */

#include "sj_spatial_compress.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ─── 벤치마크 헬퍼 ─── */

static double time_ms(clock_t start, clock_t end) {
    return (double)(end - start) / CLOCKS_PER_SEC * 1000.0;
}

static void run_benchmark(const char *name, const uint8_t *data, size_t len) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BENCHMARK: %s\n", name);
    printf("  Input: %zu bytes\n", len);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    SjSpatialCanvas sj;
    if (sj_init(&sj) != 0) {
        printf("  ERROR: init failed\n");
        return;
    }

    /* 인코딩 */
    clock_t t0 = clock();
    sj_encode(&sj, data, len);
    clock_t t1 = clock();

    /* 압축 패키지 */
    size_t bound = sj_compressed_size_bound(&sj);
    uint8_t *comp = (uint8_t *)malloc(bound);
    clock_t t2 = clock();
    size_t comp_len = sj_compress_full(&sj, comp, bound);
    clock_t t3 = clock();

    /* 복원 */
    uint8_t *restored = (uint8_t *)malloc(len + 256);
    size_t restored_len = len + 256;
    clock_t t4 = clock();
    int rc = sj_decompress_full(comp, comp_len, restored, &restored_len);
    clock_t t5 = clock();

    /* 검증 */
    int match = (rc == 0 && restored_len >= len &&
                 memcmp(restored, data, len) == 0);

    /* 통계 */
    SjCompressStats s = sj_stats(&sj);

    double ratio = (comp_len > 0) ? (double)len / (double)comp_len : 0;
    double saving = (len > 0) ? (1.0 - (double)comp_len / (double)len) * 100.0 : 0;

    printf("\n  ┌─ 압축 결과 ─────────────────────────────\n");
    printf("  │ 원본 크기:      %10zu bytes\n", len);
    printf("  │ 압축 크기:      %10zu bytes\n", comp_len);
    printf("  │ 압축률:         %10.2fx\n", ratio);
    printf("  │ 절감:           %9.1f%%\n", saving);
    printf("  ├─ 캔버스 상태 ────────────────────────────\n");
    printf("  │ 비제로 셀:      %10u / %u\n", s.canvas_nonzero, SJ_TOTAL_CELLS);
    printf("  │ 희소율:         %9.1f%%\n", s.canvas_sparsity * 100.0);
    printf("  │ 최대 밝기:      %10lld\n", (long long)s.canvas_max_brightness);
    printf("  ├─ WH/BH ─────────────────────────────────\n");
    printf("  │ WH 레코드:      %10u\n", s.wh_records);
    printf("  │ BH 요약:        %10u\n", s.bh_summaries);
    printf("  │ BH 틱 절감:     %10llu\n", (unsigned long long)s.bh_ticks_saved);
    printf("  ├─ 시간 ──────────────────────────────────\n");
    printf("  │ 인코딩:         %9.1f ms\n", time_ms(t0, t1));
    printf("  │ 패키징:         %9.1f ms\n", time_ms(t2, t3));
    printf("  │ 복원:           %9.1f ms\n", time_ms(t4, t5));
    printf("  ├─ 검증 ──────────────────────────────────\n");
    printf("  │ 복원 일치:      %10s\n", match ? "PASS" : "FAIL");
    printf("  └──────────────────────────────────────────\n");

    /* 가역성 검증 */
    printf("\n  [가역성 검증] 역누산 실행 중...\n");
    uint8_t *verify_buf = (uint8_t *)malloc(len + 256);
    size_t verify_len = len + 256;
    bool reversible = false;

    /* sj를 재초기화해서 가역성 테스트 */
    SjSpatialCanvas sj2;
    sj_init(&sj2);
    sj_encode(&sj2, data, len);

    sj_decode_verify(&sj2, verify_buf, &verify_len, &reversible);
    printf("  [가역성 검증] 캔버스 제로: %s\n",
           reversible ? "PASS (완전 가역)" : "FAIL");

    free(comp);
    free(restored);
    free(verify_buf);
    sj_free(&sj);
    sj_free(&sj2);
}

/* ─── 메인 ─── */

int main(void) {
    printf("══════════════════════════════════════════════════════\n");
    printf("  SJ Spatial Compression Engine v1.0 (C Native)\n");
    printf("  512x512 Single-Image Brightness Accumulation\n");
    printf("══════════════════════════════════════════════════════\n");

    /* 1. 텍스트 (반복 패턴) */
    {
        const char *text = "Hello, SJ Spatial Compression! ";
        size_t unit = strlen(text);
        size_t total = unit * 100;
        uint8_t *data = (uint8_t *)malloc(total);
        for (size_t i = 0; i < 100; i++)
            memcpy(data + i * unit, text, unit);
        run_benchmark("텍스트 (반복 패턴)", data, total);
        free(data);
    }

    /* 2. 랜덤 바이너리 */
    {
        size_t len = 3000;
        uint8_t *data = (uint8_t *)malloc(len);
        srand(42);
        for (size_t i = 0; i < len; i++)
            data[i] = (uint8_t)(rand() % 256);
        run_benchmark("랜덤 바이너리 (패턴 없음)", data, len);
        free(data);
    }

    /* 3. 구조화 데이터 */
    {
        size_t len = 256 * 12;
        uint8_t *data = (uint8_t *)malloc(len);
        for (size_t i = 0; i < len; i++)
            data[i] = (uint8_t)(i % 256);
        run_benchmark("구조화 (0-255 반복)", data, len);
        free(data);
    }

    /* 4. 단일 바이트 반복 (최상) */
    {
        size_t len = 5000;
        uint8_t *data = (uint8_t *)malloc(len);
        memset(data, 0x42, len);
        run_benchmark("단일 바이트 반복 (최상 케이스)", data, len);
        free(data);
    }

    /* 5. 고엔트로피 (최악) */
    {
        size_t len = 512;
        uint8_t *data = (uint8_t *)malloc(len);
        for (size_t i = 0; i < 256; i++) data[i] = (uint8_t)i;
        for (size_t i = 0; i < 256; i++) data[256 + i] = (uint8_t)(255 - i);
        run_benchmark("고엔트로피 (최악 케이스)", data, len);
        free(data);
    }

    printf("\n══════════════════════════════════════════════════════\n");
    printf("  모든 벤치마크 완료.\n");
    printf("══════════════════════════════════════════════════════\n");

    return 0;
}
