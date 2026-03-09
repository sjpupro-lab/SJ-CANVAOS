/**
 * SJ Spatial V2 — 벤치마크
 * 핵심 테스트: 4MB+ 데이터에서 압축률 극대화 검증
 */

#include "sj_spatial_v2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double time_ms(clock_t s, clock_t e) {
    return (double)(e - s) / CLOCKS_PER_SEC * 1000.0;
}

static void run_bench(const char *name, const uint8_t *data, size_t len) {
    printf("\n━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("  BENCHMARK: %s\n", name);
    printf("  Input: %zu bytes (%.2f MB)\n", len, (double)len / (1024*1024));
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    V2Canvas *v = (V2Canvas *)calloc(1, sizeof(V2Canvas));
    if (!v || v2_init(v) != 0) {
        printf("  ERROR: init failed\n");
        if (v) free(v);
        return;
    }

    /* 인코딩 */
    clock_t t0 = clock();
    v2_encode(v, data, len);
    clock_t t1 = clock();

    /* 압축 */
    size_t bound = 28 + V2_PAGES * 256 * 8 + v->wh.count * 4 + 64;
    uint8_t *comp = (uint8_t *)malloc(bound);
    clock_t t2 = clock();
    size_t comp_len = v2_compress_full(v, comp, bound);
    clock_t t3 = clock();

    /* 복원 */
    size_t restore_cap = len + 4096;
    uint8_t *restored = (uint8_t *)malloc(restore_cap);
    size_t restored_len = restore_cap;
    clock_t t4 = clock();
    int rc = v2_decompress_full(comp, comp_len, restored, &restored_len);
    clock_t t5 = clock();

    /* 검증 */
    int match = 0;
    if (rc == 0 && restored_len >= len) {
        match = (memcmp(restored, data, len) == 0);
    }

    /* 통계 */
    V2Stats s = v2_stats(v);

    double ratio = (comp_len > 0) ? (double)len / (double)comp_len : 0;
    double saving = (len > 0) ? (1.0 - (double)comp_len / (double)len) * 100.0 : 0;

    printf("\n  ┌─ 압축 결과 ──────────────────────────────────\n");
    printf("  │ 원본 크기:       %12zu bytes (%.2f MB)\n", len, (double)len/1048576);
    printf("  │ 캔버스 고정:     %12zu bytes (%.2f MB)\n", s.canvas_raw_bytes, (double)s.canvas_raw_bytes/1048576);
    printf("  │ 압축 크기:       %12zu bytes (%.2f MB)\n", comp_len, (double)comp_len/1048576);
    printf("  │ 압축률:          %12.2fx\n", ratio);
    printf("  │ 절감:            %11.1f%%\n", saving);
    printf("  ├─ 캔버스 배치 ──────────────────────────────\n");
    printf("  │ 최대 밝기:       %12u\n", s.max_brightness);
    printf("  │ 사용 페이지:     %12u / %d\n", s.pages_used, V2_PAGES);
    printf("  │ 활성 R슬롯:      %12u\n", s.active_r_slots);
    printf("  │ 활성 G슬롯:      %12u\n", s.active_g_slots);
    printf("  │ A존 오버플로:    %12u\n", s.a_overflow_count);
    printf("  ├─ 시간 ─────────────────────────────────────\n");
    printf("  │ 인코딩:          %11.1f ms\n", time_ms(t0, t1));
    printf("  │ 압축:            %11.1f ms\n", time_ms(t2, t3));
    printf("  │ 복원:            %11.1f ms\n", time_ms(t4, t5));
    printf("  ├─ 검증 ─────────────────────────────────────\n");
    printf("  │ 복원 일치:       %12s\n", match ? "PASS" : "FAIL");
    printf("  └────────────────────────────────────────────────\n");

    /* 캔버스 맵 */
    v2_print_canvas_map(v);

    free(comp);
    free(restored);
    v2_free(v);
    free(v);
}

/* ─── 데이터 생성기 ─── */

static uint8_t *gen_text_repeat(size_t target_mb, size_t *out_len) {
    const char *phrase = "CanvasOS SJ Spatial Compression Engine V2 - "
                         "data restructuring for OS execution. ";
    size_t plen = strlen(phrase);
    size_t total = target_mb * 1024 * 1024;
    uint8_t *data = (uint8_t *)malloc(total);
    for (size_t i = 0; i < total; i++)
        data[i] = (uint8_t)phrase[i % plen];
    *out_len = total;
    return data;
}

static uint8_t *gen_binary_structured(size_t target_mb, size_t *out_len) {
    /* 실행 파일 유사 구조: 헤더 + 코드 섹션 + 데이터 섹션 */
    size_t total = target_mb * 1024 * 1024;
    uint8_t *data = (uint8_t *)malloc(total);

    /* 헤더 (반복 패턴) */
    size_t hdr = total / 10;
    memset(data, 0x00, hdr);
    data[0] = 0x7F; data[1] = 'E'; data[2] = 'L'; data[3] = 'F';

    /* 코드 섹션 (의사 명령어, 구조화) */
    size_t code_end = total / 2;
    for (size_t i = hdr; i < code_end; i++)
        data[i] = (uint8_t)((i * 7 + 0x55) & 0xFF);

    /* 데이터 섹션 (문자열 테이블 등, 반복 많음) */
    const char *strings[] = {"main", "printf", "malloc", "free", "canvas_init",
                             "engine_run", "wh_write", "bh_compress", NULL};
    size_t si = 0;
    for (size_t i = code_end; i < total; ) {
        const char *s = strings[si % 8]; si++;
        size_t slen = strlen(s) + 1;
        size_t cplen = (i + slen <= total) ? slen : total - i;
        memcpy(data + i, s, cplen);
        i += cplen;
    }

    *out_len = total;
    return data;
}

static uint8_t *gen_random(size_t target_mb, size_t *out_len) {
    size_t total = target_mb * 1024 * 1024;
    uint8_t *data = (uint8_t *)malloc(total);
    srand(42);
    for (size_t i = 0; i < total; i++)
        data[i] = (uint8_t)(rand() % 256);
    *out_len = total;
    return data;
}

static uint8_t *gen_mono(size_t target_mb, size_t *out_len) {
    size_t total = target_mb * 1024 * 1024;
    uint8_t *data = (uint8_t *)malloc(total);
    memset(data, 0x42, total);
    *out_len = total;
    return data;
}

int main(void) {
    printf("══════════════════════════════════════════════════════════════\n");
    printf("  SJ Spatial Compression Engine V2 (C Native)\n");
    printf("  512×512 Fixed Image = OS Execution Unit\n");
    printf("  \"집 그린 이미지 = 도심숲 이미지 = 같은 크기\"\n");
    printf("══════════════════════════════════════════════════════════════\n");

    size_t len;
    uint8_t *data;

    /* 1. 소형 (1KB) — 기준선 */
    {
        const char *small = "Hello SJ Spatial V2! ";
        size_t slen = strlen(small);
        len = slen * 50;
        data = (uint8_t *)malloc(len);
        for (size_t i = 0; i < 50; i++) memcpy(data + i * slen, small, slen);
        run_bench("소형 1KB (기준선)", data, len);
        free(data);
    }

    /* 2. 4MB 텍스트 (임계점) */
    data = gen_text_repeat(4, &len);
    run_bench("4MB 텍스트 (임계점)", data, len);
    free(data);

    /* 3. 4MB 바이너리 (ELF 유사) */
    data = gen_binary_structured(4, &len);
    run_bench("4MB 바이너리 (OS 실행파일 유사)", data, len);
    free(data);

    /* 4. 8MB 텍스트 */
    data = gen_text_repeat(8, &len);
    run_bench("8MB 텍스트 (대형)", data, len);
    free(data);

    /* 5. 8MB 랜덤 (최악) */
    data = gen_random(8, &len);
    run_bench("8MB 랜덤 (최악 케이스)", data, len);
    free(data);

    /* 6. 8MB 단일바이트 (최상) */
    data = gen_mono(8, &len);
    run_bench("8MB 단일바이트 (최상 케이스)", data, len);
    free(data);

    /* 7. 16MB 텍스트 (스케일 검증) */
    data = gen_text_repeat(16, &len);
    run_bench("16MB 텍스트 (스케일 검증)", data, len);
    free(data);

    printf("\n══════════════════════════════════════════════════════════════\n");
    printf("  모든 벤치마크 완료.\n");
    printf("  핵심: 파일 크기 ↑ → 압축률 ↑ (고정 캔버스 크기 효과)\n");
    printf("══════════════════════════════════════════════════════════════\n");

    return 0;
}
