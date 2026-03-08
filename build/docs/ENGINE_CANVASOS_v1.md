# CanvasOS Engine Specification v1.0
## Always-ON Spatial Execution Model

---

## 0. Core Philosophy

CanvasOS는 기본적으로 **항상 ON 상태**이다.

제어는 ON 신호가 아니라,
**차단(CLOSE)을 기본값으로 두고 통과(OPEN)만 흐르게 하는 방식**으로 수행한다.

즉:
- 실행은 "켜기"가 아니라
- **차단을 신호로 처리하는 구조**이다.

이 구조는 기존 인터럽트/컨텍스트 스위치 중심 OS와 다르게,
공간 기반 제어 모델을 따른다.

---

## 1. Fundamental Data Model

### 1.1 Cell Structure (ABGR Contract)

Each Cell consists of:
- **A_word (u32)**  → Spatial Address Layer (Where)
- **B (u8)**        → Behavior Layer (What)
- **G (u8)**        → State Layer (How it is)
- **R (u8)**        → Stream Layer (Data body)
- **pad (u8)**      → Reserved / Extension

---

## 2. Channel Contract (Immutable)

### 2.1 A — Spatial Address Layer (Where)

A는 다음만 허용된다:
- 주소 (tile / cell / slot / chain pointer)
- 링크 (next/ref)
- 공간 ID / 페이지 ID / 볼륨 ID
- 클래스/공간 계층 ID (공간 분류)

❗ **A에는 명령(opcode)을 두지 않는다.**  
A는 항상 **Where**만 정의한다.

---

### 2.2 B — Behavior Layer (What)

B는 다음만 허용된다:
- 명령 인덱스 (opcode index)
- 타입 (DAT/META/DIR/FRE/VOLH 등)
- 슬롯 클래스 (TINY/SMALL/LARGE)
- B-page ID / Adapter ID
- MicroStep / RuleTable Index

❗ **B에는 주소를 저장하지 않는다.**  
B는 항상 **What**만 정의한다.

---

### 2.3 G — State Layer (How it is)

G는 상태 관련 값만 가진다:
- 길이 (short len)
- 상태 플래그
- 버전 / 락 / 검증 상태
- 에너지/예산 상태(스케줄러 연동)

---

### 2.4 R — Stream Layer (Data body)

R은 데이터 스트림 전용이다:
- 파일 payload
- Δ 기록 데이터
- 연속 접근 데이터

---

### 2.5 복합 의미 금지 규칙

하나의 채널에 주소 + 명령 + 상태를 혼합하지 않는다.  
복합 의미는 반드시 **RuleTable(B-page)**에서 조합한다.

---

## 3. RuleTable (B-page Engine)

B는 단순 인덱스이다.  
실제 동작 의미는 `RuleTable[B]`에서 정의된다.

RuleTable은 다음을 포함할 수 있다:
- addr_mode
- exec_mode
- state_policy
- stream_policy
- adapter chain

RuleTable은 반드시:
- **가역성(encode/decode 1:1)**
- **결정론(deterministic)**

을 만족해야 한다.

---

## 4. Execution Model

### 4.1 Always-ON + Gate Filtering

한 Tick은 다음 순서를 따른다:

1) Observe
- PageSelector 적용
- GateSet 적용
- ActiveSet 산출 (살아남은 신호)

2) Interpret
- ActiveSet 셀을 B-page 체인으로 해석

3) Commit
- 변경사항은 Δ-Commit으로 기록 (TimeLane)

---

## 5. TimeMachine Engine

TimeMachine은 로그 파일이 아니라, **공간 기반 시간 모델**이다.

### 5.1 Time Definition
- **Y축은 시간 진행 방향**이다.
- 각 Quadrant는 중심에서 외곽으로 시간 진행을 가진다(정책으로 고정).

### 5.2 TimeSlice
TimeSlice는 다음으로 정의된다:
- page_id
- bpage_chain
- gate_snapshot
- scan_state

이 조합이 동일하면 결과는 항상 동일해야 한다.

### 5.3 Δ-Commit Rule
모든 상태 변화는:
- TimeLane 영역에 기록된다.
- 기존 상태를 덮어쓰지 않는다.
- 과거는 변경되지 않는다(읽기 전용).

이전 픽셀로 이동 = 디버깅(타임머신).

---


## 5.4 WhiteHole / BlackHole Tick Space (WH/BH)

CanvasOS는 Tick 단위 동작을 위해, Q3(우하 정배열 512×512) 상단에
Tick 전용 공간을 고정 배치한다.

- WhiteHole(WH): Tick 증가에 따라 기록이 "추가(append)"되는 공간
- BlackHole(BH): Tick 진행에 따라 값이 "감소(decay)"하는 공간

WH/BH는 Always-ON 캔버스 위에서 Gate(CLOSE/OPEN)로만 흐름을 제어한다.

---

### 5.4.1 위치/크기 (Q3 Top Allocation)

Q3 영역:
- x: 512..1023 (width = 512)
- y: 512..1023 (height = 512)

WH/BH는 Q3의 "가장 윗쪽(=y=512부터)"에 배치한다.

기본값:
- WH: N_WH_TILES_Y = 8 tiles  → 8*16 = 128 px 높이
- BH: N_BH_TILES_Y = 4 tiles  → 4*16 = 64 px 높이

좌표 정의:
- WH origin = (WH_X0, WH_Y0) = (512, 512)
- WH size   = (512, 128)
- BH origin = (BH_X0, BH_Y0) = (512, 512 + 128) = (512, 640)
- BH size   = (512, 64)

