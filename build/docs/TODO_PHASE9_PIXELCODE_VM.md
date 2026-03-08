# CanvasOS Phase-9 — PixelCode VM

> **버전**: v1.0.1-p9 (목표)  
> **기반**: Phase-8 커널 (PASS 34/34)  
> **한 줄**: 캔버스 위에서 실행되는 바이트코드 VM — 타자기처럼 치고, 피아노처럼 연주한다

---

## 0. 철학

```
캔버스OS의 터미널은 텍스트 에디터가 아니다.
타자기(Typewriter) 앞에 바둑판이 깔려 있고,
사용자는 글자 대신 픽셀을 치면서 프로그램을 "연주"한다.

키 하나가 음표이고, 한 줄이 악보이고,
실행은 오르골이 돌아가듯 Y축(시간축)을 따라 자동으로 흐른다.

ABGR — 네 손가락이 네 채널을 누른다.
왼손(A,B)이 주소와 동작을, 오른손(G,R)이 상태와 데이터를 친다.
```

---

## 1. PixelCode — 바이트코드 언어

### 1.1 설계 원칙

```
1. 선두 알파벳 = ABGR 채널 → 사람이 읽을 수 있다
2. 모든 명령은 한 줄 = 한 셀(8 bytes)에 매핑된다
3. 터미널에서 타이핑하면 캔버스에 즉시 반영된다
4. 마우스로 셀을 클릭하면 A 주소 단위로 내용이 보인다
5. 실행은 Y축 아래로 흐른다 (시간 = 공간)
```

### 1.2 명령 문법

모든 명령은 **채널 접두사 + 값** 조합이다.

```
형식: [채널][연산자][값]  [채널][연산자][값]  ...

채널:   A  B  G  R
연산자: =  +  -  ~  @  !  ?  >  <  #
```

### 1.3 연산자 정의

| 기호 | 이름 | 의미 | 예시 |
|------|------|------|------|
| `=` | 설정 (SET) | 채널에 값 대입 | `A=00010000` |
| `+` | 증가 (ADD) | 채널에 값 더함 | `G+10` |
| `-` | 감소 (SUB) | 채널에서 값 뺌 | `G-1` |
| `~` | 반복 (REPEAT) | N번 반복 실행 | `R='X' ~5` = XXXXX |
| `@` | 주소 (ADDR) | 좌표로 이동 | `@(512,512)` |
| `!` | 커밋 (COMMIT) | 현재 레지스터 → 셀 기록 | `!` |
| `?` | 조회 (QUERY) | 셀/영역 정보 표시 | `?` `?(100,200)` |
| `>` | 이동 (STEP) | 커서 오른쪽/아래 | `>5` `.3` |
| `<` | 역이동 (BACK) | 커서 왼쪽/위 | `<3` |
| `#` | 범위 (RANGE) | 사각 영역 지정 | `#(0,0)~(511,511)` |

### 1.4 PixelCode 명령 전체 목록

**셀 편집 (타자기)**:

| 명령 | 동작 | 키보드 느낌 |
|------|------|-------------|
| `A=HHHHHHHH` | A 채널 32bit hex | 왼손 약지 |
| `B=HH` | B 채널 8bit hex | 왼손 검지 |
| `G=DDD` | G 채널 decimal | 오른손 검지 |
| `R=HH` 또는 `R='C'` | R 채널 | 오른손 약지 |
| `!` | 커밋 + Y↓ 자동 이동 | 엔터키 = 타자기 캐리지 리턴 |
| `!!N` | N번 반복 커밋 | 연타 |
| `!#` | 범위 전체 채우기 | 스탬프 |

**커서 이동 (타자기 캐리지)**:

| 명령 | 동작 |
|------|------|
| `@(x,y)` | 절대 이동 |
| `>N` | 오른쪽 N칸 |
| `<N` | 왼쪽 N칸 |
| `v N` | 아래 N칸 |
| `^ N` | 위 N칸 |
| `@wh` | WhiteHole 시작으로 |
| `@bh` | BlackHole 시작으로 |
| `@home` | (512,512) 중심으로 |

**영역 조작 (피아노 코드)**:

| 명령 | 동작 | 피아노 비유 |
|------|------|-------------|
| `#(x0,y0)~(x1,y1)` | 사각 영역 선택 | 코드 잡기 |
| `#fill B=01 G=100 R='A'` | 영역 채우기 | 코드 연주 |
| `#copy @(dx,dy)` | 영역 복사 | 전조 |
| `#clear` | 영역 0으로 | 음소거 |
| `#hash` | 영역 해시 | 음정 확인 |
| `#count` | 활성 셀 수 | 음표 수 세기 |

**데이터 스트림 (피아노 아르페지오)**:

| 명령 | 동작 |
|------|------|
| `R="HELLO"` | 문자열을 Y축으로 수직 적층 |
| `R=[48 45 4C 4C 4F]` | hex 배열을 Y축으로 적층 |
| `B=[01 02 03] ~3` | 패턴을 3번 반복 |
| `G=100 G-1 ~100` | 100→0 감쇠 패턴 생성 |

**프로그램 제어 (오르골)**:

| 명령 | 동작 |
|------|------|
| `run` | 현재 위치부터 Y↓ 방향으로 실행 시작 |
| `run @(x,y) ~N` | (x,y)에서 N tick 실행 |
| `stop` | 실행 중지 |
| `step` | 1 tick만 실행 |
| `step N` | N tick 실행 |
| `watch` | 실행 중 미니맵 자동 갱신 |
| `trace` | 실행 중 매 셀 출력 |

**게이트/시스템 (페달)**:

| 명령 | 동작 |
|------|------|
| `gate N` | 타일 N 게이트 OPEN |
| `gate -N` | 타일 N 게이트 CLOSE |
| `gate #(x0,y0)~(x1,y1)` | 영역 게이트 일괄 OPEN |
| `save [file]` | CVP 저장 |
| `load [file]` | CVP 로드 |
| `hash` | 전체 캔버스 해시 |
| `timewarp N` | tick N으로 시간여행 |
| `det on/off` | 결정론 토글 |

**마우스 (NCurses 확장)**:

| 동작 | 결과 |
|------|------|
| 셀 클릭 | `?(x,y)` 자동 실행 — A 주소 단위로 표시 |
| 셀 드래그 | `#(x0,y0)~(x1,y1)` 영역 자동 선택 |
| 더블 클릭 | 해당 A 주소의 모든 셀 하이라이트 |
| 우클릭 | 컨텍스트: copy / fill / gate / inspect |

---

## 2. VM 실행 모델

### 2.1 실행 흐름

```
캔버스의 한 열(X=고정)이 "프로그램"이다.
Y축 아래로 읽으면서 실행한다.

     x=512
  y=512  [B=01 G=100 R='H']  ← 첫 명령어
  y=513  [B=01 G=100 R='E']  ← 두번째
  y=514  [B=01 G=100 R='L']
  y=515  [B=01 G=100 R='L']
  y=516  [B=01 G=100 R='O']
  y=517  [B=00 G=000 R=00 ]  ← NOP = 프로그램 끝

실행 = "오르골이 Y축을 따라 핀을 읽어가는 것"
```

### 2.2 B 채널 = 명령어 (Instruction Set)

B 채널이 VM의 opcode이다. 이미 RuleTable[B]로 정의된 구조를 활용한다.

**기본 명령어 세트 (Phase-9)**:

