# CanvasFS v1.1 Specification (VolumeTile + Slot Table + DataTile)

Status: Draft

Depends on:
- `SPEC_ControlRegion_Page_Bpage_v1`
- `SPEC_CanvasOS_QuadBoot_v1`

Default engine:
- Scan: `RING_WAVEFRONT`
- Neighbor: `N4`
- Exec: `Gate → ActiveSet → Δ-Commit`

---

## 1. 목표

CanvasFS v1.1은 "타일 1개=파일" 모델을 버리고, **타일 1개=슬롯 테이블(VolumeTile)**로 고정한다.

- VolumeTile 1개는 16×16=256 슬롯을 가진다.
- 슬롯은 "가장 작은 클래스"부터 자동 선택한다.
- 실제 데이터는 DataTile로 분리 저장하며, 필요시 체인으로 무제한 확장한다.
- 브랜치/멀티버스는 Canvas 복사가 아니라 PageSelector(가시성) 변경이다.

---

## 2. 단위 정의

### 2.1 Tile
- Tile = 16×16 Cell
- 전체 타일 수 = 64×64 = 4096
- `gate_id`는 Tile ID(0..4095)

### 2.2 FileKey

```
FsKey = (volume_gate_id, slot)
volume_gate_id: 0..4095
slot: 0..255
```

**이론상 최대 슬롯 수**
- 4096 × 256 = 1,048,576

이 값은 "파일 엔트리 슬롯"의 수이며, 실제 저장 데이터 용량과 동일하지 않다.

---

## 3. VolumeTile

VolumeTile은 슬롯 테이블이다.

### 3.1 Magic
VolumeTile의 (row0,col0) cell에 다음 값을 저장한다:
- B='V', G='O', R='L', pad='1'

### 3.2 Slot Cell Schema
VolumeTile 내의 각 Cell이 곧 "슬롯 1개"이다.

- B: slot class
- G: length
- R: flags
- A: payload(TINY) 또는 head data_tile gate_id

Slot class:
- `FREE(0)`  : 비어있음
- `TINY(1)`  : 0..4 bytes를 A(u32)에 inline 저장
- `SMALL(2)` : 5..224 bytes를 DataTile 1개에 저장
- `LARGE(3)` : 225..∞ bytes를 DataTile 체인으로 저장

Length:
- TINY/SMALL: 정확한 길이(0..224)
- LARGE: v1.1에서는 `0xFF`로 마크, 실제 길이는 v1.2 Meta에 저장

---

## 4. DataTile

DataTile은 실제 페이로드 타일이다.

### 4.1 Magic
DataTile의 (row0,col0) cell:
- B='D', G='A', R='T', pad='1'

### 4.2 Next pointer
DataTile의 (row0,col1) cell:
- A(u32)의 하위 16비트에 next_gate_id 저장
- next_gate_id=0이면 체인 종료

### 4.3 Payload Layout
- payload rows = row2..row15
- payload cells = 14×16=224
- v1.1 저장: R 채널 1 byte per cell

---

## 5. 동작 규칙

### 5.1 Write
입력 길이 len에 따라 자동 선택:
- len<=4  → TINY
- 5..224  → SMALL (DataTile 1개)
- >224    → LARGE (DataTile 체인)

### 5.2 Read
- TINY: A에서 복원
- SMALL: head DataTile 1개 읽음
- LARGE: 체인을 순서대로 읽음(출력 버퍼 cap까지)

### 5.3 Free
- 슬롯을 FREE로만 표시한다.
- v1.1에서는 DataTile GC는 수행하지 않는다.

---

## 6. v1.2 확장

- DirectoryBlock: `name_hash → FsKey(volume,slot)`
- Large length meta: LARGE의 실제 길이를 MetaTile에 저장
- FreeMap: DataTile 할당을 위한 비트맵/프리리스트
- Gate 연동: Gate CLOSED면 read/write 차단



## 9. B-page (Format Adapter)

CanvasFS의 "포맷 전환"은 **데이터 이동 없이** 이뤄진다.

- VolumeTile마다 `active bpage_id`가 있다.
- `bpage_id`가 바뀌면, 같은 DataTile의 R 바이트열을 다른 규칙으로 decode/encode 한다.
- 즉, **B-page 교체 한 줄 = 포맷2포맷 Adapter**.

### 9.1 저장 위치

- VolumeTile (VOL1)
  - (row0,col1).B = bpage_id low
  - (row0,col1).G = bpage_id high

### 9.2 기본 제공 bpage_id (v1.2)

- 0: IDENTITY (no-op)
- 1: XOR8      (byte ^= key)
- 2: NIBBLE    (swap hi/lo nibble)
- 3: ROTL1     (encode rotl1, decode rotr1)

`key`는 기본적으로 다음으로 결정된다:

- `key = 0x5A ^ (bpage_id & 0xFF) ^ (volume_gate_id & 0xFF)`

(결정론적, 가역, 빠름)

### 9.3 API

- `fs_set_bpage(fs, volume_gate_id, bpage_id)`
- `fs_get_bpage(fs, volume_gate_id, &out_bpage_id)`

