# CanvasOS Phase-8 — Userland Completion

> **버전**: v1.0.1-p8 (목표)  
> **기반**: Phase-7 Stable (PASS 16/16)  
> **목표**: 리눅스 수준 기본 기능 + CanvasOS 태생적 특장점 특화

---

## 0. 설계 원칙

```
1. 기본은 다 한다 — 리눅스가 하는 것은 CanvasOS도 한다
2. 태생적 장점을 특화한다 — 시간여행, 공간실행, 8MB 결정론
3. 필요시 비결정적 모드 — DK 규약을 의도적으로 풀 수 있다
```

---

## 1. 리눅스 vs CanvasOS 대응표 (Phase-8 완료 조건)

| 리눅스 기능 | CanvasOS 대응 | Task | 상태 |
|-------------|---------------|------|------|
| fork/exec | `proc_spawn` + 코드 타일 로딩 | T8-01 | 🔲 |
| exit/wait | `proc_exit` + ZOMBIE 회수 | T8-01 | 🔲 |
| kill/signal | `proc_signal` (SIGKILL/STOP/CONT/USR1) | T8-02 | 🔲 |
| 가상 메모리 | TileGroup 보호 (게이트 기반 격리) | T8-03 | 🔲 |
| pipe | R채널 스트림 연결 (proc A.R → proc B.R) | T8-04 | 🔲 |
| 파일 연산 (open/read/write/close) | `fd_open` / `fd_read` / `fd_write` / `fd_close` | T8-05 | 🔲 |
| 디렉토리 (ls/cd/mkdir) | CanvasFS DIR1 경로 탐색 | T8-06 | 🔲 |
| 셸 (bash) | CanvasShell (런처 확장) | T8-07 | 🔲 |
| 시스템 콜 테이블 | `syscall_dispatch[]` (WH 옵코드 기반) | T8-08 | 🔲 |
| 사용자/권한 (uid/gid) | Lane = 사용자, Gate = 권한 | T8-09 | 🔲 |
| 유틸리티 (ps/top/cat/echo) | 내장 명령 + Tervas 연동 | T8-10 | 🔲 |

### 리눅스에 없는 CanvasOS 특장점

| 특장점 | Task | 설명 |
|--------|------|------|
| 시간여행 디버거 | T8-11 | `timewarp <tick>` — 과거 상태 복원 + 재실행 |
| 결정론/비결정론 토글 | T8-12 | `det on/off` — DK 규약 동적 해제/복원 |
| 공간 실행 시각화 | T8-13 | `watch` — 실시간 타일 실행 미니맵 |
| 8MB 전체 스냅샷 | (기존) | `save/load` — CVP로 전체 OS 상태 1파일 |
| WH 감사 로그 | (기존) | 모든 연산이 자동 기록 → 포렌식 |

---

## 2. Task 상세

### T8-01: 프로세스 모델 완성

**현재**: Scheduler에 PROC_MAX=64, spawn/kill 기본 동작.  
**목표**: 실제 코드 로딩 + 실행 격리 + parent-child 관계.

```
파일: include/canvasos_proc.h, src/proc.c

구조체:
  Process 확장:
    uint32_t  parent_pid;      // fork 계보
    uint16_t  code_tile;       // 코드가 적재된 타일 시작
    uint16_t  code_tiles;      // 코드 타일 수
    uint16_t  stack_tile;      // 스택 영역 타일
    uint8_t   exit_code;       // exit 코드
    uint8_t   lane_id;         // 소속 Lane (= uid 역할)

API:
  int  proc_spawn(ctx, parent_pid, code_tile, energy)
       → 자식 프로세스 생성
       → 부모의 게이트 공간에서 자식 공간 분리
       → WH에 SPAWN 레코드 기록
       → pid 반환

  int  proc_exec(ctx, pid, code_tile)
       → 기존 프로세스의 코드 타일 교체
       → 이전 코드 타일 해제
       → WH에 EXEC 레코드 기록

  int  proc_exit(ctx, pid, exit_code)
       → ZOMBIE 상태 전환
       → 자식 프로세스 → init(pid=0) 입양
       → 부모에게 SIGCHLD 시그널
       → 게이트 CLOSE

  int  proc_wait(ctx, parent_pid, &status)
       → ZOMBIE 자식 찾아서 회수
       → 슬롯 free

테스트:
  P8-T1: spawn → exit → wait → 슬롯 재사용 확인
  P8-T2: parent kill → 자식 전부 init 입양
  P8-T3: PROC_MAX 도달 → spawn 실패 → -1
```