| B 값 | 이름 | 동작 | ABGR 사용 |
|------|------|------|-----------|
| `00` | NOP | 아무것도 안 함 | — |
| `01` | PRINT | R을 stdout에 출력 | R=문자 |
| `02` | HALT | 실행 중지 | — |
| `03` | SET | A 주소의 셀에 G,R 기록 | A=대상, G=값, R=값 |
| `04` | COPY | A→A+arg 복사 | A=원본, G=대상 offset |
| `05` | ADD | A 주소의 G += R | A=대상, R=가산값 |
| `06` | SUB | A 주소의 G -= R | A=대상, R=감산값 |
| `07` | CMP | A 주소의 G와 R 비교 → flag | A=대상, R=비교값 |
| `08` | JMP | PC를 A 주소로 점프 | A=점프 대상 (y*1024+x) |
| `09` | JZ | flag==0이면 점프 | A=점프 대상 |
| `0A` | JNZ | flag!=0이면 점프 | A=점프 대상 |
| `0B` | CALL | PC 저장 + 점프 | A=서브루틴 주소 |
| `0C` | RET | 저장된 PC로 복귀 | — |
| `0D` | LOAD | A 주소의 셀을 레지스터에 로드 | A=원본 주소 |
| `0E` | STORE | 레지스터를 A 주소에 저장 | A=대상 주소 |
| `10` | GATE_ON | A 주소의 타일 게이트 OPEN | A=tile_id |
| `11` | GATE_OFF | A 주소의 타일 게이트 CLOSE | A=tile_id |
| `20` | SEND | 파이프에 R 전송 | A=pipe_id, R=데이터 |
| `21` | RECV | 파이프에서 R 수신 | A=pipe_id |
| `30` | SPAWN | 프로세스 생성 | A=코드 타일, G=에너지 |
| `31` | EXIT | 프로세스 종료 | G=exit code |
| `40` | DRAW | A 주소에 R 색상 직접 기록 | A=대상, R=색상 |
| `41` | LINE | A→G 사이 직선 그리기 | A=시작, G=끝, R=색상 |
| `42` | RECT | A 시작, G 크기 사각형 | A=좌상단, G=w|h, R=색상 |
| `FE` | SYSCALL | 시스템 콜 | A=syscall_nr, G,R=인자 |
| `FF` | BREAKPOINT | 디버거 중단점 | — |

### 2.3 실행 레지스터 (VM State)

```c
typedef struct {
    uint32_t pc_x;       /* Program Counter — X 좌표 (열) */
    uint32_t pc_y;       /* Program Counter — Y 좌표 (행, 아래로 증가) */
    uint32_t reg_A;      /* A 레지스터 (주소)   */
    uint8_t  reg_B;      /* B 레지스터 (opcode)  */
    uint8_t  reg_G;      /* G 레지스터 (상태)    */
    uint8_t  reg_R;      /* R 레지스터 (데이터)  */
    uint8_t  flag;       /* 비교 플래그 (CMP 결과) */
    uint32_t sp;         /* 스택 포인터 (CALL/RET용) */
    uint32_t call_stack[32]; /* 리턴 주소 스택 */
    bool     running;    /* 실행 중 여부 */
    bool     trace;      /* trace 모드 */
    uint32_t pid;        /* 소속 프로세스 */
    uint32_t tick_limit; /* 최대 실행 tick (무한루프 방지) */
} VmState;
```

### 2.4 Fetch-Decode-Execute 사이클

```c
int vm_step(EngineContext *ctx, VmState *vm) {
    /* 1. FETCH: PC 위치의 셀 읽기 */
    Cell *cell = &ctx->cells[vm->pc_y * CANVAS_W + vm->pc_x];

    /* 2. DECODE: ABGR 채널 분리 */
    vm->reg_A = cell->A;
    vm->reg_B = cell->B;
    vm->reg_G = cell->G;
    vm->reg_R = cell->R;

    /* 3. EXECUTE: B 채널로 dispatch */
    switch (vm->reg_B) {
    case 0x00: break;                              /* NOP */
    case 0x01: putchar(vm->reg_R); break;          /* PRINT */
    case 0x02: vm->running = false; break;         /* HALT */
    case 0x08: vm->pc_y = vm->reg_A / CANVAS_W;   /* JMP */
               vm->pc_x = vm->reg_A % CANVAS_W;
               return 0; /* PC 수동 이동, 자동 증가 안 함 */
    /* ... */
    }

    /* 4. PC 자동 증가 (Y↓) */
    vm->pc_y++;
    if (vm->pc_y >= CANVAS_H) vm->running = false;

    return 0;
}
```

---

## 3. 터미널 UI (타자기 + 바둑판)

### 3.1 화면 구성

