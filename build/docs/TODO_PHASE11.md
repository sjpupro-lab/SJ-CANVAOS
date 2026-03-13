# CanvasOS Phase 11 — TODO 개발 계획서

**기준 문서**: `SPEC_PHASE11_OS_CORE.md v0.1`
**작성일**: 2026-03-10
**진행 방식**: 스프린트 단위, 의존성 순서 준수

---

## 스프린트 구조

```
Sprint 1 (기반)   → 에러 코드 + Syscall Tier-1 + 프로세스 관리
Sprint 2 (I/O)    → Syscall Tier-2 + CanvasFS 강화
Sprint 3 (연결)   → VM Bridge + Syscall Tier-3 (IPC)
Sprint 4 (셸)     → Shell 파이프/리다이렉션/명령어
Sprint 5 (보안)   → 권한 모델 + Syscall Tier-4
Sprint 6 (안정화) → BH Compress + Phase 5 TODO + 통합 테스트
```

---

## Sprint 1: 기반 (에러 코드 + 프로세스 + Syscall Tier-1)

**의존성**: 없음 (최우선)
**목표**: 프로세스가 생성-실행-종료-대기 사이클을 완성

### 1.1 에러 코드 프레임워크
- [ ] `include/canvasos_errno.h` 신규 생성
  - COS_OK(0) ~ COS_EPIPE(-10) 정의
- [ ] 기존 `-38` (ENOSYS) 반환을 `COS_ENOSYS(-5)`로 교체
- [ ] `syscall.c` — 미등록 syscall 에러코드 통일

### 1.2 프로세스 관리 완성
- [ ] `proc.c` — `proc_wait()` 블로킹 구현
  - ZOMBIE 자식 스캔 → 없으면 parent를 PROC_WAITING으로 전환
- [ ] `signal.c` — 시그널 체계 구현
  - SIG 상수 정의 (KILL=9, STOP=17, CONT=18, TERM=15, CHILD=1, USR1=7, USR2=8)
  - `signal_send()` — 시그널 큐에 삽입
  - `signal_register()` — 프로세스별 핸들러 등록
  - `process_pending_signals()` — 틱마다 펜딩 시그널 처리
- [ ] `proc.c` — `proc_tick()` 강화
  - energy=0 → SLEEPING 전환
  - PROC_WAITING 상태에서 자식 ZOMBIE 감지 → 깨움
  - 펜딩 시그널 처리 호출

### 1.3 Syscall Tier-1 (프로세스 제어)
- [ ] `syscall.c` — SYS_SPAWN(0x01) 핸들러 → `proc_spawn` 연결
- [ ] `syscall.c` — SYS_EXIT(0x02) 핸들러 → `proc_exit` 연결
- [ ] `syscall.c` — SYS_WAIT(0x03) 핸들러 → `proc_wait` 연결
- [ ] `syscall.c` — SYS_KILL(0x04) 핸들러 → `signal_send` 연결
- [ ] `syscall.c` — SYS_SIGNAL(0x05) 핸들러 → `signal_register` 연결
- [ ] `syscall.c` — SYS_GETPPID(0x42) 핸들러 → proc 테이블 참조
- [ ] 모든 핸들러에 `wh_record_syscall` 호출 추가

### 1.4 Sprint 1 테스트
- [ ] I-01: SYS_SPAWN → pid 반환
- [ ] I-02: SYS_EXIT → ZOMBIE 상태
- [ ] I-03: SYS_WAIT → exit_code 수신
- [ ] I-04: SYS_KILL → SIGKILL 전달
- [ ] I-05: SYS_SIGNAL → 핸들러 등록 및 호출
- [ ] I-15: SYS_GETPPID → 부모 pid
- [ ] I-27: proc_wait 블로킹 → 자식 EXIT 시 깨움
- [ ] I-28: SIGCHILD → 부모에게 전달
- [ ] I-29: SIGSTOP/SIGCONT → 정지/재개
- [ ] I-30: proc_tick energy=0 → SLEEPING

**완료 기준**: `make test_all` + Sprint 1 테스트 10개 PASS

---

## Sprint 2: I/O (Syscall Tier-2 + CanvasFS)

**의존성**: Sprint 1 (에러 코드)
**목표**: 파일을 열고-읽고-쓰고-닫는 전체 사이클 동작

### 2.1 Syscall Tier-2 (파일 I/O)
- [ ] SYS_OPEN(0x10) → `fd_open` 연결
- [ ] SYS_READ(0x11) → `fd_read` 연결
- [ ] SYS_WRITE(0x12) → `fd_write` 연결
- [ ] SYS_CLOSE(0x13) → `fd_close` 연결
- [ ] SYS_SEEK(0x14) → `fd_seek` 연결
- [ ] SYS_MKDIR(0x15) → `cmd_mkdir` 연결
- [ ] SYS_RM(0x16) → `cmd_rm` 연결
- [ ] SYS_LS(0x17) → `cmd_ls` 연결

