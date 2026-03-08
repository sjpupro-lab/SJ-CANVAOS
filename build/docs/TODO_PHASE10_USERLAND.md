# CanvasOS Phase-10 — Userland on PixelCode

> **버전**: v1.0.1-p10 (목표)  
> **기반**: Phase-9 PixelCode VM  
> **한 줄**: VM 위에서 돌아가는 유저랜드 — fd, 경로, 셸, 유틸리티, 사용자 권한

---

## 0. 원칙

```
Phase-8이 커널 프리미티브를 만들었고,
Phase-9가 그 위에서 프로그램을 "연주"하는 VM을 만들었다.

Phase-10은 그 VM으로 실제 유저랜드를 세운다.

모든 유틸리티는 PixelCode 프로그램이다.
셸도 PixelCode이다.
ls도, cat도, ps도 — 전부 캔버스 위의 셀로 존재한다.

"프로그램을 설치한다" = "캔버스의 특정 타일에 셀을 심는다"
"프로그램을 실행한다" = "그 타일의 게이트를 열고 VM을 돌린다"
"프로그램을 삭제한다" = "게이트를 닫고 셀을 0으로 초기화한다"
```

---

## 1. Phase-8 잔여분 편입 (유저랜드 기반)

Phase-8에서 헤더만 존재하던 5개를 Phase-10에서 구현한다.
단, Phase-9 VM 위에서 동작하도록 재설계한다.

| Phase-8 잔여 | Phase-10 Task | 변경점 |
|-------------|---------------|--------|
| T8-05 fd | T10-01 | PixelCode에서 `SYSCALL SYS_OPEN/READ/WRITE` |
| T8-06 경로 탐색 | T10-02 | `/dir/file` → CanvasFS DIR1 재귀 탐색 |
| T8-07 셸 | T10-03 | PixelCode 기반 셸 (파이프 + 리다이렉션) |
| T8-09 사용자 | T10-04 | Lane = uid, 타일 권한 검사 |
| T8-10 유틸리티 | T10-05~15 | 각 명령 = PixelCode 프로그램 |
| T8-13 시각화 | T10-16 | watch = VM 실시간 미니맵 |

---

## 2. 파일 디스크립터 (T10-01)

```
파일: src/fd.c (P8 헤더 구현)

원리:
  fd = 프로세스별 인덱스 → FsKey 또는 Pipe 매핑
  fd 0/1/2 = stdin/stdout/stderr (예약)

  VM에서 파일 접근:
    B=FE A=SYS_OPEN  G=flags R=path_slot  → fd 반환
    B=FE A=SYS_READ  G=fd    R=len        → buf에 읽기
    B=FE A=SYS_WRITE G=fd    R=len        → buf에서 쓰기
    B=FE A=SYS_CLOSE G=fd                 → fd 닫기

  PixelCode 문법 (사용자용):
    open "/data/hello.txt" > $fd
    read $fd 10 > $buf
    write $fd "HELLO"
    close $fd

테스트:
  P10-T1: open → write → close → open → read → 데이터 일치
  P10-T2: fd 16개 초과 → 에러
  P10-T3: stdin/stdout 파이프 리다이렉션
```

---

## 3. 경로 탐색 (T10-02)

```
파일: src/path.c (P8 헤더 구현)

원리:
  경로 = "/" 구분 문자열
  각 단계: DIR1 name_hash 탐색
  cwd = 프로세스별 현재 디렉터리

  특수 경로:
    "/"    = root (pid=0 VOLH)
    "."    = cwd
    ".."   = parent (DIR1 첫 엔트리)
    "~"    = 사용자 홈 (Lane별 홈 타일)
    "/wh"  = WhiteHole 영역
    "/bh"  = BlackHole 영역
    "/dev" = 디바이스 (키보드/네트워크)
    "/proc"= 프로세스 정보 (가상)

  /proc 가상 파일시스템:
    /proc/0/status  → "RUNNING energy=255 lane=0"
    /proc/1/status  → "RUNNING energy=100 lane=1"
    /proc/self      → 현재 프로세스

테스트:
  P10-T4: mkdir /a → mkdir /a/b → cd /a/b → pwd = "/a/b"
  P10-T5: resolve "/proc/0/status" → init 상태
  P10-T6: "~" → 사용자 홈 타일
```

---

## 4. 셸 (T10-03)

