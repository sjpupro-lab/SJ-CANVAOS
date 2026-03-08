/*
 * test_tervas.c — Phase-7 Tervas 검증 (Spec §16)
 *
 * TV1:  attach + full snapshot
 * TV2:  windowed snapshot
 * TV3:  projection determinism (동일 snapshot → 동일 TvFrame)
 * TV4:  inspect OOB 검증
 * TV5:  WH/BH geometry
 * TV6:  CLI 입력 검증 (OOB/bad args)
 * TV7:  engine hash 불변 (READ-ONLY R-4)
 * TV8:  TvRenderCell style bits 정확성
 * TV9:  tv_build_frame — Renderer 공통 포맷 검증 (Spec §11)
 * TV10: quick 명령군 (Spec §12)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/canvas_determinism.h"
#include "../include/canvas_bh_compress.h"
#include "../include/engine_time.h"
#include "../include/tervas_core.h"
#include "../include/tervas_bridge.h"
#include "../include/tervas_cli.h"
#include "../include/tervas_projection.h"
#include "../include/tervas_render_cell.h"
#include "../include/tervas_dispatch.h"

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];

static EngineContext make_eng(void) {
    memset(g_cells, 0, sizeof(g_cells));
    memset(g_gates, 0, sizeof(g_gates));
    memset(g_active,0, sizeof(g_active));
    EngineContext ctx;
    engctx_init(&ctx, g_cells, CANVAS_W*CANVAS_H, g_gates, g_active, NULL);
    gate_open_tile(&ctx, 10);
    g_cells[512*CANVAS_W+512].B=0x01; g_cells[512*CANVAS_W+512].G=200;
    g_cells[512*CANVAS_W+512].A=0x01000000u;
    g_cells[513*CANVAS_W+512].B=0x02; g_cells[513*CANVAS_W+512].G=100;
    g_cells[WH_Y0*CANVAS_W+WH_X0].G=50;
    g_cells[BH_Y0*CANVAS_W+BH_X0].G=30;
    bh_set_energy(&ctx,1,100,255);
    engctx_tick(&ctx); engctx_tick(&ctx);
    return ctx;
}

static int P=0,F=0;
#define T(n)  printf("  %-54s ",n)
#define PASS() do{printf("PASS\n");P++;}while(0)
#define FAIL(m) do{printf("FAIL: %s\n",m);F++;return;}while(0)
#define CHK(c,m)    do{if(!(c))FAIL(m);}while(0)
#define CHEQ(a,b,m) do{if((a)!=(b)){printf("(%ld≠%ld) ",(long)(a),(long)(b));FAIL(m);}}while(0)

/* TV1 */
static void tv1(void) {
    T("TV1 attach + full snapshot");
    EngineContext e=make_eng(); Tervas tv;
    CHK(tervas_init(&tv)==TV_OK,"init");
    CHK(tervas_bridge_attach(&tv,&e)==TV_OK,"attach");
    CHK(tervas_bridge_snapshot(&tv,&e,e.tick)==TV_OK,"snap");
    CHK(tv.snapshot.valid,"valid");
    CHEQ(tv.snapshot.tick,e.tick,"tick");
    CHK(memcmp(tv.snapshot.canvas,e.cells,sizeof(Cell)*CANVAS_W*CANVAS_H)==0,"canvas copy");
    tervas_free(&tv); PASS();
}

/* TV2 */
static void tv2(void) {
    T("TV2 windowed snapshot dimensions");
    EngineContext e=make_eng(); Tervas tv; tervas_init(&tv);
    tv.filter.snap_mode=TV_SNAP_WINDOW;
    tv.filter.viewport=(TvViewport){WH_X0,WH_Y0,64,64};
    CHK(tervas_bridge_snapshot(&tv,&e,e.tick)==TV_OK,"snap");
    CHEQ(tv.snapshot.width,64u,"w"); CHEQ(tv.snapshot.height,64u,"h");
    tervas_free(&tv); PASS();
}

/* TV3 */
static void tv3(void) {
    T("TV3 TvFrame determinism (same snap → same frame)");
    EngineContext e=make_eng(); Tervas tv1,tv2;
    tervas_init(&tv1); tervas_init(&tv2);
    tervas_bridge_snapshot(&tv1,&e,e.tick);
    tervas_bridge_snapshot(&tv2,&e,e.tick);
    TvFrame f1,f2;
    TvFilter flt; tv_filter_reset(&flt); flt.mode=TV_PROJ_WH;
    tv_build_frame(&f1,&tv1.snapshot,&flt,64,32);
    tv_build_frame(&f2,&tv2.snapshot,&flt,64,32);
    CHEQ(f1.count,f2.count,"count");
    CHEQ(f1.wh_active,f2.wh_active,"wh_active");
    /* cell-by-cell style check */
    for(uint32_t i=0;i<f1.count;i++)
        if(f1.cells[i].visible!=f2.cells[i].visible) FAIL("visible mismatch");
    tervas_free(&tv1); tervas_free(&tv2); PASS();
}