```
┌─────────────────────────────────────────────────────────────────┐
│  ◆ CANVAS OS v1.0.1-p9      tick=00042  hash=A3B2C1D0          │
├──────────────┬──────────────────────────────────────────────────┤
│              │                                                  │
│   미니맵     │    메인 뷰 (확대된 셀 영역)                      │
│   64×32      │    A 주소 단위로 색상 표시                       │
│              │                                                  │
│   ···@····   │    ┌─A=00010000─┬─A=00010001─┬─A=00010002─┐     │
│   ·····█··   │    │ B=01 G=100 │ B=01 G=100 │ B=01 G=100 │     │
│   ····████   │    │ R='H'      │ R='E'      │ R='L'      │     │
│   ···WH···   │    └────────────┴────────────┴────────────┘     │
│   ···BH···   │                                                  │
│              │    [실행 출력]                                    │
│              │    > HELLO                                       │
│              │    > tick 5 completed                             │
├──────────────┴──────────────────────────────────────────────────┤
│  canvas:512,514:0042>  B=01 G=100 R='L' !                      │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 미니맵 색상

| 영역 | 색상 | 의미 |
|------|------|------|
| `·` (dim) | 어두운 회색 | 비활성 (G=0, gate CLOSE) |
| `░` (green) | 연두 | gate OPEN, 비어있음 |
| `█` (bright green) | 밝은 녹색 | gate OPEN + 데이터 있음 |
| `█` (cyan) | 밝은 파랑 | WH 영역, 활성 |
| `█` (red) | 밝은 빨강 | BH 영역, 에너지 >0 |
| `@` (white) | 흰색 | 현재 커서 위치 |
| `▶` (yellow) | 노랑 | VM 프로그램 카운터 |

### 3.3 메인 뷰 (A 주소 단위)

마우스 클릭 또는 `?` 명령으로 셀을 선택하면, A 주소를 기준으로 관련 셀을 그룹으로 보여준다.

```
?(512,512)

  A=00010000  (Lane 1, Slot 0)
  ┌──────────────────────────────────┐
  │ (512,512) B=01 G=100 R=48 'H'   │  ← PC=여기
  │ (512,513) B=01 G=100 R=45 'E'   │
  │ (512,514) B=01 G=100 R=4C 'L'   │
  │ (512,515) B=01 G=100 R=4C 'L'   │
  │ (512,516) B=01 G=100 R=4F 'O'   │
  │ (512,517) B=00 G=000 R=00  NOP  │  ← 프로그램 끝
  └──────────────────────────────────┘
  tile=2080 gate=OPEN  energy=100
```

---

## 4. 예시: "HELLO" 프로그램

### 4.1 타이핑으로 작성

```
canvas:512,512:0001> @home
canvas:512,512:0001> gate 2080
canvas:512,512:0001> B=01 G=100 R='H' !
canvas:512,513:0002> R='E' !
canvas:512,514:0003> R='L' !
canvas:512,515:0004> R='L' !
canvas:512,516:0005> R='O' !
canvas:512,517:0006> B=02 !
canvas:512,518:0007> @(512,512)
canvas:512,512:0007> run
> HELLO
> [HALT at tick 13]
```

### 4.2 한 줄 스트림으로 작성

```
canvas> @home gate 2080 R="HELLO" B=02 ! run
> HELLO
> [HALT at tick 13]
```

### 4.3 반복 패턴 (피아노 아르페지오)

```
canvas> @home gate 2080
canvas> B=01 G=100 R=[48 45 4C 4C 4F] ~1 B=02 ! run
> HELLO
```

### 4.4 조건 분기

```
canvas> @home gate 2080
canvas> B=0D A=00010000 !    ← LOAD: A 주소 셀을 레지스터에
canvas> B=07 R=00 !           ← CMP: G와 0 비교
canvas> B=09 A=00000208 !    ← JZ: 0이면 (520) 점프
canvas> B=01 R='Y' !          ← PRINT 'Y' (0이 아닌 경우)
canvas> B=08 A=00000209 !    ← JMP: (521)로 건너뜀
canvas> B=01 R='N' !          ← PRINT 'N' (0인 경우)
canvas> B=02 !                 ← HALT
```

---

## 5. Task 목록

### Round 1: VM 코어

| Task | 이름 | 설명 | 파일 |
|------|------|------|------|
| T9-01 | VmState 구조체 | PC, 레지스터, 플래그, 콜스택 | `canvasos_vm.h` |
| T9-02 | vm_step() | Fetch-Decode-Execute 1 사이클 | `vm.c` |
| T9-03 | 기본 명령어 16종 | NOP~STORE + GATE | `vm.c` |
| T9-04 | vm_run() | 연속 실행 (tick_limit 포함) | `vm.c` |
| T9-05 | vm_trace() | 실행 중 매 셀 출력 모드 | `vm.c` |

### Round 2: PixelCode 파서

| Task | 이름 | 설명 | 파일 |
|------|------|------|------|
| T9-06 | 영역 명령 `#` | `#(x,y)~(x,y)` 선택/fill/copy/clear | `pixelcode.c` |
| T9-07 | 스트림 명령 | `R="HELLO"`, `R=[hex array]`, `~N` 반복 | `pixelcode.c` |
| T9-08 | run/stop/step | VM 실행 제어 | `pixelcode.c` |
| T9-09 | 그리기 명령 | DRAW/LINE/RECT (B=40~42) | `vm.c` |