### T8-02: 시그널 시스템

**현재**: 없음.  
**목표**: 리눅스 시그널 최소 호환.

```
파일: include/canvasos_signal.h, src/signal.c

시그널 종류:
  SIG_KILL  = 1    // 즉시 종료 (무시 불가)
  SIG_STOP  = 2    // 일시 정지 (에너지 동결)
  SIG_CONT  = 3    // 재개 (에너지 동결 해제)
  SIG_CHILD = 4    // 자식 종료 알림
  SIG_USR1  = 5    // 사용자 정의 1
  SIG_USR2  = 6    // 사용자 정의 2
  SIG_ALARM = 7    // tick 타이머 만료

구조:
  Process 확장:
    uint8_t   sig_pending;     // 비트마스크 (최대 8개)
    uint8_t   sig_mask;        // 블록된 시그널 마스크
    void     *sig_handler[8];  // 핸들러 (NULL = 기본동작)

API:
  int  sig_send(ctx, dst_pid, signal)
       → sig_pending에 비트 설정
       → WH에 SIG 레코드 기록

  int  sig_check(ctx, pid)
       → tick boundary에서 호출
       → pending & ~mask 확인
       → 핸들러 호출 또는 기본동작

  void sig_mask_set(ctx, pid, mask)
  void sig_mask_clear(ctx, pid, mask)

WH 옵코드 추가:
  WH_OP_SIG_SEND = 0x70

테스트:
  P8-T4: SIGKILL → 즉시 ZOMBIE
  P8-T5: SIGSTOP → SLEEPING, SIGCONT → RUNNING
  P8-T6: sig_mask → SIGKILL 블록 불가 확인
```

### T8-03: 메모리 보호 (TileGroup)

**현재**: 게이트 OPEN/CLOSE만.  
**목표**: 프로세스별 타일 소유권 + 접근 위반 탐지.

```
파일: include/canvasos_mprotect.h, src/mprotect.c

구조:
  uint16_t tile_owner[TILE_COUNT];  // 각 타일의 소유 pid (0=free)
  uint8_t  tile_perm[TILE_COUNT];   // 권한 비트

  PERM_READ   = 0x01
  PERM_WRITE  = 0x02
  PERM_EXEC   = 0x04
  PERM_SHARED = 0x08    // 다른 프로세스도 접근 가능

API:
  int  tile_alloc(ctx, pid, count)     // 연속 타일 할당
  void tile_free(ctx, pid, start, count)
  int  tile_check(ctx, pid, tile_id, perm)  // 접근 검사
  void tile_set_perm(ctx, pid, tile_id, perm)

  // 접근 위반 시:
  //   tile_check 실패 → SIG_SEGV를 프로세스에 전송
  //   WH에 FAULT 레코드 기록 (포렌식용)

WH 옵코드 추가:
  WH_OP_MPROTECT = 0x71
  WH_OP_FAULT    = 0x72

테스트:
  P8-T7: 타일 할당 → 소유 확인
  P8-T8: 다른 pid가 접근 → FAULT
  P8-T9: PERM_SHARED → 접근 허용
```

### T8-04: 파이프 (R채널 스트림)

**현재**: IPC 스텁만.  
**목표**: 프로세스 간 단방향 데이터 스트림.

```
파일: include/canvasos_pipe.h, src/pipe.c

원리:
  파이프 = CanvasFS 슬롯 1개를 원형 버퍼로 사용
  writer가 R채널에 쓰면 → reader가 R채널에서 읽음
  게이트 기반: writer 게이트 OPEN = 쓰기 가능
                reader 게이트 OPEN = 읽기 가능

구조:
  typedef struct {
      FsKey     slot;           // CanvasFS 슬롯 (버퍼)
      uint32_t  writer_pid;
      uint32_t  reader_pid;
      uint16_t  write_cursor;   // 원형 버퍼 쓰기 위치
      uint16_t  read_cursor;    // 원형 버퍼 읽기 위치
      uint16_t  capacity;       // 224 bytes (SMALL 슬롯)
      bool      closed;
  } Pipe;

  #define PIPE_MAX 64

API:
  int  pipe_create(ctx, writer_pid, reader_pid, &pipe_id)
  int  pipe_write(ctx, pipe_id, data, len)
       → 버퍼 가득 → writer BLOCKED
  int  pipe_read(ctx, pipe_id, buf, len)
       → 버퍼 비어있음 → reader BLOCKED
  void pipe_close(ctx, pipe_id)

셸 연동:
  "cmd1 | cmd2" → pipe_create(cmd1, cmd2) → cmd1.R 출력 → pipe → cmd2.R 입력

WH 옵코드 추가:
  WH_OP_PIPE_WRITE = 0x73
  WH_OP_PIPE_READ  = 0x74

테스트:
  P8-T10: write→read 데이터 일치
  P8-T11: 버퍼 가득 → writer BLOCKED → read 후 깨어남
  P8-T12: close → EOF 전달
```

