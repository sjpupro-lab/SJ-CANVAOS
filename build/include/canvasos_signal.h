#pragma once
/*
 * canvasos_signal.h — Phase-8: Signal System
 */
#include <stdint.h>
#include "canvasos_proc.h"

#define SIG_KILL   1    /* 즉시 종료 (블록 불가)         */
#define SIG_STOP   2    /* 일시 정지 (에너지 동결)       */
#define SIG_CONT   3    /* 재개                         */
#define SIG_CHILD  4    /* 자식 종료 알림               */
#define SIG_USR1   5
#define SIG_USR2   6
#define SIG_ALARM  7    /* tick 타이머 만료             */
#define SIG_SEGV   8    /* 메모리 접근 위반 (T8-03)     */
#define SIG_MAX    8

#define WH_OP_SIG_SEND  0x70

/* signal bit positions use (sig-1). SIG_KILL은 mask로 블록 불가 */
#define SIG_BIT(sig)        ((uint8_t)(1u << ((sig) - 1u)))
#define SIG_UNMASKABLE      SIG_BIT(SIG_KILL)

typedef void (*SigHandler)(ProcTable *pt, uint32_t pid, uint8_t sig);

int  sig_send(ProcTable *pt, uint32_t dst_pid, uint8_t signal);
int  sig_check(ProcTable *pt, uint32_t pid);  /* tick boundary에서 호출 */
void sig_mask_set(ProcTable *pt, uint32_t pid, uint8_t mask);
void sig_mask_clear(ProcTable *pt, uint32_t pid, uint8_t mask);
