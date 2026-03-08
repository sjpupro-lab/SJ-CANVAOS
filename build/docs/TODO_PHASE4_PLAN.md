# Phase 4 Plan — CVP I/O + TimeMachine Replay (Draft)

> 목표: **Canvas 하나(.cvp)**만으로 부팅/저장/복원이 가능한 최소 OS 흐름을 만든다.  
> Phase 3(스케줄러, CanvasFS, Gate, WH/BH) 위에 얹는다.

## 4.0 범위 고정
- Canvas = 1024×1024 고정
- 스캔 = Ring N4 기본 (정책 교체 가능)
- ABGR 채널 계약 유지 (A=주소, B=동작 인덱스, G=상태, R=스트림)
- Always-ON, 신호는 CLOSE/OPEN

---

## 4.1 CVP 포맷 v1 (파일 1개로 끝내기)

### 4.1.1 컨테이너 레이아웃
- Header (고정 4KB)
  - magic, version, endian, flags
  - canvas_w/h (=1024/1024)
  - root_page_id, root_bpage_chain
  - reserved region map(최소)
- Sections (TLV)
  - CANVAS_BASE: 초기 캔버스(또는 sparse)
  - WH_LANE: WhiteHole 기록 구간(선택)
  - BH_STATE: BlackHole 상태(선택)
  - FS_VOLUME: CanvasFS 볼륨들(선택)
  - RULETABLE: RuleTable dump(선택)

### 4.1.2 직렬화 정책
- v1 최소 구현은 **RAW 덤프 + CRC32**로 시작
- 다음 단계에서 sparse(변경 셀만)로 전환
- Gate 상태는 bitmap(4096bit)로 저장

### 4.1.3 API
- cvp_save(path, EngineContext*)
- cvp_load(path, EngineContext*)
- cvp_validate(path) → magic/version/crc

---

## 4.2 TimeMachine Replay (진짜 “시간여행”)

### 4.2.1 리플레이 원칙
- “로그”를 별도로 쌓지 않는다.
- WH 레코드 자체가 리플레이 소스다.
- 같은 TimeSlice(page/bpage/gate/scan)면 결과 동일해야 함.

### 4.2.2 리플레이 API
- engctx_replay(ctx, from_tick, to_tick)
- wh_exec_record(ctx, record) 기반으로 side-effect 재현
- Phase 4에서 **WH opcode set 확장**:
  - FS write pointer update
  - gate open/close (완료)
  - process state transition (sleep/wake/kill)
  - commit Δ write

---

## 4.3 RuleTable v1 고정 (B-page Engine)

### 4.3.1 최소 엔트리
- EXEC_NOP
- EXEC_GATE (tile open/close)
- EXEC_FS (tiny/small payload write)
- EXEC_COMMIT (Δ-Commit)

### 4.3.2 어댑터 체인(Format Stack)
- bpage_chain을 통해 “포맷2포맷” 해석 스택 연결
- Canvas간 통신(Phase5) 전에 로컬에서 먼저 검증

---

## 4.4 테스트 (필수)
- Determinism 테스트:
  - 같은 CVP → 같은 tick 범위 실행 → 캔버스 해시 동일
- Replay 테스트:
  - tick 0..N 실행 후 저장
  - tick 0..N을 WH 리플레이로 재생성
  - 결과 동일
- Gate 보호 테스트:
  - reserved region은 어떤 경우에도 open/overwrite 금지(FreeMap 포함)

---

## 4.5 산출물
- docs/CVP_FORMAT_v1.md
- src/cvp_io.c 실제 구현(현재 stub 대체)
- tests/test_cvp.c 신규 추가
- docs/ENGINE_CANVASOS_v1.md 업데이트(Phase4 항목)

---
끝.


## Phase 4 Add-on: DevDictionary Web (Behavior-first Search)

- SSOT: include/canvasos_opcodes.def, canvasos_regions.def, canvasos_bindings.def
- Generator: tools/gen_devdict.py -> devdict_site/data/*.json
- UI goal: search by legacy dev keywords (GateSpace, sched_tick, VOLH/VOLT, WhiteHole/BlackHole)
- Primary: behavior -> locations list (pixel boxes + gate ranges)
- Secondary: location lookup (x,y or gate_id) -> region + linked behaviors
- Deploy: GitHub Pages (static)

---

# ✅ 반영된 사항 (현재 구현 상태)

## Determinism Lock
- CVP header에 **scan_policy / bpage_version / contract_hash / wh_cap / save_tick** 고정
- lock mismatch 시 load/validate 실패

## WH Overflow Policy
- WH는 **원형 버퍼(mod WH_CAP)**로 고정
- replay는 **save_tick 기준 retained window**에서만 유효

## Phase 5: IPC (WH relay)
- IPC send/recv를 WH opcode(0x30)로 기록
- proc.ipc_cursor로 **결정론적 consume**

## Phase 5 다음 TODO
- IPC record packing 확장: full 32-bit src/dst pid + payload key 분리
- Multi-canvas routing: dst_canvas >255, adapter chain 기반 해석