### T8-05: 파일 디스크립터 (fd)

**현재**: CanvasFS는 FsKey로 직접 접근.  
**목표**: 리눅스 스타일 fd 추상화.

```
파일: include/canvasos_fd.h, src/fd.c

구조:
  typedef struct {
      FsKey    key;          // CanvasFS 슬롯
      uint16_t cursor;       // 읽기/쓰기 위치
      uint8_t  flags;        // O_READ | O_WRITE | O_APPEND
      uint8_t  type;         // FD_FILE | FD_PIPE | FD_DEVICE
  } FileDesc;

  #define FD_MAX_PER_PROC 16

  // 프로세스별 fd 테이블
  FileDesc proc_fds[PROC_MAX][FD_MAX_PER_PROC];

  // 예약 fd:
  //   fd=0 : stdin  (파이프 또는 키보드)
  //   fd=1 : stdout (파이프 또는 터미널)
  //   fd=2 : stderr (터미널)

API:
  int  fd_open(ctx, pid, path, flags)   → fd 번호
  int  fd_read(ctx, pid, fd, buf, len)  → 읽은 바이트
  int  fd_write(ctx, pid, fd, buf, len) → 쓴 바이트
  int  fd_close(ctx, pid, fd)
  int  fd_seek(ctx, pid, fd, offset)
  int  fd_dup(ctx, pid, old_fd, new_fd)  → 파이프 리다이렉션

테스트:
  P8-T13: open → write → seek(0) → read → 데이터 일치
  P8-T14: fd_dup(stdout→pipe) → 리다이렉션
  P8-T15: FD_MAX 초과 → 에러
```

### T8-06: 경로 탐색 (Path Resolution)

**현재**: CanvasFS DIR1에 name_hash → FsKey 매핑만.  
**목표**: `/dir/subdir/file` 경로 탐색.

```
파일: include/canvasos_path.h, src/path.c

구조:
  // 경로 = "/" 구분 문자열
  // 각 단계: DIR1 엔트리에서 name_hash로 탐색
  // 루트 = pid=0의 VOLH

  typedef struct {
      uint32_t  pid;          // cwd 소유 프로세스
      FsKey     cwd;          // current working directory 슬롯
  } PathContext;

API:
  int  path_resolve(ctx, pctx, path_str, &out_key)
       → "/"로 분할
       → 각 단계마다 DIR1 lookup
       → 최종 FsKey 반환

  int  path_mkdir(ctx, pctx, name)
  int  path_ls(ctx, pctx, dir_key, entries[], max)
  int  path_cd(ctx, pctx, path_str)
  int  path_rm(ctx, pctx, path_str)

  // 특수 경로:
  //   "."   = cwd
  //   ".."  = parent (DIR1 첫 엔트리에 parent_key 저장)
  //   "/"   = root

테스트:
  P8-T16: mkdir → cd → ls → 엔트리 확인
  P8-T17: 중첩 경로 /a/b/c → resolve 성공
  P8-T18: 존재하지 않는 경로 → 에러
```

### T8-07: CanvasShell (셸)

**현재**: 런처에 SJ-PTL + Tervas 통합.  
**목표**: 파이프, 리다이렉션, 변수, 내장 명령을 가진 진짜 셸.