/* TV4 */
static void tv4(void) {
    T("TV4 inspect OOB");
    EngineContext e=make_eng(); char b[256];
    CHK(tervas_bridge_inspect(&e,512,512,b,sizeof(b))==TV_OK,"ok");
    CHK(tervas_bridge_inspect(&e,1024,0,b,sizeof(b))==TV_ERR_OOB,"x OOB");
    CHK(tervas_bridge_inspect(&e,0,1024,b,sizeof(b))==TV_ERR_OOB,"y OOB");
    PASS();
}

/* TV5 */
static void tv5(void) {
    T("TV5 WH/BH geometry + disjoint");
    CHK( tv_is_wh_cell(WH_X0,WH_Y0),"WH TL");
    CHK( tv_is_wh_cell(WH_X0+WH_W-1,WH_Y0+WH_H-1),"WH BR");
    CHK(!tv_is_wh_cell(WH_X0-1,WH_Y0),"WH L");
    CHK(!tv_is_wh_cell(WH_X0,WH_Y0+WH_H),"WH B");
    CHK( tv_is_bh_cell(BH_X0,BH_Y0),"BH TL");
    CHK(!tv_is_bh_cell(BH_X0,BH_Y0-1),"BH above");
    for(uint32_t y=BH_Y0;y<BH_Y0+BH_H;y++)
        for(uint32_t x=BH_X0;x<BH_X0+BH_W;x++)
            CHK(!tv_is_wh_cell(x,y),"WH∩BH≠∅");
    PASS();
}

/* TV6 */
static void tv6(void) {
    T("TV6 CLI input validation");
    EngineContext e=make_eng(); Tervas tv; tervas_init(&tv);
    tervas_bridge_snapshot(&tv,&e,e.tick); tv.running=true;
    CHEQ(tv_cli_exec(&tv,&e,"inspect 9999 0"),TV_ERR_OOB,"oob x");
    CHEQ(tv_cli_exec(&tv,&e,"inspect 0 9999"),TV_ERR_OOB,"oob y");
    CHEQ(tv_cli_exec(&tv,&e,"inspect abc def"),TV_ERR_OOB,"bad xy");
    CHEQ(tv_cli_exec(&tv,&e,"zoom 0"),TV_ERR_ZOOM,"z0");
    CHEQ(tv_cli_exec(&tv,&e,"zoom 9"),TV_ERR_ZOOM,"z9");
    CHEQ(tv_cli_exec(&tv,&e,"region badname"),TV_ERR_NO_REGION,"bad region");
    CHEQ(tv_cli_exec(&tv,&e,"region wh"),TV_OK,"ok region");
    CHEQ(tv_cli_exec(&tv,&e,"snap bad"),TV_ERR_CMD,"snap bad");
    CHEQ(tv_cli_exec(&tv,&e,"snap win"),TV_OK,"snap win");
    CHEQ(tv_cli_exec(&tv,&e,"tick goto 999999999"),TV_OK,"tick clamp");
    CHEQ(tv_cli_exec(&tv,&e,"xyzzy"),TV_ERR_CMD,"unknown");
    tv_cli_exec(&tv,&e,"quit"); CHK(!tv.running,"quit");
    tervas_free(&tv); PASS();
}

/* TV7 */
static void tv7(void) {
    T("TV7 READ-ONLY: engine hash unchanged");
    EngineContext e=make_eng();
    uint32_t hb=dk_canvas_hash(e.cells,e.cells_count);
    Tervas tv; tervas_init(&tv);
    tervas_bridge_attach(&tv,&e);
    tervas_bridge_snapshot(&tv,&e,e.tick);
    TvFrame fr; TvFilter f; tv_filter_reset(&f);
    f.mode=TV_PROJ_WH; tv_build_frame(&fr,&tv.snapshot,&f,64,32);
    f.mode=TV_PROJ_BH; tv_build_frame(&fr,&tv.snapshot,&f,64,32);
    CHEQ(dk_canvas_hash(e.cells,e.cells_count),hb,"hash changed");
    tervas_free(&tv); PASS();
}

