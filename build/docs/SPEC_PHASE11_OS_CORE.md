# CanvasOS Phase 11: OS 코어 완성 명세서

**버전**: v0.1 — 2026-03-10
**목표**: CanvasOS를 "데모용 프로토타입"에서 "자립 가능한 OS"로 전환

---

## 1. 현재 상태 진단

### 1.1 서브시스템 완성도

| 서브시스템 | 완성도 | 상태 | 위험도 |
|-----------|--------|------|--------|
| Core Engine (scan, gate, active set) | 95% | 안정 | 낮음 |
| EngineContext / CVP I/O | 95% | 안정 | 낮음 |
| CanvasFS (파일시스템) | 85% | 양호 | 낮음 |
| Scheduler | 90% | 양호 | 낮음 |
| Determinism (DK-1~5) | 95% | 안정 | 낮음 |
| Tervas (터미널 뷰어) | 95% | 안정 | 낮음 |
| PixelCode VM | 95% | 양호 | 중간 |
| PixelCode Parser | 80% | 양호 | 중간 |
| **Syscall 디스패치** | **5%** | **위험** | **치명** |
| **프로세스 관리** | **70%** | **미완** | **높음** |
| **Shell** | **50%** | **미완** | **높음** |
| **권한/보안 모델** | **0%** | **미구현** | **치명** |
| **VM Runtime Bridge** | **30%** | **미완** | **높음** |
| BH Compress (루프/폭주 감지) | 40% | 스텁 | 중간 |
| Branch/Merge (Phase 5) | 70% | 미완 | 중간 |
| GPU 추상화 | 스텁 | CPU폴백 | 낮음 |
| 네트워크 | 0% | 미구현 | — |

### 1.2 핵심 병목 3가지

```
1. Syscall 테이블: 62개 정의, 3개 구현 (4.8%)
   → Userland 전체가 사실상 동작 불가

2. VM ↔ 커널 브릿지: VM_SEND/RECV/SPAWN이 no-op
   → PixelCode 프로그램이 프로세스 생성/IPC 불가

3. 권한 모델 부재: lane_id=uid이지만 검증 없음
   → 모든 프로세스가 모든 자원에 접근 가능
```

---

## 2. Phase 11 목표

**한 줄 요약**: Syscall 완성 → VM 브릿지 연결 → 셸 강화 → 권한 적용

### 2.1 완료 기준

- [ ] Syscall 핸들러 30개 이상 동작
- [ ] VM에서 `SPAWN → SEND → RECV → EXIT` 파이프라인 동작
- [ ] Shell에서 `cmd1 | cmd2 > file` 동작
- [ ] `uid/gid` 기반 파일 접근 제어 동작
- [ ] 160 + 40 = 200개 이상 테스트 PASS

---

## 3. 서브시스템별 명세

### 3.1 Syscall 디스패치 완성

**파일**: `src/syscall.c`, `src/syscall_bindings.c`

현재 등록된 핸들러: `SYS_GETPID`, `SYS_TIME`, `SYS_TICK` (3개)

#### Tier-1: 필수 (프로세스 제어)
| 번호 | 이름 | 시그니처 | 구현 대상 |
|------|------|----------|-----------|
| 0x01 | SYS_SPAWN | (code_tile, energy) → pid | proc_spawn 연결 |
| 0x02 | SYS_EXIT | (exit_code) → void | proc_exit 연결 |
| 0x03 | SYS_WAIT | (pid) → exit_code | proc_wait 연결 (블로킹) |
| 0x04 | SYS_KILL | (pid, signal) → 0/-1 | signal_send 연결 |
| 0x05 | SYS_SIGNAL | (signum, handler) → 0/-1 | signal_register 연결 |
| 0x40 | SYS_GETPID | () → pid | ✅ 구현됨 |
| 0x42 | SYS_GETPPID | () → parent_pid | proc 테이블 참조 |

#### Tier-2: 필수 (파일 I/O)
| 번호 | 이름 | 시그니처 | 구현 대상 |
|------|------|----------|-----------|
| 0x10 | SYS_OPEN | (path, flags) → fd | fd_open 연결 |
| 0x11 | SYS_READ | (fd, buf, len) → n | fd_read 연결 |
| 0x12 | SYS_WRITE | (fd, buf, len) → n | fd_write 연결 |
| 0x13 | SYS_CLOSE | (fd) → 0/-1 | fd_close 연결 |
| 0x14 | SYS_SEEK | (fd, offset, whence) → pos | fd_seek 연결 |
| 0x15 | SYS_MKDIR | (path) → 0/-1 | cmd_mkdir 연결 |
| 0x16 | SYS_RM | (path) → 0/-1 | cmd_rm 연결 |