---

### 5.4.2 WH 레코드 포맷 (2 Cells / Record)

WH는 "로그처럼" 사용되며, Tick 인덱스가 곧 저장 주소가 된다.
레코드는 고정 길이 2셀로 정의한다.

Record = 2 Cells (C0, C1)

채널 계약 준수:
- A: Spatial Address Layer (Where)
- B: Behavior Layer (What)
- G: State Layer (State/flags)
- R: Stream Layer (byte stream)

C0 (Header/Opcode):
- A: tick_low32 또는 event_id
- B: opcode_index (RuleTable index)
- G: state/flags
- R: param0 (1 byte)

C1 (Target/Arg):
- A: target_addr (예: gate_id/slot/cell index를 packing한 주소)
- B: target_kind (FS_SLOT / TILE / CELL / PROC 등)
- G: arg_state (짧은 길이/상태)
- R: param1 (1 byte)

큰 payload는 WH에 직접 저장하지 않고,
CanvasFS key(pointer) 또는 별도 DataTile을 통해 참조한다.

---

### 5.4.3 Tick → WH 주소 매핑 (O(1))

정의:
- RECORD_CELLS = 2
- LANE_W = 512 (Q3 width)
- RECS_PER_ROW = LANE_W / RECORD_CELLS = 256
- WH_H = 128
- WH_CAP = RECS_PER_ROW * WH_H = 32768 records

tick -> index:
- idx = tick % WH_CAP
- row = idx / RECS_PER_ROW
- col = idx % RECS_PER_ROW
- x = WH_X0 + col * RECORD_CELLS
- y = WH_Y0 + row

WH 레코드의 첫 셀(C0)은 (x,y), 두번째 셀(C1)은 (x+1,y)

이 매핑은 결정론적이며, 동일 tick은 동일 좌표에 기록된다.

---

### 5.4.4 BH(BlackHole) 목적 및 매핑

BH는 "감쇠/에너지/TTL" 전용 공간이다.

BH는 append가 아니라 "소유자 기반 direct mapping"을 사용한다.
즉, pid 또는 gate_id에 대해 BH의 고정 위치가 존재한다.

기본 pid 에너지 매핑:
- pid -> BH cell 좌표 (x,y)를 O(1)로 매핑
- 해당 셀의 G 또는 A에 에너지 값을 저장 (정책으로 선택)
- Tick마다 감소(decay), 0이면 해당 프로세스 GateSpace를 CLOSE

BH는 시스템 안정성(자동 sleep/close, TTL)에 직접 연결된다.

---

### 5.4.5 Determinism Rule (WH/BH)

아래가 동일하면 결과도 동일해야 한다:
- 초기 Canvas
- 동일 Branch/Page/B-page chain
- 동일 Gate 상태
- 동일 Scan Policy
- 동일 tick 입력

WH 기록 좌표는 tick에 의해 결정되며,
BH 감소는 pid/gate_id 매핑에 의해 결정된다.

## 6. CanvasFS Integration

### 6.1 Volume Model
- **VOLH** (Volume Header Tile)
- **VOLT** (256 Slot Tile, 헤더 0개)

`VOLH(0,1).A = volt_gate_id`

### 6.2 Slot Contract
- `slot.A low16  = head_dat_gate`
- `slot.A high16 = meta_gate`

- `slot.B = slot class / type`
- `slot.G = state/length`
- `slot.R = flags`

---

## 7. Reserved Regions

Control Region:
- tile_x 32..35
- tile_y 32..35
- gate_id 2080..2275

FreeMap 초기화 시 반드시 reserved 처리한다.

---

## 8. Determinism Requirement

다음 조건이 동일하면 결과도 동일해야 한다:
- 초기 Canvas
- 동일 Branch
- 동일 Page
- 동일 Gate 상태
- 동일 B-page chain
- 동일 Scan Policy

이 조건은 테스트로 검증한다.

---

## 9. Engine Module Separation

Core는 다음만 담당한다:
- Scan
- Gate filtering
- Interpretation
- Commit
- Replay

Policy는 다음을 담당한다:
- Scan Mode (Ring / Spiral)
- Page Selection
- B-page selection

FS는 Core 위에서 동작한다.

---

## 10. Design Goals
- Copy-free branching
- Spatial determinism
- Gate-based protection
- Reversible interpretation
- GPU-friendly tile parallelism
- Time-as-space debugging

---

## 11. Determinism Lock + WH Overflow (Phase 4.1)

### 11.1 CVP Lock Fields

CVP는 아래 값이 동일할 때만 load/replay를 허용한다:

- `scan_policy`
- `bpage_version`
- `contract_hash` (ABGR contract signature)
- `wh_cap` (compiled WH_CAP)

다르면 `CVP_ERR_LOCK`.

### 11.2 WH Overflow Policy (Fixed)

WH는 **원형 버퍼(mod WH_CAP)**로 고정한다.

따라서 replay 가능한 범위는:

`[save_tick - min(save_tick, WH_CAP), save_tick]`

범위를 벗어나는 요청은 자동 clamp된다.

---

End of Specification v1.0

# Appendix: DevDictionary (Search-first Documentation)

- Canonical opcode/region/binding definitions live in `include/*.def` (SSOT).
- Generate search index JSON with `python3 tools/gen_devdict.py`.
- The DevDictionary UI is behavior-first: search a concept/opcode/name -> list pixel/tile/gate locations.

