/*
 * test_phase8.c — Phase-8 Userland Tests (18 cases)
 */
#include <stdio.h>
#include <string.h>
#include "../include/canvasos_types.h"
#include "../include/canvasos_engine_ctx.h"
#include "../include/canvasos_gate_ops.h"
#include "../include/engine_time.h"
#include "../include/canvas_determinism.h"
#include "../include/canvasos_proc.h"
#include "../include/canvasos_signal.h"
#include "../include/canvasos_mprotect.h"
#include "../include/canvasos_pipe.h"
#include "../include/canvasos_syscall.h"
#include "../include/canvasos_detmode.h"

static Cell      g_cells[CANVAS_W * CANVAS_H];
static GateState g_gates[TILE_COUNT];
static uint8_t   g_active[TILE_COUNT];
static int P=0, F=0;

#define T(n)     printf("  %-54s ",n)
#define PASS()   do{printf("PASS\n");P++;}while(0)
#define FAIL(m)  do{printf("FAIL: %s\n",m);F++;return;}while(0)
#define CHK(c,m) do{if(!(c))FAIL(m);}while(0)

static EngineContext *mk(void) {
    static EngineContext ctx;
    memset(g_cells,0,sizeof(g_cells));
    memset(g_gates,0,sizeof(g_gates));
    memset(g_active,0,sizeof(g_active));
    engctx_init(&ctx,g_cells,CANVAS_W*CANVAS_H,g_gates,g_active,NULL);
    engctx_tick(&ctx);
    return &ctx;
}