#### Tier-3: 필수 (IPC)
| 번호 | 이름 | 시그니처 | 구현 대상 |
|------|------|----------|-----------|
| 0x20 | SYS_PIPE | () → (read_fd, write_fd) | pipe_create 연결 |
| 0x21 | SYS_DUP | (old_fd) → new_fd | fd_dup 연결 |

#### Tier-4: CanvasOS 고유
| 번호 | 이름 | 시그니처 | 구현 대상 |
|------|------|----------|-----------|
| 0x30 | SYS_GATE_OPEN | (tile_id) → 0/-1 | gate_open 연결 |
| 0x31 | SYS_GATE_CLOSE | (tile_id) → 0/-1 | gate_close 연결 |
| 0x43 | SYS_TIME | () → tick | ✅ 구현됨 |
| 0x44 | SYS_HASH | () → canvas_hash | dk_canvas_hash 연결 |
| 0x50 | SYS_TIMEWARP | (tick) → 0/-1 | timeline_timewarp 연결 |
| 0x51 | SYS_DET_MODE | (on/off) → 0/-1 | det_set_all 연결 |
| 0x52 | SYS_SNAPSHOT | (name) → snap_id | timeline_snapshot 연결 |
| 0x53 | SYS_MPROTECT | (tile, perm) → 0/-1 | mprotect 연결 |

**구현 규칙**:
- 모든 핸들러는 WH 로그에 기록 (`wh_record_syscall`)
- 실패 시 음수 에러코드 반환 (`-EINVAL`, `-ENOENT`, `-EPERM`)
- DK 결정론 규칙 준수 (정수 연산만, 틱 경계에서만 커밋)

---

### 3.2 VM Runtime Bridge 완성

**파일**: `src/vm_runtime_bridge.c`, `src/vm.c`

#### 현재 문제
```c
// vm.c Line 234 — VM_SEND
if (vm_bridge_is_active()) {
    vm_exec_send(NULL, ctx, vm);   // 외부 함수 호출
} else {
    // WH-only fallback → 실제 데이터 전달 안됨
}
```

#### 구현 목표
| 함수 | 동작 |
|------|------|
| `vm_bridge_init(ctx, pt, pipes)` | 브릿지 활성화, 커널 구조체 연결 |
| `vm_bridge_is_active()` | 초기화 여부 반환 |
| `vm_exec_send(ctx, vm)` | VM의 R 레지스터 → pipe_write |
| `vm_exec_recv(ctx, vm)` | pipe_read → VM의 R 레지스터 |
| `vm_exec_spawn(ctx, vm)` | proc_spawn 호출 → VM에 child pid 반환 |

**검증 시나리오**: VM 프로그램이 자식 프로세스를 spawn하고, pipe로 데이터를 주고받은 후 exit.

---

### 3.3 Shell 강화

**파일**: `src/shell.c`

#### 3.3.1 파이프 실행 (현재 스텁)
```
# 목표: 아래가 실제로 동작해야 함
echo hello | cat
ls / | sort
ps | grep RUNNING
```

**구현**:
1. `|` 기호로 명령어 분리
2. `pipe_create()` 호출
3. 좌측 명령 stdout → pipe write fd
4. 우측 명령 stdin ← pipe read fd
5. 양쪽 실행 후 pipe 정리

#### 3.3.2 리다이렉션 (현재 스텁)
```
# 목표
echo hello > /data/test.txt
cat /data/test.txt >> /data/log.txt
sort < /data/input.txt
```

**구현**:
1. `>` / `>>` / `<` 파싱
2. fd_open으로 파일 열기
3. stdout/stdin을 fd로 리다이렉트
4. 명령 실행 후 fd 복원

#### 3.3.3 추가 내장 명령어
| 명령 | 동작 | 우선순위 |
|------|------|---------|
| `env` | 환경변수 목록 출력 | 높음 |
| `export VAR=val` | 환경변수 설정 + 자식 전달 | 높음 |
| `which cmd` | 명령어 위치 출력 | 중간 |
| `clear` | 화면 클리어 | 중간 |
| `head -n N` | 처음 N줄 출력 | 중간 |
| `tail -n N` | 마지막 N줄 출력 | 중간 |
| `wc` | 줄/단어/바이트 수 | 중간 |
| `grep PATTERN` | 패턴 매칭 | 높음 |
| `test / [` | 조건 평가 | 높음 |
| `sleep N` | N틱 대기 | 중간 |
| `alias` | 명령어 별칭 | 낮음 |
| `history` | 명령 이력 | 낮음 |