```
파일: include/canvasos_shell.h, src/shell.c

셸 문법:
  단순 실행:    cmd arg1 arg2
  파이프:       cmd1 | cmd2 | cmd3
  리다이렉션:   cmd > file    cmd >> file    cmd < file
  변수:         $VAR=value    echo $VAR
  스크립트:     source file.sh

  // 셸은 프로세스 pid=1 (init의 자식)
  // 각 명령 = proc_spawn → proc_exec → proc_wait

내장 명령 (T8-10과 연동):
  echo TEXT         → stdout에 출력
  cat FILE          → 파일 내용 출력
  ls [DIR]          → 디렉토리 열거
  cd DIR            → cwd 변경
  mkdir DIR         → 디렉토리 생성
  rm FILE           → 파일 삭제
  cp SRC DST        → 파일 복사
  ps                → 프로세스 목록
  kill PID [SIG]    → 시그널 전송
  top               → Tervas 실시간 뷰
  hash              → 캔버스 해시
  save / load       → CVP 저장/로드
  timewarp TICK     → 시간여행 (T8-11)
  det on/off        → 결정론 토글 (T8-12)
  watch             → 실시간 미니맵 (T8-13)
  exit [CODE]       → 셸 종료

테스트:
  P8-T19: echo hello | cat → "hello" 출력
  P8-T20: echo data > file → cat file → "data"
  P8-T21: $VAR=world → echo hello $VAR → "hello world"
```

### T8-08: 시스템 콜 테이블

**현재**: WH 옵코드가 흩어져 있음.  
**목표**: 정식 syscall 번호 체계.

```
파일: include/canvasos_syscall.h, src/syscall.c

테이블:
  #define SYS_SPAWN     0x01
  #define SYS_EXIT      0x02
  #define SYS_WAIT      0x03
  #define SYS_KILL      0x04
  #define SYS_SIGNAL    0x05
  #define SYS_OPEN      0x10
  #define SYS_READ      0x11
  #define SYS_WRITE     0x12
  #define SYS_CLOSE     0x13
  #define SYS_SEEK      0x14
  #define SYS_MKDIR     0x15
  #define SYS_LS        0x16
  #define SYS_RM        0x17
  #define SYS_PIPE      0x20
  #define SYS_DUP       0x21
  #define SYS_GATE_OPEN 0x30
  #define SYS_GATE_CLOSE 0x31
  #define SYS_MPROTECT  0x32
  #define SYS_TICK      0x40
  #define SYS_GETPID    0x41
  #define SYS_GETPPID   0x42
  #define SYS_TIME      0x43    // 현재 tick
  #define SYS_HASH      0x44    // 캔버스 해시
  #define SYS_TIMEWARP  0x50    // 시간여행 (특장점)
  #define SYS_DET_MODE  0x51    // 결정론 토글 (특장점)
  #define SYS_SNAPSHOT  0x52    // Tervas 스냅샷 (특장점)

  // 진입점:
  int syscall(EngineContext *ctx, uint32_t pid, uint8_t syscall_nr,
              uint32_t arg0, uint32_t arg1, uint32_t arg2);
  // → WH에 SYSCALL 레코드 기록
  // → dispatch 테이블에서 핸들러 호출

WH 옵코드 추가:
  WH_OP_SYSCALL = 0x80

테스트:
  P8-T22: syscall(SYS_GETPID) → 현재 pid 반환
  P8-T23: syscall(SYS_WRITE, fd, data, len) → fd_write 호출
  P8-T24: 미등록 syscall 번호 → -ENOSYS
```

### T8-09: 사용자/권한 (Lane = User)

**현재**: Lane은 실행 분리 단위.  
**목표**: Lane = 사용자 ID, Gate = 권한.

```
파일: include/canvasos_user.h, src/user.c

매핑:
  Lane 0 = root (시스템, 모든 타일 접근)
  Lane 1 = user1
  Lane 2 = user2
  ...
  Lane 255 = broadcast

구조:
  typedef struct {
      uint8_t   lane_id;       // = uid
      char      name[16];      // 사용자 이름
      uint16_t  home_tile;     // 홈 디렉터리 타일
      uint8_t   priv;          // PRIV_ROOT | PRIV_USER | PRIV_GUEST
  } User;

  #define PRIV_ROOT   0x80
  #define PRIV_USER   0x40
  #define PRIV_GUEST  0x20

API:
  int  user_create(ctx, lane_id, name, priv)
  int  user_check_perm(ctx, uid, tile_id, perm)
       → tile_owner[tile_id]의 lane_id 확인
       → root는 항상 통과
  int  user_su(ctx, pid, target_lane)
       → root만 가능

테스트:
  P8-T25: user1이 user2 타일 접근 → FAULT
  P8-T26: root가 모든 타일 접근 → OK
  P8-T27: su root → lane 전환
```

### T8-10: 유틸리티 명령 (셸 내장)

**현재**: SJ-PTL + Tervas 분리.  
**목표**: 리눅스 수준 유틸리티.