### Round 3: 터미널 통합

| Task | 이름 | 설명 | 파일 |
|------|------|------|------|
| T9-10 | 런처 VM 연동 | 런처에서 `run`, `step`, `trace` 지원 | `canvasos_launcher.c` |
| T9-11 | A 주소 뷰어 | `?` → A 주소 기준 그룹 표시 | `launcher / tervas` |
| T9-12 | NCurses 미니맵 | 컬러 미니맵 + VM PC 표시 | `render_ncurses.c` |

### Round 4: 확장 명령어

| Task | 이름 | 설명 | 파일 |
|------|------|------|------|
| T9-13 | SPAWN/EXIT | VM에서 프로세스 생성/종료 (P8 연동) | `vm.c` |
| T9-14 | SEND/RECV | VM에서 파이프 통신 (P8 연동) | `vm.c` |
| T9-15 | SYSCALL | VM에서 시스템 콜 (P8 연동) | `vm.c` |

---

## 6. 의존성

```
T9-01 → T9-02 → T9-03 → T9-04 → T9-05     (VM 코어)
T9-06 → T9-07 → T9-08                        (파서)
T9-03 + T9-08 → T9-09                        (그리기)
T9-04 + T9-08 → T9-10 → T9-11 → T9-12       (터미널)
T9-03 + P8 → T9-13 → T9-14 → T9-15          (확장)
```

---

## 7. 완료 조건

```bash
make test_all          # P6:6 + P7:10 + P8:18 회귀 없음
make test_phase9       # P9 PASS:20+ / FAIL:0

# VM 실행 테스트
echo '@home gate 2080 R="HELLO" B=02 ! run' | ./canvasos_launcher
# → HELLO 출력

# 반복 패턴
echo '@home gate 2080 B=01 G=100 R=65 ~10 B=02 ! run' | ./canvasos_launcher
# → AAAAAAAAAA 출력

# 조건 분기
# → JZ/JNZ 테스트 통과
```

---

## 8. 새 WH 옵코드

| Code | 이름 | 설명 |
|------|------|------|
| 0x90 | WH_OP_VM_START | VM 실행 시작 |
| 0x91 | WH_OP_VM_HALT | VM 실행 중지 |
| 0x92 | WH_OP_VM_STEP | VM 1 step |
| 0x93 | WH_OP_VM_BREAKPOINT | 중단점 도달 |

---

## 9. 파일 목록

```
include/
  canvasos_vm.h          VmState, 명령어 enum, vm_step/run API
  canvasos_pixelcode.h   PixelCode 파서 (#, ~, R="", 등)

src/
  vm.c                   VM Fetch-Decode-Execute + 명령어 구현
  pixelcode.c            PixelCode 파서 + 영역/스트림/반복

tests/
  test_phase9.c          VM + PixelCode 테스트 (20+ cases)
```

---

*CanvasOS Phase-9 · PixelCode VM · v1.0.1-p9 · 2026-03-08*