#### 3.3.4 스크립트 실행
```
# 목표: .cvs (CanvasOS Script) 파일 실행
source /scripts/boot.cvs
```
- 줄 단위 읽기 → `shell_exec_line` 호출
- `#` 주석 지원 (✅ 이미 구현)
- 조건문: `if ... then ... fi` (Phase 12 이후)

---

### 3.4 권한/보안 모델

**파일**: 신규 `src/permission.c`, `include/canvasos_permission.h`

#### 3.4.1 UID/GID 모델
```c
typedef struct {
    uint8_t uid;        // lane_id를 uid로 활용 (0=root, 1-254=user)
    uint8_t gid;        // 그룹 ID
    uint8_t mode;       // rwx 비트 (owner:group:other = 3:3:2 = 8bit)
} FilePermission;
```

#### 3.4.2 검증 지점
| 위치 | 검증 내용 |
|------|-----------|
| `fd_open()` | 파일 mode vs 프로세스 uid 확인 |
| `gate_open()` | 타일 소유권 vs 프로세스 uid 확인 |
| `proc_spawn()` | 부모 권한 상속 or 제한 |
| `proc_kill()` | uid=0 또는 소유자만 kill 가능 |
| `syscall dispatch` | uid=0만 SYS_MPROTECT, SYS_DET_MODE 허용 |

#### 3.4.3 Gate 권한 확장
```
현재: gate = OPEN / CLOSE (binary)
목표: gate = { owner_uid, perm_bits, state }
       perm_bits: READ | WRITE | EXEC | SHARED
```

---

### 3.5 프로세스 관리 완성

**파일**: `src/proc.c`, `src/signal.c`

#### 3.5.1 proc_wait 블로킹
```c
// 현재: 즉시 반환 (스텁)
// 목표: 자식이 EXIT할 때까지 부모를 SLEEPING 상태로 전환
int proc_wait(ProcTable *pt, uint32_t parent_pid, uint8_t *exit_code) {
    // 자식 중 ZOMBIE 상태 찾기
    // 없으면 parent 상태를 PROC_WAITING으로 설정
    // 다음 tick에서 자식 EXIT 시 parent를 깨움
}
```

#### 3.5.2 시그널 전달
```c
// 현재: signal_send 선언만 존재
// 목표: 실제 시그널 큐 + 핸들러 호출

#define SIG_CHILD   1   // 자식 종료 시 부모에게
#define SIG_KILL    9   // 즉시 종료 (마스크 불가)
#define SIG_STOP   17   // 정지
#define SIG_CONT   18   // 재개
#define SIG_TERM   15   // 정상 종료 요청
#define SIG_USR1    7   // 사용자 정의 1
#define SIG_USR2    8   // 사용자 정의 2

typedef void (*SignalHandler)(uint8_t signum);

// 프로세스별 시그널 핸들러 테이블
SignalHandler sig_handlers[SIG_MAX];
```

#### 3.5.3 proc_tick 강화
```c
// 현재: energy-- 만 수행
// 목표:
void proc_tick(ProcTable *pt, EngineContext *ctx) {
    for (int i = 0; i < PROC8_MAX; i++) {
        Proc8 *p = &pt->procs[i];
        if (p->state == PROC_RUN) {
            p->energy--;
            if (p->energy == 0) p->state = PROC_SLEEPING;
        }
        if (p->state == PROC_WAITING) {
            // 자식 ZOMBIE 스캔 → 있으면 깨우기
        }
        // 펜딩 시그널 처리
        process_pending_signals(pt, ctx, p);
    }
}
```

---

### 3.6 CanvasFS 강화

**파일**: `src/canvasfs.c`, `include/canvasfs.h`

#### 3.6.1 파일 크기 확장
```
현재: SMALL 슬롯 최대 224B
목표: LARGE 슬롯 체이닝으로 최대 57KB (256 × 224B)
구현: fs_write_large() — 여러 슬롯을 next_slot 포인터로 연결
```

#### 3.6.2 Atomic Rename
```c
int fs_rename(EngineContext *ctx, FsKey from, FsKey to);
// 1. to에 from의 슬롯 포인터 복사
// 2. from 디렉토리 엔트리 삭제
// 3. WH 로그에 단일 트랜잭션으로 기록
```