```
파일: src/shell.c (P8 헤더 구현 + PixelCode 통합)

원리:
  셸 = pid=1의 PixelCode 인터프리터
  사용자 입력 → PixelCode 파싱 → VM 실행 또는 내장 명령

  새로 추가되는 셸 문법:

  파이프:
    echo "HELLO" | cat
    → pipe_create(echo_pid, cat_pid)
    → echo의 stdout → pipe → cat의 stdin

  리다이렉션:
    echo "DATA" > /data/file.txt
    cat < /data/file.txt
    echo "APPEND" >> /data/file.txt

  변수:
    $name = "value"
    echo $name          → "value"
    $count = 0
    $count + 1           → 1

  조건 (미래 확장):
    if G > 0 then ... end
    while G > 0 do ... end

  스크립트:
    source /scripts/init.pxl
    → 파일 내용을 한 줄씩 실행

  프로세스 연주 (셸에서 VM 실행):
    # 타일 100에 프로그램 작성 후
    run @tile(100)          → VM이 타일 100의 코드 실행
    run @tile(100) &        → 백그라운드 실행
    jobs                     → 백그라운드 프로세스 목록

테스트:
  P10-T7: echo hello | cat → "hello"
  P10-T8: echo data > /tmp/f → cat /tmp/f → "data"
  P10-T9: $x = world → echo hello $x → "hello world"
  P10-T10: source script → 스크립트 실행
```

---

## 5. 사용자 / 권한 (T10-04)

```
파일: src/user.c (P8 헤더 구현)

원리:
  Lane 0 = root (시스템)
  Lane 1~254 = 일반 사용자
  Lane 255 = broadcast

  로그인:
    login <username>
    → Lane 전환 + cwd = 홈 타일
    → Tervas 필터 = 해당 Lane만 표시

  권한 검사:
    파일 접근 시 tile_check(tp, uid, tile_id, perm) 호출
    실패 → SIG_SEGV → 프로세스 종료

  sudo:
    sudo <command>
    → 일시적으로 Lane 0으로 전환
    → 명령 실행 후 원래 Lane 복귀
    → root만 sudo 가능 (또는 sudoers 목록)

  PixelCode에서:
    B=FE A=SYS_GETUID  → 현재 Lane 반환
    B=FE A=SYS_SETUID  G=target_lane  → root만 가능

테스트:
  P10-T11: user1이 user2 타일 write → FAULT
  P10-T12: root가 모든 타일 접근 → OK
  P10-T13: sudo → Lane 전환 → 복귀
```

---

## 6. 유틸리티 (T10-05 ~ T10-15)

모든 유틸리티는 **PixelCode 프로그램**이다.
캔버스의 지정된 타일에 "설치"되어 있고, 셸이 호출하면 VM이 실행한다.

### 6.1 프로세스 유틸리티

| 명령 | Task | VM 동작 |
|------|------|---------|
| `ps` | T10-05 | `/proc` 순회 → stdout 출력 |
| `top` | T10-06 | Tervas 스냅샷 + 프로세스별 에너지/타일 표시 |
| `kill PID [SIG]` | T10-07 | `SYSCALL SYS_KILL` |

### 6.2 파일 유틸리티

| 명령 | Task | VM 동작 |
|------|------|---------|
| `ls [DIR]` | T10-08 | `path_ls` → 포맷팅 출력 |
| `cd DIR` | T10-09 | `path_cd` → cwd 변경 |
| `cat FILE` | T10-10 | `open → read loop → stdout → close` |
| `echo TEXT` | T10-11 | TEXT → stdout (또는 fd redirect) |
| `mkdir DIR` | T10-12 | `path_mkdir` |
| `rm FILE` | T10-13 | `path_rm` |
| `cp SRC DST` | T10-14 | `open SRC → read → open DST → write → close` |

### 6.3 시스템 유틸리티

| 명령 | Task | VM 동작 |
|------|------|---------|
| `hash` | (기존) | `dk_canvas_hash` 출력 |
| `save/load` | (기존) | CVP 저장/로드 |
| `timewarp N` | (기존) | 시간여행 |
| `det on/off` | (기존) | 결정론 토글 |
| `watch` | T10-16 | VM 실행 중 미니맵 자동 갱신 |
| `info` | T10-15 | tick, 프로세스 수, 메모리, WH/BH 상태 종합 |

### 6.4 유틸리티 "설치" 모델

```
  /sys/bin/         ← 시스템 유틸리티 타일 영역
  /sys/bin/ps       = 타일 3000에 PixelCode로 심어짐
  /sys/bin/ls       = 타일 3002에 PixelCode로 심어짐
  /sys/bin/cat      = 타일 3004에 PixelCode로 심어짐

  셸에서 "ls /data" 입력 시:
    1. 셸이 /sys/bin/ls 경로 resolve
    2. proc_spawn(ls_tile, energy=100, lane=current)
    3. VM이 ls_tile 코드 실행
    4. 실행 완료 → proc_exit
    5. 셸이 proc_wait → 결과 수집

  즉, "프로그램 설치 = 타일에 셀 기록"
      "프로그램 실행 = 게이트 열고 VM run"
      "프로그램 삭제 = 게이트 닫고 셀 클리어"
```

