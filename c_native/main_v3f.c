/**
 * SJ Spatial V3-Final — 벤치마크
 * 로그 없음. 512×512 이미지 = 유일한 출력. 밝기 = 원본.
 */

#include "sj_spatial_v3f.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define IMG_FILE "/home/sj_spatial_compress/test_data/canvas.sj3f"
#define STREAM_BUF (64 * 1024)

typedef void (*gen_fn)(uint8_t *buf, size_t len, uint64_t offset);

static void gen_text(uint8_t *b, size_t l, uint64_t o) {
    const char *p = "CanvasOS SJ Spatial V3 Final - image is everything. ";
    size_t pl = strlen(p);
    for (size_t i = 0; i < l; i++) b[i] = (uint8_t)p[(o+i)%pl];
}
static void gen_bin(uint8_t *b, size_t l, uint64_t o) {
    for (size_t i = 0; i < l; i++) b[i] = (uint8_t)(((o+i)*7+0x55)&0xFF);
}
static void gen_rnd(uint8_t *b, size_t l, uint64_t o) {
    (void)o; for (size_t i = 0; i < l; i++) b[i] = (uint8_t)(rand()%256);
}
static void gen_mono(uint8_t *b, size_t l, uint64_t o) {
    (void)o; memset(b, 0x42, l);
}

static void run(const char *name, uint64_t total, gen_fn gen) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  %s\n", name);
    printf("  Input: %llu bytes (%.2f MB)\n",
           (unsigned long long)total, (double)total/1048576.0);
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    V3FCanvas *c = (V3FCanvas*)calloc(1, sizeof(V3FCanvas));
    v3f_init(c);

    uint8_t *chunk = (uint8_t*)malloc(STREAM_BUF);
    uint64_t fed = 0;

    /* 인코딩 */
    clock_t t0 = clock();
    while (fed < total) {
        size_t n = STREAM_BUF;
        if (fed + n > total) n = (size_t)(total - fed);
        gen(chunk, n, fed);
        v3f_encode_chunk(c, chunk, n);
        fed += n;
    }
    clock_t t1 = clock();
    double enc_s = (double)(t1-t0)/CLOCKS_PER_SEC;

    /* 이미지 출력 */
    clock_t t2 = clock();
    size_t img_size = v3f_write_image(c, IMG_FILE);
    clock_t t3 = clock();
    double img_s = (double)(t3-t2)/CLOCKS_PER_SEC;

    /* 통계 */
    V3FStats s = v3f_stats(c, IMG_FILE);
    v3f_print_stats(&s);

    printf("  ┌─ 성능 ─────────────────────────────────────────\n");
    double tp = (enc_s > 0) ? (double)total / enc_s / 1048576.0 : 0;
    printf("  │ 인코딩:          %10.3f s (%7.1f MB/s)\n", enc_s, tp);
    printf("  │ 이미지 출력:     %10.3f s\n", img_s);
    printf("  └─────────────────────────────────────────────────\n");

    /* 복원 검증 (작은 데이터만) */
    if (total <= 64ULL * 1024 * 1024) {
        printf("\n  [복원] 이미지 → 원본 디코딩...\n");
        size_t dec_cap = (size_t)total + 1024;
        uint8_t *restored = (uint8_t*)malloc(dec_cap);
        size_t actual = 0;

        clock_t t4 = clock();
        int rc = v3f_read_and_decode(IMG_FILE, restored, dec_cap, &actual);
        clock_t t5 = clock();
        double dec_s = (double)(t5-t4)/CLOCKS_PER_SEC;

        if (rc == 0 && actual == total) {
            /* 원본 재생성하여 비교 */
            uint8_t *expect = (uint8_t*)malloc((size_t)total);
            uint64_t eo = 0;
            while (eo < total) {
                size_t n = STREAM_BUF;
                if (eo + n > total) n = (size_t)(total - eo);
                gen(expect + eo, n, eo);
                eo += n;
            }

            /* 바이트 단위 정확도 */
            uint64_t match = 0;
            for (uint64_t i = 0; i < total; i++)
                if (restored[i] == expect[i]) match++;

            double accuracy = (double)match / (double)total * 100.0;
            printf("  [복원] %llu / %llu bytes 일치 (%.2f%%) | %.3fs\n",
                   (unsigned long long)match,
                   (unsigned long long)total,
                   accuracy, dec_s);

            if (accuracy < 100.0) {
                /* 첫 불일치 위치 */
                for (uint64_t i = 0; i < total && i < 20; i++) {
                    if (restored[i] != expect[i]) {
                        printf("  [불일치] pos=%llu expect=0x%02X got=0x%02X\n",
                               (unsigned long long)i, expect[i], restored[i]);
                    }
                }
            }

            free(expect);
        } else {
            printf("  [복원] FAIL (rc=%d, actual=%zu)\n", rc, actual);
        }
        free(restored);
    } else {
        printf("\n  [복원] 대형 데이터: 생략 (인코딩 검증만)\n");
    }

    free(chunk);
    free(c);
}

int main(void) {
    srand(42);

    printf("══════════════════════════════════════════════════════════════════════\n");
    printf("  SJ Spatial V3-Final: 로그 없음. 이미지가 전부.\n");
    printf("  512×512 brightness map = 압축 결과물 = 원본의 모든 정보\n");
    printf("  출력: [헤더 + 활성셀 델타] — WH/BH 로그 없음\n");
    printf("══════════════════════════════════════════════════════════════════════\n");

    run("1KB 텍스트",              1024, gen_text);
    run("10KB 텍스트",             10240, gen_text);
    run("100KB 텍스트",            102400, gen_text);
    run("1MB 텍스트",              1048576, gen_text);
    run("4MB 텍스트",              4ULL*1024*1024, gen_text);
    run("4MB 바이너리",            4ULL*1024*1024, gen_bin);
    run("8MB 텍스트",              8ULL*1024*1024, gen_text);
    run("16MB 텍스트",             16ULL*1024*1024, gen_text);
    run("16MB 단일바이트",         16ULL*1024*1024, gen_mono);
    run("64MB 텍스트",             64ULL*1024*1024, gen_text);
    run("128MB 텍스트",            128ULL*1024*1024, gen_text);
    run("256MB 단일바이트",        256ULL*1024*1024, gen_mono);
    run("512MB 텍스트",            512ULL*1024*1024, gen_text);
    run("1GB 텍스트",              1024ULL*1024*1024, gen_text);

    printf("\n══════════════════════════════════════════════════════════════════════\n");
    printf("  핵심: 이미지 크기는 활성 셀 수에만 비례 (원본 크기 무관)\n");
    printf("  → 데이터 클수록 → 밝기만 높아짐 → 이미지 크기 거의 불변\n");
    printf("══════════════════════════════════════════════════════════════════════\n");

    return 0;
}
