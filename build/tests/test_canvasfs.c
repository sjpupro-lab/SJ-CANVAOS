#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "../include/canvasos_types.h"
#include "../include/canvasfs.h"
#include "../include/canvasfs_bpage.h"

extern void activeset_init(ActiveSet *);
extern void activeset_open(ActiveSet *, uint32_t);
extern void activeset_close(ActiveSet *, uint32_t);

static Cell canvas[CANVAS_W * CANVAS_H];

static void ls_cb(int n, const char *name, FsKey k, void *u) {
    (void)u;
    printf("  [%d] '%.4s'  vol=%u slot=%u\n", n, name, k.gate_id, k.slot);
}

int main(void) {
    memset(canvas, 0, sizeof(canvas));
    CanvasFS fs;
    fs_init(&fs, canvas, CANVAS_W * CANVAS_H, NULL);

    /* FreeMap at a safe non-reserved tile */
    uint16_t fm = 10;
    assert(fs_freemap_init(&fs, fm) == FS_OK);
    printf("FreeMap gate=%u  Reserved=[%u..%u]\n",
           fm, FS_RESERVED_LO, FS_RESERVED_HI);

    /* verify reserved tiles are blocked */
    for (uint16_t g = FS_RESERVED_LO; g <= FS_RESERVED_HI; g++) {
        FsResult r = fs_format_volume(&fs, g, FS_BPAGE_IDENTITY);
        assert(r == FS_ERR_OOB);
    }
    printf("Reserved tiles: all blocked OK\n");

    /* =============================================
     * TEST 1: VOLH+VOLT, TINY/SMALL/LARGE, MetaTile real_len
     * ============================================= */
    printf("\n=== TEST 1: VOLH+VOLT  TINY/SMALL/LARGE ===\n");

    uint16_t vol = 20;
    assert(fs_format_volume(&fs, vol, FS_BPAGE_XOR8) == FS_OK);

    /* verify 256 slots available — allocate all */
    uint8_t slots[256];
    for (int i = 0; i < 256; i++) {
        assert(fs_alloc_slot(&fs, vol, &slots[i]) == FS_OK);
    }
    /* 257th must fail */
    uint8_t dummy;
    assert(fs_alloc_slot(&fs, vol, &dummy) == FS_ERR_NOSLOT);
    printf("256 slots fully allocated OK\n");

    FsKey kt = {vol, slots[0]};
    FsKey ks = {vol, slots[1]};
    FsKey kl = {vol, slots[2]};

    /* TINY */
    assert(fs_write(&fs, kt, (const uint8_t*)"OK!", 3) == FS_OK);
    uint8_t buf[2048]; size_t rlen;
    assert(fs_read(&fs, kt, buf, sizeof(buf), &rlen) == FS_OK);
    buf[rlen] = 0;
    printf("TINY   : '%s'\n", buf);
    assert(rlen == 3 && memcmp(buf, "OK!", 3) == 0);

    /* SMALL */
    const char *small = "HELLO_CanvasFS_v1.3";
    assert(fs_write(&fs, ks, (const uint8_t*)small, strlen(small)) == FS_OK);
    assert(fs_read(&fs, ks, buf, sizeof(buf), &rlen) == FS_OK);
    buf[rlen] = 0;
    printf("SMALL  : '%s' len=%zu\n", buf, rlen);
    assert(rlen == strlen(small) && memcmp(buf, small, rlen) == 0);

    /* LARGE — 600 B, chained + MetaTile */
    static uint8_t big[600];
    for (int i = 0; i < 600; i++) big[i] = (uint8_t)('A' + (i % 26));
    assert(fs_write(&fs, kl, big, 600) == FS_OK);

    FsSlotClass cls; uint32_t rlen32;
    assert(fs_stat(&fs, kl, &cls, &rlen32) == FS_OK);
    printf("LARGE  : class=%d real_len=%u\n", (int)cls, rlen32);
    assert(cls == FS_SLOT_LARGE && rlen32 == 600);

    assert(fs_read(&fs, kl, buf, sizeof(buf), &rlen) == FS_OK);
    assert(rlen == 600 && memcmp(buf, big, 600) == 0);
    printf("LARGE  : read=%zu first=%c last=%c\n", rlen, buf[0], buf[rlen-1]);
    printf("[PASS] TEST 1\n");

    /* =============================================
     * TEST 2: per-slot bpage override
     * ============================================= */
    printf("\n=== TEST 2: per-slot bpage override ===\n");
    FsKey kov = {vol, slots[3]};
    assert(fs_slot_set_bpage(&fs, kov, FS_BPAGE_NIBBLE) == FS_OK);
    uint16_t eff;
    assert(fs_slot_get_bpage(&fs, kov, &eff) == FS_OK);
    assert(eff == FS_BPAGE_NIBBLE);
    const char *ov = "OVERRIDE";
    assert(fs_write(&fs, kov, (const uint8_t*)ov, strlen(ov)) == FS_OK);
    assert(fs_read(&fs, kov, buf, sizeof(buf), &rlen) == FS_OK);
    buf[rlen] = 0;
    printf("NIBBLE : '%s' (vol bpage=XOR8, slot=NIBBLE)\n", buf);
    assert(rlen == strlen(ov) && memcmp(buf, ov, rlen) == 0);
    printf("[PASS] TEST 2\n");

    /* =============================================
     * TEST 3: Adapter Chain [XOR8→ROTL1]
     * ============================================= */
    printf("\n=== TEST 3: Adapter Chain [XOR8→ROTL1] ===\n");
    uint16_t vol2 = 30;
    assert(fs_format_volume(&fs, vol2, FS_BPAGE_IDENTITY) == FS_OK);
    FsBpageChain chain = bpchain_make(FS_BPAGE_XOR8);
    chain = bpchain_push(chain, FS_BPAGE_ROTL1);
    assert(fs_set_bpage_chain(&fs, vol2, &chain) == FS_OK);
    uint8_t sc4; assert(fs_alloc_slot(&fs, vol2, &sc4) == FS_OK);
    FsKey kch = {vol2, sc4};
    const char *chd = "CHAIN_TEST_XOR8_ROTL1";
    assert(fs_write(&fs, kch, (const uint8_t*)chd, strlen(chd)) == FS_OK);
    assert(fs_read(&fs, kch, buf, sizeof(buf), &rlen) == FS_OK);
    buf[rlen] = 0;
    printf("Chain  : '%s'\n", buf);
    assert(rlen == strlen(chd) && memcmp(buf, chd, rlen) == 0);
    printf("[PASS] TEST 3\n");

    /* =============================================
     * TEST 4: Gate 연동 — VOLH+VOLT 모두 체크
     * ============================================= */
    printf("\n=== TEST 4: Gate 연동 (VOLH+VOLT dual check) ===\n");
    ActiveSet aset; activeset_init(&aset);
    CanvasFS fs2;
    fs_init(&fs2, canvas, CANVAS_W * CANVAS_H, &aset);

    uint16_t gvol = 50;
    /* closed → format fails */
    assert(fs_format_volume(&fs2, gvol, FS_BPAGE_IDENTITY) == FS_ERR_GATE);
    printf("format closed: FS_ERR_GATE OK\n");

    /* open VOLH only — format succeeds, but alloc needs VOLT check */
    activeset_open(&aset, gvol);
    /* VOLT will be allocated by format; we need VOLT open too.
     * Since fs2.aset is set and VOLT gate is unknown before format,
     * format itself only needs VOLH open (it allocates VOLT).
     * After format, VOLT gate is known; accessing slots needs both. */
    assert(fs_format_volume(&fs2, gvol, FS_BPAGE_IDENTITY) == FS_OK);
    printf("format open:   FS_OK\n");

    /* find VOLT gate from VOLH */
    uint16_t volt_g = (uint16_t)(canvas[
        (uint32_t)((gvol/TILES_X)*TILE)*CANVAS_W +
        (gvol%TILES_X)*TILE + 1  /* col=1, row=0 */
    ].A & 0xFFFF);
    printf("VOLT gate=%u\n", volt_g);

    /* alloc slot: VOLH open but VOLT closed → FS_ERR_GATE */
    uint8_t gs;
    assert(fs_alloc_slot(&fs2, gvol, &gs) == FS_ERR_GATE);
    printf("alloc slot (VOLT closed): FS_ERR_GATE OK\n");

    activeset_open(&aset, volt_g);
    assert(fs_alloc_slot(&fs2, gvol, &gs) == FS_OK);
    FsKey gkey = {gvol, gs};
    assert(fs_write(&fs2, gkey, (const uint8_t*)"gated", 5) == FS_OK);

    activeset_close(&aset, volt_g);
    assert(fs_read(&fs2, gkey, buf, sizeof(buf), &rlen) == FS_ERR_GATE);
    printf("read   (VOLT closed): FS_ERR_GATE OK\n");
    printf("[PASS] TEST 4\n");

    /* =============================================
     * TEST 5: DirectoryBlock v2
     * ============================================= */
    printf("\n=== TEST 5: DirectoryBlock v2 ===\n");
    uint16_t dir = 60;
    uint16_t fv0 = 70, fv1 = 80, fv2 = 90;
    assert(fs_mkdir(&fs, dir) == FS_OK);
    FsKey dk0, dk1, dk2;
    assert(fs_dir_create(&fs, dir, "main", fv0, &dk0) == FS_OK);
    assert(fs_dir_create(&fs, dir, "conf", fv1, &dk1) == FS_OK);
    assert(fs_dir_create(&fs, dir, "log",  fv2, &dk2) == FS_OK);
    assert(fs_dir_create(&fs, dir, "main", fv0, NULL) == FS_ERR_EXISTS);

    const char *src = "int main(){return 0;}";
    assert(fs_write(&fs, dk0, (const uint8_t*)src, strlen(src)) == FS_OK);

    printf("ls:\n"); fs_dir_ls(&fs, dir, ls_cb, NULL);

    FsKey found;
    assert(fs_dir_open(&fs, dir, "main", &found) == FS_OK);
    assert(fs_read(&fs, found, buf, sizeof(buf), &rlen) == FS_OK);
    buf[rlen] = 0;
    printf("open 'main': '%s'\n", buf);
    assert(rlen == strlen(src) && memcmp(buf, src, rlen) == 0);
    assert(fs_dir_open(&fs, dir, "nope", &found) == FS_ERR_NOTFOUND);

    assert(fs_dir_unlink(&fs, dir, "log") == FS_OK);
    assert(fs_dir_open(&fs, dir, "log",  &found) == FS_ERR_NOTFOUND);
    printf("after unlink 'log':\n"); fs_dir_ls(&fs, dir, ls_cb, NULL);
    printf("[PASS] TEST 5\n");

    /* =============================================
     * TEST 6: FreeMap free/reuse
     * ============================================= */
    printf("\n=== TEST 6: FreeMap free/reuse ===\n");
    uint16_t t1, t2;
    assert(fs_freemap_alloc(&fs, &t1) == FS_OK);
    assert(fs_freemap_alloc(&fs, &t2) == FS_OK);
    assert(t1 != t2);
    assert(fs_freemap_free(&fs, t1) == FS_OK);
    uint16_t t3;
    assert(fs_freemap_alloc(&fs, &t3) == FS_OK);
    printf("alloc t1=%u t2=%u  free t1  realloc t3=%u\n", t1, t2, t3);
    assert(t3 == t1);
    printf("[PASS] TEST 6\n");

    printf("\n=== ALL TESTS PASSED ===\n");
    return 0;
}