```
파일: include/canvasos_utils.h, src/utils.c

명령 구현:
  // 프로세스 관련
  cmd_ps(ctx)        → PID STATE ENERGY LANE TILES 목록
  cmd_top(ctx)       → Tervas 연동 실시간 갱신
  cmd_kill(ctx, pid, sig)

  // 파일 관련
  cmd_cat(ctx, pid, path)    → fd_open → fd_read → stdout
  cmd_echo(ctx, pid, text)   → stdout 또는 fd_write
  cmd_ls(ctx, pid, path)     → path_ls → 포맷팅
  cmd_cd(ctx, pid, path)
  cmd_mkdir(ctx, pid, path)
  cmd_rm(ctx, pid, path)
  cmd_cp(ctx, pid, src, dst)

  // 시스템 관련
  cmd_hash(ctx)              → dk_canvas_hash 출력
  cmd_info(ctx)              → tick, 프로세스 수, 게이트, WH/BH 상태
  cmd_save(ctx, path)
  cmd_load(ctx, path)

테스트:
  P8-T28: echo hello > file → cat file → "hello"
  P8-T29: ps → 최소 2개 프로세스 (init + shell)
  P8-T30: kill → 프로세스 제거 확인
```

---

## 3. CanvasOS 특장점 특화

### T8-11: 시간여행 디버거 (TimeWarp)

```
파일: include/canvasos_timewarp.h, src/timewarp.c

원리:
  1. CVP로 현재 상태 백업 (자동)
  2. WH ring에서 target_tick까지 되감기
  3. replay(target_tick, current_tick)
  4. Tervas로 과거 상태 시각화
  5. "resume" → 백업에서 복원

API:
  int  timewarp_goto(ctx, target_tick)
       → 자동 CVP 백업
       → WH 기반 상태 재구성
       → Tervas 스냅샷 갱신

  int  timewarp_resume(ctx)
       → CVP 백업에서 복원

  int  timewarp_step(ctx, n_ticks)
       → target_tick에서 n_ticks만큼 재실행
       → 중간 상태를 Tervas로 관찰

셸 명령:
  timewarp 100        → tick 100으로 이동
  timewarp +5         → 현재에서 5틱 전
  timewarp step 3     → 3틱 진행 관찰
  timewarp resume     → 현재로 복귀
  timewarp diff 100   → tick 100과 현재의 차이 (변경된 셀 수)

테스트:
  P8-T31: timewarp → 과거 해시 = 과거 시점 실제 해시
  P8-T32: resume → 원래 해시 복원
  P8-T33: step → 중간 상태 결정론 확인
```

### T8-12: 결정론/비결정론 토글

```
파일: include/canvasos_detmode.h, src/detmode.c

원리:
  기본: DK-1~5 전부 적용 (결정론)
  det off: 규약 해제 → 랜덤, float, 비순차 허용
  det on: 규약 복원

  비결정론 모드 활용:
    - AI/ML 추론 (float 필요)
    - 랜덤 시뮬레이션
    - 외부 데이터 직접 주입 (inject 우회)
    - 성능 최적화 (순서 자유)

구조:
  typedef struct {
      bool dk1_tick_boundary;    // true = 강제
      bool dk2_integer_only;
      bool dk3_fixed_order;
      bool dk4_normalize;
      bool dk5_noise_absorb;
      bool wh_recording;         // false = WH 기록 중단
      uint32_t nondet_since;     // 비결정론 시작 tick
  } DetMode;

API:
  void det_set_mode(ctx, DetMode *mode)
  void det_reset(ctx)            → 전부 true (결정론 복원)
  bool det_is_deterministic(ctx) → 전부 true인지 확인

  // 비결정론 전환 시:
  //   WH에 DET_MODE_CHANGE 레코드 기록 (감사용)
  //   이후 replay 불가 경고
  //   timewarp도 nondet 구간 건너뜀

WH 옵코드 추가:
  WH_OP_DET_MODE = 0x81

셸 명령:
  det on              → 결정론 복원
  det off             → 전체 해제
  det off dk2         → DK-2만 해제 (float 허용)
  det status          → 현재 모드 표시

테스트:
  P8-T34: det off → float 연산 → crash 없음
  P8-T35: det on → 해시 결정론 복원
  P8-T36: 비결정론 구간 → timewarp 건너뜀 확인
```

### T8-13: 공간 실행 시각화 (Watch)