---

## 7. 부팅 시퀀스 (init → shell)

```
POST (런처)
  │
  ├── 메모리 검증 (8MB)
  ├── WH/BH 기하학 검증
  ├── 결정론 해시 검증
  │
  ▼
init (pid=0, Lane=0, 타일 0)
  │
  ├── /sys/bin/ 유틸리티 타일 초기화
  ├── /dev/ 디바이스 등록
  ├── /proc/ 가상 FS 등록
  ├── root 사용자 생성 (Lane 0)
  │
  ▼
login
  │
  ├── 사용자 인증 (Lane 선택)
  ├── cwd = ~/  (홈 타일)
  │
  ▼
shell (pid=1, Lane=user)
  │
  ├── /scripts/profile.pxl 실행 (자동 설정)
  ├── 프롬프트 표시
  │
  ▼
  사용자가 타자기를 치기 시작한다.
```

---

## 8. Task 의존성

```
T10-01 (fd) ────────┬──► T10-02 (경로)
                    └──► T10-03 (셸)

T10-02 (경로) ──────┬──► T10-08 (ls)
                    ├──► T10-09 (cd)
                    ├──► T10-10 (cat)
                    └──► T10-12 (mkdir)

T10-03 (셸) ────────┬──► T10-05~15 (유틸리티)
                    └──► T10-16 (watch)

T10-04 (사용자) ◄── T10-03 (셸 login 지원)

P9 VM ─────────────► 모든 T10 (VM 위에서 실행)
```

**구현 순서**:

```
Round 1: T10-01 (fd) → T10-02 (경로)
Round 2: T10-04 (사용자) → T10-03 (셸)
Round 3: T10-05~15 (유틸리티)
Round 4: T10-16 (watch) + 부팅 시퀀스 통합
```

---

## 9. 완료 조건

```bash
make test_all              # P6:6 + P7:10 + P8:18 + P9:20+ 회귀 없음
make test_phase10          # P10 PASS:25+ / FAIL:0

# 셸 통합 테스트
echo 'ls /' | ./canvasos_launcher
echo 'echo hello | cat' | ./canvasos_launcher
echo 'mkdir /data && echo test > /data/f && cat /data/f' | ./canvasos_launcher
echo 'ps' | ./canvasos_launcher
echo 'run @home' | ./canvasos_launcher

# 부팅 → 로그인 → 셸 → 명령 실행 → 종료
./canvasos_launcher
  > login user1
  > ls ~
  > echo hello > ~/test.txt
  > cat ~/test.txt
  > ps
  > @home B=01 R="HI" ! run
  > exit
```

---

## 10. Phase-10 완료 후 시스템 전체 상태

```
Phase  0~5: 엔진 기반           LOCKED
Phase  6:   결정론 엔진          PASS 6/6
Phase  7:   Tervas 터미널        PASS 10/10
Phase  8:   커널 프리미티브      PASS 18/18
Phase  9:   PixelCode VM         PASS 20+
Phase 10:   유저랜드             PASS 25+
─────────────────────────────────────────
총 테스트:                       79+ PASS

사용 가능한 기능:
  ✓ 프로세스 (fork/exec/exit/wait/signal)
  ✓ 메모리 보호 (타일 단위)
  ✓ 파이프
  ✓ 파일 디스크립터
  ✓ 경로 탐색 (/a/b/c)
  ✓ 셸 (파이프, 리다이렉션, 변수)
  ✓ 유틸리티 (ls, cat, ps, kill, echo, mkdir, rm, cp)
  ✓ 사용자/권한 (Lane = uid)
  ✓ PixelCode VM (바이트코드 실행)
  ✓ 시간여행 디버거
  ✓ 결정론/비결정론 토글
  ✓ 실시간 시각화

시스템 전체가 8MB 안에서 동작한다.
```

---

## 11. 새 파일 목록

```
src/
  fd.c              파일 디스크립터
  path.c            경로 탐색
  shell.c           셸 (PixelCode 기반)
  user.c            사용자/권한
  utils.c           유틸리티 명령 모음
  watch.c           실시간 시각화
  init.c            init 프로세스 (부팅 시퀀스)

tests/
  test_phase10.c    P10 테스트 (25+ cases)
```

---

*CanvasOS Phase-10 · Userland on PixelCode · v1.0.1-p10 · 2026-03-08*