#### 3.6.3 파일 메타데이터
```c
typedef struct {
    uint32_t size;          // 파일 크기
    uint32_t created_tick;  // 생성 틱
    uint32_t modified_tick; // 수정 틱
    uint8_t  owner_uid;     // 소유자
    uint8_t  mode;          // rwx 비트
} FsMeta;
```

---

### 3.7 BH Compress 완성

**파일**: `src/canvas_bh_compress.c`

#### 3.7.1 루프 감지 (현재 `return 0`)
```c
// 목표: 최근 N틱의 WH 해시가 반복되면 루프로 판정
static int _detect_loop(const BhWindow *win) {
    // 윈도우 내 hash 패턴 반복 검출
    // 예: [A,B,A,B,A,B] → period=2, count=3 → LOOP
    uint32_t hashes[BH_WINDOW_SIZE];
    for (int period = 1; period <= BH_WINDOW_SIZE/2; period++) {
        if (is_repeating(hashes, win->count, period))
            return period;
    }
    return 0;
}
```

#### 3.7.2 폭주 감지 (현재 `return 0`)
```c
// 목표: 단위 시간 당 WH 기록 수가 임계값 초과
static int _detect_burst(const BhWindow *win) {
    int writes_per_tick = count_wh_records(win, last_tick);
    return (writes_per_tick > BH_BURST_THRESHOLD) ? writes_per_tick : 0;
}
#define BH_BURST_THRESHOLD 1024
```

---

### 3.8 Phase 5 잔여 TODO 해결

#### 3.8.1 Branch Switch: CR2 PageSelector 갱신
```c
// canvas_branch.c Line 42
// 현재: "Phase 5 TODO: ctx의 CR2 PageSelector 업데이트" → 미구현
// 목표: 브랜치 전환 시 CR2 타일의 PageSelector를 새 브랜치의 BPageTable로 교체
void branch_switch(BranchTable *bt, EngineContext *ctx, int branch_id) {
    // ... 기존 코드 ...
    // CR2 갱신
    uint32_t cr2_base = CR2_TILE * TILE_SIZE;
    BPageEntry *entries = bt->branches[branch_id].bpage_entries;
    memcpy(&ctx->cells[cr2_base], entries, sizeof(BPageEntry) * BPAGE_COUNT);
}
```

#### 3.8.2 Merge: Gate 충돌 해결
```c
// canvas_merge.c Line 104
// 현재: "CONFLICT_GATE_CLOSE: Phase 5 TODO" → 미구현
// 목표: 양쪽 브랜치에서 같은 gate를 다르게 변경한 경우 해결
case CONFLICT_GATE:
    // 규칙: CLOSE가 우선 (안전 원칙)
    result->cells[idx] = (a_open && !b_open) ? b_cell : a_cell;
    break;
```

#### 3.8.3 Merge: RuleTable 기반 커스텀 병합
```c
// canvas_merge.c Line 182
// 현재: 하드코딩된 병합 규칙
// 목표: RuleTable에서 opcode별 병합 전략 참조
MergeStrategy get_merge_strategy(const RuleTable *rt, uint8_t opcode) {
    return rt->strategies[opcode]; // OVERWRITE, KEEP_LOWER, KEEP_HIGHER, ERROR
}
```

---

## 4. 에러 핸들링 강화

### 4.1 malloc 실패 처리
```c
// 현재 (pixelcode.c Line 70):
Cell *tmp = malloc(w * h * sizeof(Cell));  // NULL 체크 없음

// 목표: 모든 malloc에 NULL 체크 + 에러 반환
Cell *tmp = malloc(w * h * sizeof(Cell));
if (!tmp) return -ENOMEM;
```

### 4.2 통합 에러 코드
```c
// include/canvasos_errno.h (신규)
#define COS_OK          0
#define COS_ENOMEM     -1   // 메모리 부족
#define COS_EINVAL     -2   // 잘못된 인자
#define COS_ENOENT     -3   // 파일/경로 없음
#define COS_EPERM      -4   // 권한 없음
#define COS_ENOSYS     -5   // 미구현 syscall
#define COS_EEXIST     -6   // 이미 존재
#define COS_EBUSY      -7   // 사용 중
#define COS_EIO        -8   // I/O 에러
#define COS_EFULL      -9   // 테이블 가득 참
#define COS_EPIPE     -10   // 파이프 깨짐
```

---

## 5. 테스트 계획

### 5.1 신규 테스트 (Patch-I: 40개)