```
파일: include/canvasos_watch.h, src/watch.c

원리:
  Tervas + 셸 통합
  매 tick마다 미니맵 자동 갱신
  프로세스별 색상 구분
  WH 기록 실시간 표시

API:
  void watch_start(ctx, refresh_ms)
  void watch_stop(ctx)
  void watch_set_filter(ctx, TvProjectionMode mode)

셸 명령:
  watch              → 기본 미니맵 자동 갱신 (500ms)
  watch wh           → WH만 관찰
  watch proc 3       → pid=3의 타일만 강조
  watch stop         → 관찰 중지

테스트:
  P8-T37: watch 시작/정지
  P8-T38: 필터 전환
```

---

## 4. 의존성 그래프

```
T8-01 (프로세스) ──┬──► T8-02 (시그널)
                   ├──► T8-03 (메모리 보호)
                   ├──► T8-04 (파이프)
                   └──► T8-09 (사용자/권한)

T8-05 (fd) ────────┬──► T8-06 (경로 탐색)
                   └──► T8-04 (파이프: fd로 감싸기)

T8-01 + T8-04 + T8-05 + T8-06 ──► T8-07 (셸)

T8-08 (syscall) ◄── 모든 API를 syscall 번호로 등록

T8-10 (유틸리티) ◄── T8-07 (셸 내장 명령)

T8-11 (시간여행) ◄── 기존 WH + CVP
T8-12 (비결정론)  ◄── 기존 DK 규약
T8-13 (시각화)   ◄── 기존 Tervas
```

**구현 순서 (권장)**:

```
Round 1: T8-01 → T8-03 → T8-02        (프로세스 기반)
Round 2: T8-05 → T8-06 → T8-04        (파일/파이프)
Round 3: T8-08 → T8-09                 (시스템 레이어)
Round 4: T8-07 → T8-10                 (셸 + 유틸리티)
Round 5: T8-11 → T8-12 → T8-13        (특장점)
```

---

## 5. Phase-8 완료 조건

```bash
make test_all          # P6 PASS:6 + P7 PASS:10 (회귀 없음)
make test_phase8       # P8 PASS:38 / FAIL:0
make test_all WORKERS=4 # 멀티스레드 결정론 유지

# 셸 통합 테스트:
echo "echo hello | cat" | ./canvasos_launcher   # → "hello"
echo "ps" | ./canvasos_launcher                  # → init + shell 표시
echo "timewarp 0" | ./canvasos_launcher          # → tick 0 상태 복원
echo "det status" | ./canvasos_launcher           # → deterministic: YES
```

---

## 6. 새 WH 옵코드 전체 (Phase-8 추가분)

| Code | 이름 | 설명 |
|------|------|------|
| 0x70 | WH_OP_SIG_SEND | 시그널 전송 |
| 0x71 | WH_OP_MPROTECT | 타일 권한 변경 |
| 0x72 | WH_OP_FAULT | 접근 위반 기록 |
| 0x73 | WH_OP_PIPE_WRITE | 파이프 쓰기 |
| 0x74 | WH_OP_PIPE_READ | 파이프 읽기 |
| 0x75 | WH_OP_PROC_SPAWN | 프로세스 생성 |
| 0x76 | WH_OP_PROC_EXIT | 프로세스 종료 |
| 0x77 | WH_OP_PROC_EXEC | 코드 교체 |
| 0x80 | WH_OP_SYSCALL | 시스템 콜 기록 |
| 0x81 | WH_OP_DET_MODE | 결정론 모드 변경 |

---

## 7. 파일 목록 (신규)

```
include/
  canvasos_proc.h         프로세스 모델
  canvasos_signal.h       시그널
  canvasos_mprotect.h     메모리 보호
  canvasos_pipe.h         파이프
  canvasos_fd.h           파일 디스크립터
  canvasos_path.h         경로 탐색
  canvasos_shell.h        셸
  canvasos_syscall.h      시스템 콜 테이블
  canvasos_user.h         사용자/권한
  canvasos_utils.h        유틸리티 명령
  canvasos_timewarp.h     시간여행
  canvasos_detmode.h      결정론 토글
  canvasos_watch.h        실시간 시각화

src/
  proc.c
  signal.c
  mprotect.c
  pipe.c
  fd.c
  path.c
  shell.c
  syscall.c
  user.c
  utils.c
  timewarp.c
  detmode.c
  watch.c

tests/
  test_phase8.c           P8 테스트 (38개)
```

---

*CanvasOS Phase-8 · Userland TODO · v1.0.1-p8 · 2026-03-08*
