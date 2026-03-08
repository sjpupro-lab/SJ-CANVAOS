# CVP Format v1.1

CanvasOS의 **결정론(Determinism) 고정 + 복원(Replay)** 용 캡슐 포맷.

핵심:

- **Always-ON** 캔버스 스냅샷 + WH 기반 재실행
- **Lock**: ScanPolicy / B-page version / ABGR contract hash가 다르면 로드 실패
- **WH overflow**: WH는 원형 버퍼(모듈로 WH_CAP). 저장 시점 기준 유지 구간만 replay 가능

## 1. Header (CvpHeader)

리틀엔디안.

| Field | Type | Meaning |
|---|---:|---|
| magic | u32 | `CVP1` |
| version | u32 | `0x00010001` |
| engine_major/minor | u32 | 엔진 버전 |
| scan_policy | u32 | `SCAN_RING_N4=1`, `SCAN_SPIRAL=2` |
| bpage_version | u32 | RuleTable/SSOT 버전(또는 hash) |
| contract_hash | u32 | ABGR 계약 signature |
| wh_cap | u32 | 컴파일된 WH_CAP 고정 |
| save_tick | u32 | 저장 시점 tick |
| flags | u32 | 예약 |
| header_crc | u32 | header_crc 제외 0..(N-1) CRC32 |

## 2. TLV Sections

반복 구조:

```
[type:u16][len:u32][crc:u32][data...]
```

- `crc`는 `data`에 대한 CRC32

### Section Types

| type | name | payload |
|---:|---|---|
| 1 | CANVAS_BASE | 전체 Cell dump (`sizeof(Cell) * W*H`) |
| 2 | GATE_STATE | Tile gate state array (`TILEGATE_COUNT`) |
| 3 | WH_LANE | WH 영역 Cell dump (검증/툴링용, CANVAS_BASE와 중복) |
| 4 | BH_STATE | BH 영역 Cell dump (검증/툴링용, CANVAS_BASE와 중복) |

## 3. Replay

- Replay의 유일 입력은 **WH 레코드(2-cell)**.
- WH는 원형 버퍼이므로 replay 가능한 범위는:

```
[save_tick - min(save_tick, WH_CAP), save_tick]
```

이 범위를 벗어나면 from_tick은 자동으로 clamp된다.