| ID | 테스트 | 검증 항목 |
|----|--------|-----------|
| I-01 | syscall SYS_SPAWN → pid 반환 | 프로세스 생성 |
| I-02 | syscall SYS_EXIT → ZOMBIE 상태 | 프로세스 종료 |
| I-03 | syscall SYS_WAIT → exit_code 수신 | 블로킹 대기 |
| I-04 | syscall SYS_KILL → SIGKILL 전달 | 시그널 |
| I-05 | syscall SYS_SIGNAL → 핸들러 등록 | 시그널 핸들러 |
| I-06 | syscall SYS_OPEN → fd 반환 | 파일 열기 |
| I-07 | syscall SYS_READ → 데이터 반환 | 파일 읽기 |
| I-08 | syscall SYS_WRITE → 바이트 수 반환 | 파일 쓰기 |
| I-09 | syscall SYS_CLOSE → fd 해제 | 파일 닫기 |
| I-10 | syscall SYS_PIPE → read/write fd 쌍 | 파이프 생성 |
| I-11 | syscall SYS_GATE_OPEN → 권한 확인 | 게이트 + 보안 |
| I-12 | syscall SYS_TIMEWARP → 틱 복원 | 시간 여행 |
| I-13 | syscall SYS_SNAPSHOT → snap_id 반환 | 스냅샷 |
| I-14 | syscall SYS_HASH → 해시 반환 | 캔버스 해시 |
| I-15 | syscall SYS_GETPPID → 부모 pid | 프로세스 관계 |
| I-16 | vm_bridge SEND → pipe 데이터 전달 | VM 브릿지 |
| I-17 | vm_bridge RECV → pipe 데이터 수신 | VM 브릿지 |
| I-18 | vm_bridge SPAWN → 자식 프로세스 생성 | VM 브릿지 |
| I-19 | shell pipe: echo X \| cat → X 출력 | 셸 파이프 |
| I-20 | shell redir: echo X > file → 파일 생성 | 셸 리다이렉션 |
| I-21 | shell append: echo Y >> file → 추가 | 셸 추가 |
| I-22 | shell input: cat < file → 내용 출력 | 셸 입력 |
| I-23 | perm: uid=0 → 모든 파일 접근 가능 | root 권한 |
| I-24 | perm: uid=1 → 타인 파일 접근 거부 | 사용자 격리 |
| I-25 | perm: gate owner만 open 가능 | 게이트 권한 |
| I-26 | perm: non-root SYS_MPROTECT 거부 | 권한 제한 |
| I-27 | proc_wait 블로킹 → 자식 EXIT 시 깨움 | 블로킹 대기 |
| I-28 | SIGCHILD → 부모에게 전달 | 시그널 체인 |
| I-29 | SIGSTOP/SIGCONT → 정지/재개 | 프로세스 제어 |
| I-30 | proc_tick energy=0 → SLEEPING | 스케줄링 |
| I-31 | fs_rename atomic → 이름 변경 | 파일시스템 |
| I-32 | fs LARGE 슬롯 체이닝 → 224B+ 파일 | 대용량 파일 |
| I-33 | FsMeta 생성/수정 틱 기록 | 메타데이터 |
| I-34 | BH loop 감지 → period 반환 | 루프 감지 |
| I-35 | BH burst 감지 → threshold 초과 | 폭주 감지 |
| I-36 | branch_switch → CR2 갱신 확인 | 브랜치 |
| I-37 | merge gate 충돌 → CLOSE 우선 | 병합 |
| I-38 | malloc 실패 → COS_ENOMEM 반환 | 에러 핸들링 |
| I-39 | shell grep PATTERN → 매칭 라인 출력 | 유틸리티 |
| I-40 | 전체 통합: spawn→pipe→write→read→exit | E2E |

---

## 6. 제약 조건 (불변)

다음은 CanvasOS 설계 원칙으로 Phase 11에서도 변경 불가:

1. **DK-1**: Delta commit/merge는 틱 경계에서만
2. **DK-2**: 정수 연산만 (부동소수점 금지)
3. **DK-3**: 셀 인덱스 오름차순 병합
4. **DK-4**: 오버플로우 정수 클램프
5. **DK-5**: 노이즈 플로어 필터링
6. **R-4**: Tervas는 READ-ONLY (엔진 상태 수정 금지)
7. **캔버스 크기**: 1024×1024 = 1,048,576 셀 (8MB)
8. **셀 구조**: ABGR 8바이트 (변경 불가)