### 2.2 CanvasFS 강화
- [ ] `canvasfs.c` — LARGE 슬롯 체이닝 구현 (`fs_write_large`)
  - 여러 슬롯을 next_slot 포인터로 연결
  - 최대 57KB (256 × 224B)
- [ ] `canvasfs.c` — `fs_rename()` atomic 구현
  - to에 from 슬롯 복사 → from 삭제 → WH 단일 트랜잭션
- [ ] `canvasfs.h` / `canvasfs.c` — `FsMeta` 구조체 추가
  - size, created_tick, modified_tick, owner_uid, mode
- [ ] 기존 fs 함수들에 FsMeta 갱신 코드 삽입

### 2.3 Sprint 2 테스트
- [ ] I-06: SYS_OPEN → fd 반환
- [ ] I-07: SYS_READ → 데이터 반환
- [ ] I-08: SYS_WRITE → 바이트 수 반환
- [ ] I-09: SYS_CLOSE → fd 해제
- [ ] I-31: fs_rename → 이름 변경
- [ ] I-32: fs LARGE 슬롯 체이닝 → 224B+ 파일
- [ ] I-33: FsMeta 생성/수정 틱 기록

**완료 기준**: Sprint 2 테스트 7개 PASS

---

## Sprint 3: 연결 (VM Runtime Bridge + IPC)

**의존성**: Sprint 1 (프로세스), Sprint 2 (파일 I/O)
**목표**: VM 프로그램이 프로세스 생성하고 IPC로 통신

### 3.1 VM Runtime Bridge
- [ ] `vm_runtime_bridge.c` — `vm_bridge_init(ctx, pt, pipes)` 구현
  - 커널 구조체 포인터 저장, 활성화 플래그 세팅
- [ ] `vm_runtime_bridge.c` — `vm_exec_send(ctx, vm)` 구현
  - VM R 레지스터 → pipe_write
- [ ] `vm_runtime_bridge.c` — `vm_exec_recv(ctx, vm)` 구현
  - pipe_read → VM R 레지스터
- [ ] `vm_runtime_bridge.c` — `vm_exec_spawn(ctx, vm)` 구현
  - proc_spawn 호출 → child pid를 VM에 반환
- [ ] `canvasos_launcher.c` / `shell.c` — 부팅 시 `vm_bridge_init` 호출 추가

### 3.2 Syscall Tier-3 (IPC)
- [ ] SYS_PIPE(0x20) → `pipe_create` 연결 (read_fd, write_fd 반환)
- [ ] SYS_DUP(0x21) → `fd_dup` 연결

### 3.3 Sprint 3 테스트
- [ ] I-10: SYS_PIPE → read/write fd 쌍
- [ ] I-16: vm_bridge SEND → pipe 데이터 전달
- [ ] I-17: vm_bridge RECV → pipe 데이터 수신
- [ ] I-18: vm_bridge SPAWN → 자식 프로세스 생성

**완료 기준**: Sprint 3 테스트 4개 PASS + VM spawn→send→recv→exit 시나리오 동작

---

## Sprint 4: 셸 (파이프/리다이렉션/명령어)

**의존성**: Sprint 2 (파일 I/O), Sprint 3 (IPC/파이프)
**목표**: `cmd1 | cmd2 > file` 동작

### 4.1 Shell 파이프 실행
- [ ] `shell.c` — `shell_exec_pipe()` 완성
  - `|` 분리 → pipe_create → 좌측 stdout→write fd, 우측 stdin←read fd
  - 다중 파이프 지원: `a | b | c`

### 4.2 Shell 리다이렉션
- [ ] `shell.c` — `shell_exec_redir()` 완성
  - `>` 파싱 → fd_open(CREATE|TRUNC) → stdout 리다이렉트
  - `>>` 파싱 → fd_open(CREATE|APPEND) → stdout 리다이렉트
  - `<` 파싱 → fd_open(READ) → stdin 리다이렉트
  - 실행 후 fd 복원