/* TV8 */
static void tv8(void) {
    T("TV8 TvRenderCell style bits accuracy");
    EngineContext e=make_eng(); Tervas tv; tervas_init(&tv);
    tervas_bridge_snapshot(&tv,&e,e.tick);
    TvFrame fr; TvFilter f; tv_filter_reset(&f); f.mode=TV_PROJ_ALL;
    tv_build_frame(&fr,&tv.snapshot,&f,64,32);
    /* WH 셀이 TV_STYLE_WH 비트를 가지는지 검증 */
    bool found_wh=false, found_bh=false;
    for(uint32_t i=0;i<fr.count;i++){
        if(tv_is_wh_cell(fr.cells[i].x,fr.cells[i].y))
            if(fr.cells[i].style & TV_STYLE_WH){found_wh=true;}
        if(tv_is_bh_cell(fr.cells[i].x,fr.cells[i].y))
            if(fr.cells[i].style & TV_STYLE_BH){found_bh=true;}
    }
    CHK(found_wh,"TV_STYLE_WH not set in WH cells");
    CHK(found_bh,"TV_STYLE_BH not set in BH cells");
    /* A_MATCH: cell at (512,512) has A=0x01000000 */
    f.mode=TV_PROJ_A; f.a_values[0]=0x01000000u; f.a_count=1;
    tv_build_frame(&fr,&tv.snapshot,&f,64,32);
    bool found_a=false;
    for(uint32_t i=0;i<fr.count;i++)
        if(fr.cells[i].a==0x01000000u && (fr.cells[i].style & TV_STYLE_A_MATCH))
            found_a=true;
    CHK(found_a,"TV_STYLE_A_MATCH missing");
    tervas_free(&tv); PASS();
}

/* TV9: Renderer 공통 포맷 — 같은 snapshot, 다른 뷰 크기 → 통계 동일 */
static void tv9(void) {
    T("TV9 tv_build_frame renderer-agnostic stats");
    EngineContext e=make_eng(); Tervas tv; tervas_init(&tv);
    tervas_bridge_snapshot(&tv,&e,e.tick);
    TvFilter f; tv_filter_reset(&f); f.mode=TV_PROJ_WH;
    TvFrame f64,f32;
    tv_build_frame(&f64,&tv.snapshot,&f,64,32);
    tv_build_frame(&f32,&tv.snapshot,&f,32,16);
    /* wh_active는 뷰 해상도와 무관하게 동일해야 함 */
    CHEQ(f64.wh_active,f32.wh_active,"wh_active renderer-invariant");
    CHEQ(f64.tick,f32.tick,"tick match");
    tervas_free(&tv); PASS();
}

/* TV10: quick 명령군 */
static void tv10(void) {
    T("TV10 quick commands (Spec §12)");
    EngineContext e=make_eng(); Tervas tv; tervas_init(&tv);
    tervas_bridge_snapshot(&tv,&e,e.tick); tv.running=true;
    CHEQ(tv_cli_exec(&tv,&e,"quick wh"),TV_OK,"q wh");
    CHEQ(tv.filter.mode,TV_PROJ_WH,"q wh mode");
    CHEQ(tv_cli_exec(&tv,&e,"quick bh"),TV_OK,"q bh");
    CHEQ(tv.filter.mode,TV_PROJ_BH,"q bh mode");
    CHEQ(tv_cli_exec(&tv,&e,"quick all"),TV_OK,"q all");
    CHEQ(tv.filter.mode,TV_PROJ_ALL,"q all mode");
    CHEQ(tv_cli_exec(&tv,&e,"quick overlap"),TV_OK,"q overlap");
    CHEQ(tv.filter.mode,TV_PROJ_AB_OVERLAP,"q overlap mode");
    CHEQ(tv_cli_exec(&tv,&e,"quick now"),TV_OK,"q now");
    CHEQ(tv_cli_exec(&tv,&e,"quick inspect 512 512"),TV_OK,"q inspect");
    CHEQ(tv_cli_exec(&tv,&e,"quick inspect 9999 0"),TV_ERR_OOB,"q inspect oob");
    CHEQ(tv_cli_exec(&tv,&e,"quick region wh"),TV_OK,"q region wh");
    CHEQ(tv_cli_exec(&tv,&e,"quick region badname"),TV_ERR_NO_REGION,"q region bad");
    tervas_free(&tv); PASS();
}

int main(void){
    printf("\n=== Phase-7 Tervas Tests (v1.1) ===\n");
    tv1();tv2();tv3();tv4();tv5();
    tv6();tv7();tv8();tv9();tv10();
    printf("====================================\n");
    printf("PASS: %d / FAIL: %d\n\n",P,F);
    return F?1:0;
}