static void t1(void){
    T("P8-T1 spawn/exit/wait/respawn");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pid=proc_spawn(&pt,PID_INIT,10,100,1);
    CHK(pid>0,"spawn"); CHK(pt.count==2,"count");
    CHK(proc_exit(&pt,(uint32_t)pid,42)==0,"exit");
    uint8_t st=0; int r=proc_wait(&pt,PID_INIT,&st);
    CHK(r==pid,"wait pid"); CHK(st==42,"exit code");
    int pid2=proc_spawn(&pt,PID_INIT,20,50,1);
    CHK(pid2>0,"respawn"); PASS();
}
static void t2(void){
    T("P8-T2 orphan adoption to init");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pa=proc_spawn(&pt,PID_INIT,10,100,1);
    int ch=proc_spawn(&pt,(uint32_t)pa,20,100,1);
    proc_exit(&pt,(uint32_t)pa,0);
    Proc8 *cp=proc_find(&pt,(uint32_t)ch);
    CHK(cp&&cp->parent_pid==PID_INIT,"adopted"); PASS();
}
static void t3(void){
    T("P8-T3 PROC_MAX limit");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    for(int i=0;i<PROC8_MAX-1;i++) proc_spawn(&pt,PID_INIT,(uint16_t)(i%TILE_COUNT),100,1);
    CHK(proc_spawn(&pt,PID_INIT,999,100,1)==-1,"overflow"); PASS();
}
static void t4(void){
    T("P8-T4 SIGKILL → ZOMBIE");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pid=proc_spawn(&pt,PID_INIT,10,100,1);
    sig_send(&pt,(uint32_t)pid,SIG_KILL);
    uint8_t st=0; CHK(proc_wait(&pt,PID_INIT,&st)==pid,"wait");
    CHK(st==(128+SIG_KILL),"exit code"); PASS();
}
static void t5(void){
    T("P8-T5 SIGSTOP/SIGCONT");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pid=proc_spawn(&pt,PID_INIT,10,100,1);
    sig_send(&pt,(uint32_t)pid,SIG_STOP);
    CHK(proc_find(&pt,(uint32_t)pid)->state==PROC_SLEEPING,"stopped");
    sig_send(&pt,(uint32_t)pid,SIG_CONT);
    CHK(proc_find(&pt,(uint32_t)pid)->state==PROC_RUNNING,"cont"); PASS();
}
static void t6(void){
    T("P8-T6 SIGKILL unmaskable");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pid=proc_spawn(&pt,PID_INIT,10,100,1);
    sig_mask_set(&pt,(uint32_t)pid,0xFF);
    CHK(!(proc_find(&pt,(uint32_t)pid)->sig_mask&SIG_BIT(SIG_KILL)),"mask bit");
    sig_send(&pt,(uint32_t)pid,SIG_KILL);
    uint8_t st=0; CHK(proc_wait(&pt,PID_INIT,&st)==pid,"killed"); PASS();
}
static void t7(void){
    T("P8-T7 tile alloc ownership");
    EngineContext *c=mk(); TileProtection tp; tprot_init(&tp);
    int s=tile_alloc(&tp,c,1,3);
    CHK(s>=0,"alloc"); CHK(tp.owner[s]==1&&tp.owner[s+1]==1&&tp.owner[s+2]==1,"own"); PASS();
}
static void t8(void){
    T("P8-T8 tile FAULT on other pid");
    EngineContext *c=mk(); TileProtection tp; tprot_init(&tp);
    int s=tile_alloc(&tp,c,1,1);
    CHK(tile_check(&tp,1,(uint16_t)s,PERM_READ)==0,"owner ok");
    CHK(tile_check(&tp,2,(uint16_t)s,PERM_READ)==-1,"other fault");
    CHK(tile_check(&tp,0,(uint16_t)s,PERM_READ)==0,"root ok"); PASS();
}
static void t9(void){
    T("P8-T9 PERM_SHARED");
    EngineContext *c=mk(); TileProtection tp; tprot_init(&tp);
    int s=tile_alloc(&tp,c,1,1);
    tile_set_perm(&tp,c,1,(uint16_t)s,PERM_READ|PERM_SHARED);
    CHK(tile_check(&tp,2,(uint16_t)s,PERM_READ)==0,"shared ok"); PASS();
}
static void t10(void){
    T("P8-T10 pipe write/read match");
    EngineContext *c=mk(); PipeTable ptab; pipe_table_init(&ptab);
    int id=pipe_create(&ptab,c,1,2);
    CHK(id>=0,"create");
    CHK(pipe_write(&ptab,c,id,(const uint8_t*)"HELLO",5)==5,"write");
    uint8_t buf[8]={0};
    CHK(pipe_read(&ptab,c,id,buf,5)==5,"read");
    CHK(memcmp(buf,"HELLO",5)==0,"match"); PASS();
}
static void t11(void){
    T("P8-T11 pipe full partial write");
    EngineContext *c=mk(); PipeTable ptab; pipe_table_init(&ptab);
    int id=pipe_create(&ptab,c,1,2);
    uint8_t big[PIPE_BUF_SIZE]; memset(big,'X',sizeof(big));
    int w1=pipe_write(&ptab,c,id,big,PIPE_BUF_SIZE);
    CHK(w1>0&&w1<PIPE_BUF_SIZE,"partial");
    int w2=pipe_write(&ptab,c,id,big,PIPE_BUF_SIZE);
    CHK(w2==0,"full=0"); PASS();
}
static void t12(void){
    T("P8-T12 pipe close → 0 read");
    EngineContext *c=mk(); PipeTable ptab; pipe_table_init(&ptab);
    int id=pipe_create(&ptab,c,1,2);
    pipe_close(&ptab,c,id);
    uint8_t buf[8]; CHK(pipe_read(&ptab,c,id,buf,8)==0,"eof"); PASS();
}
static void t13(void){
    T("P8-T13 syscall GETPID");
    EngineContext *c=mk(); syscall_init();
    CHK(syscall_dispatch(c,42,SYS_GETPID,0,0,0)==42,"pid42");
    CHK(syscall_dispatch(c,7,SYS_GETPID,0,0,0)==7,"pid7"); PASS();
}
static void t14(void){
    T("P8-T14 unregistered syscall -ENOSYS");
    EngineContext *c=mk(); syscall_init();
    CHK(syscall_dispatch(c,1,0x5F,0,0,0)==-38,"enosys"); PASS();
}
static void t15(void){
    T("P8-T15 detmode toggle");
    DetMode dm; det_init(&dm);
    CHK(det_is_deterministic(&dm),"init det");
    det_set_all(&dm,false); CHK(!det_is_deterministic(&dm),"nondet");
    det_set_dk(&dm,2,true); CHK(!det_is_deterministic(&dm),"partial");
    det_set_all(&dm,true); CHK(det_is_deterministic(&dm),"restored"); PASS();
}
static void t16(void){
    T("P8-T16 proc_exec code tile swap");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pid=proc_spawn(&pt,PID_INIT,10,100,1);
    CHK(proc_find(&pt,(uint32_t)pid)->code_tile==10,"before");
    proc_exec(&pt,(uint32_t)pid,50);
    CHK(proc_find(&pt,(uint32_t)pid)->code_tile==50,"after"); PASS();
}
static void t17(void){
    T("P8-T17 proc_tick decay → SLEEPING");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    int pid=proc_spawn(&pt,PID_INIT,10,3,1);
    proc_tick(&pt); proc_tick(&pt); proc_tick(&pt);
    CHK(proc_find(&pt,(uint32_t)pid)->state==PROC_SLEEPING,"sleep@0"); PASS();
}
static void t18(void){
    T("P8-T18 WH record for proc_spawn");
    EngineContext *c=mk(); ProcTable pt; proctable_init(&pt,c);
    uint32_t t0=c->tick;
    proc_spawn(&pt,PID_INIT,10,100,1);
    WhRecord r; bool found=false;
    for(uint32_t t=t0;t<=c->tick+2;t++)
        if(wh_read_record(c,t,&r)&&r.opcode_index==WH_OP_PROC_SPAWN){found=true;break;}
    CHK(found,"spawn WH record"); PASS();
}

int main(void){
    printf("\n=== Phase-8 Userland Tests ===\n");
    t1();t2();t3();t4();t5();t6();t7();t8();t9();
    t10();t11();t12();t13();t14();t15();t16();t17();t18();
    printf("==============================\n");
    printf("PASS: %d / FAIL: %d\n\n",P,F);
    return F?1:0;
}