### 4.3 추가 내장 명령어
- [ ] `export VAR=val` — 환경변수 설정 + 자식 전달
- [ ] `which cmd` — 명령어 위치 출력
- [ ] `clear` — 화면 클리어
- [ ] `grep PATTERN` — 패턴 매칭 (stdin 또는 파일)
- [ ] `head -n N` — 처음 N줄 출력
- [ ] `tail -n N` — 마지막 N줄 출력
- [ ] `wc` — 줄/단어/바이트 수
- [ ] `test / [` — 조건 평가 (파일 존재, 문자열 비교, 수치 비교)
- [ ] `sleep N` — N틱 대기
- [ ] `alias name=cmd` — 명령어 별칭
- [ ] `history` — 명령 이력
- [ ] `source FILE` — .cvs 스크립트 실행

### 4.4 Sprint 4 테스트
- [ ] I-19: shell pipe `echo X | cat` → X 출력
- [ ] I-20: shell redir `echo X > file` → 파일 생성
- [ ] I-21: shell append `echo Y >> file` → 추가
- [ ] I-22: shell input `cat < file` → 내용 출력
- [ ] I-39: shell grep PATTERN → 매칭 라인 출력

**완료 기준**: Sprint 4 테스트 5개 PASS

---

## Sprint 5: 보안 (권한 모델 + Syscall Tier-4)

**의존성**: Sprint 2 (파일 I/O + FsMeta)
**목표**: uid/gid 기반 접근 제어, root 권한 분리

### 5.1 권한 모델 구현
- [ ] `include/canvasos_permission.h` 신규 생성
  - FilePermission 구조체 (uid, gid, mode)
  - 권한 상수: PERM_READ, PERM_WRITE, PERM_EXEC
  - `perm_check(uid, gid, mode, requested)` 선언
- [ ] `src/permission.c` 신규 생성
  - `perm_check()` — mode 비트 해석, owner/group/other 분기
  - `perm_is_root(uid)` — uid==0 확인

### 5.2 검증 지점 삽입
- [ ] `fd_open()` — 파일 mode vs 프로세스 uid 검증
- [ ] `gate_open()` — 타일 소유권 vs 프로세스 uid 검증
- [ ] `proc_spawn()` — 부모 권한 상속 or 제한 적용
- [ ] `proc_kill()` — uid=0 또는 소유자만 kill 가능
- [ ] syscall dispatch — SYS_MPROTECT, SYS_DET_MODE는 uid=0만 허용

### 5.3 Gate 권한 확장
- [ ] Gate 구조체에 `owner_uid`, `perm_bits` 필드 추가
  - perm_bits: READ | WRITE | EXEC | SHARED
- [ ] `gate_open()` / `gate_close()` — 권한 체크 로직 삽입

### 5.4 Syscall Tier-4 (CanvasOS 고유)
- [ ] SYS_GATE_OPEN(0x30) → `gate_open` + 권한 검증
- [ ] SYS_GATE_CLOSE(0x31) → `gate_close` + 권한 검증
- [ ] SYS_HASH(0x44) → `dk_canvas_hash` 연결
- [ ] SYS_TIMEWARP(0x50) → `timeline_timewarp` 연결
- [ ] SYS_DET_MODE(0x51) → `det_set_all` 연결 (root only)
- [ ] SYS_SNAPSHOT(0x52) → `timeline_snapshot` 연결
- [ ] SYS_MPROTECT(0x53) → `mprotect` 연결 (root only)

### 5.5 Sprint 5 테스트
- [ ] I-11: SYS_GATE_OPEN → 권한 확인
- [ ] I-12: SYS_TIMEWARP → 틱 복원
- [ ] I-13: SYS_SNAPSHOT → snap_id 반환
- [ ] I-14: SYS_HASH → 해시 반환
- [ ] I-23: uid=0 → 모든 파일 접근 가능
- [ ] I-24: uid=1 → 타인 파일 접근 거부
- [ ] I-25: gate owner만 open 가능
- [ ] I-26: non-root SYS_MPROTECT 거부

**완료 기준**: Sprint 5 테스트 8개 PASS

---

## Sprint 6: 안정화 (BH + Phase 5 + 통합)

**의존성**: Sprint 1~5 전체
**목표**: 모든 스텁 해소, 통합 테스트 PASS, 200개 이상 테스트

### 6.1 BH Compress 완성
- [ ] `canvas_bh_compress.c` — `_detect_loop()` 구현
  - 윈도우 내 해시 패턴 반복 검출 (period 기반)
- [ ] `canvas_bh_compress.c` — `_detect_burst()` 구현
  - 틱 당 WH 기록 수 > BH_BURST_THRESHOLD(1024) 감지

### 6.2 Phase 5 잔여 TODO
- [ ] `canvas_branch.c` — `branch_switch()` CR2 PageSelector 갱신
  - 브랜치 전환 시 CR2 타일의 BPageEntry를 새 브랜치로 교체
- [ ] `canvas_merge.c` — Gate 충돌 해결 (CLOSE 우선 규칙)
- [ ] `canvas_merge.c` — RuleTable 기반 커스텀 병합
  - MergeStrategy: OVERWRITE, KEEP_LOWER, KEEP_HIGHER, ERROR

### 6.3 malloc 실패 처리
- [ ] `pixelcode.c` — 모든 malloc에 NULL 체크 추가
- [ ] `canvasfs.c` — 모든 malloc에 NULL 체크 추가
- [ ] 기타 동적 할당 코드 — NULL 체크 감사

### 6.4 Sprint 6 테스트
- [ ] I-34: BH loop 감지 → period 반환
- [ ] I-35: BH burst 감지 → threshold 초과
- [ ] I-36: branch_switch → CR2 갱신 확인
- [ ] I-37: merge gate 충돌 → CLOSE 우선
- [ ] I-38: malloc 실패 → COS_ENOMEM 반환
- [ ] **I-40: 전체 통합 E2E**
  - spawn → pipe 생성 → write → read → exit → wait → exit_code 확인

**완료 기준**: Sprint 6 테스트 6개 PASS + 전체 200개+ PASS

---

## 의존성 다이어그램

```
Sprint 1 ─────┬──→ Sprint 2 ──┬──→ Sprint 4 (셸)
(프로세스/에러) │               │
               │               ├──→ Sprint 5 (보안)
               │               │
               └──→ Sprint 3 ──┘
                   (VM/IPC)
                                    Sprint 6 (안정화)
                                    ← Sprint 1~5 전부 필요
```

---

## 파일 변경 맵

| 파일 | 변경 내용 | 스프린트 |
|------|-----------|---------|
| `include/canvasos_errno.h` | **신규** — 에러 코드 | S1 |
| `include/canvasos_permission.h` | **신규** — 권한 모델 | S5 |
| `src/permission.c` | **신규** — 권한 검증 로직 | S5 |
| `src/syscall.c` | 핸들러 25개+ 등록 | S1,S2,S3,S5 |
| `src/syscall_bindings.c` | 바인딩 함수 추가 | S1,S2,S3,S5 |
| `src/proc.c` | proc_wait, proc_tick 강화 | S1 |
| `src/signal.c` | 시그널 체계 전체 구현 | S1 |
| `src/vm_runtime_bridge.c` | bridge 완성 | S3 |
| `src/vm.c` | bridge 호출 경로 연결 | S3 |
| `src/shell.c` | 파이프, 리다이렉션, 12개 명령어 | S4 |
| `src/canvasfs.c` | LARGE, rename, FsMeta | S2 |
| `src/canvasfs_bpage.c` | FsMeta 갱신 | S2 |
| `src/fd.c` | 권한 검증 삽입 | S5 |
| `src/gate_ops.c` | 권한 확장 | S5 |
| `src/canvas_bh_compress.c` | loop/burst 감지 | S6 |
| `src/canvas_branch.c` | CR2 갱신 | S6 |
| `src/canvas_merge.c` | gate 충돌, RuleTable | S6 |
| `src/pixelcode.c` | malloc NULL 체크 | S6 |
| `src/canvasos_launcher.c` | vm_bridge_init 호출 | S3 |

---

## 진행 추적

### 전체 진행률
- **신규 파일**: 0/3
- **Syscall 핸들러**: 3/28 (10.7%)
- **테스트**: 160/200 (80%)
- **스프린트**: 0/6

### 체크포인트
| 마일스톤 | 조건 | 상태 |
|---------|------|------|
| M1: 프로세스 사이클 | spawn→run→exit→wait 동작 | ⬜ |
| M2: 파일 사이클 | open→write→read→close 동작 | ⬜ |
| M3: VM 자립 | VM에서 SPAWN→SEND→RECV→EXIT | ⬜ |
| M4: 셸 파이프 | `echo X \| cat > file` 동작 | ⬜ |
| M5: 권한 분리 | non-root 접근 거부 | ⬜ |
| M6: 전체 통합 | 200개 테스트 PASS | ⬜ |

---

## 구현 우선순위 요약

```
치명 ██████████ Syscall (3/62 → 28+)
치명 ██████████ 권한 모델 (0% → 기본 동작)
높음 ████████░░ VM Bridge (30% → 완성)
높음 ████████░░ 프로세스 관리 (70% → 완성)
높음 ███████░░░ Shell (50% → 파이프/리다이렉트)
중간 ██████░░░░ BH Compress (40% → loop/burst)
중간 ███████░░░ Branch/Merge (70% → Phase 5 TODO)
낮음 █████████░ CanvasFS (85% → LARGE/rename/meta)
```
