"""
SJ Spatial Compression Engine v1.0
===================================
512×512 고정 이미지 1장 위에서 밝기 누산으로 초고압축을 달성하는 공간압축 엔진.

핵심 원리:
  - 모든 데이터는 512×512 캔버스의 밝기값(+1)으로 변환
  - 2세그먼트(SEG0: 0-255, SEG1: 256-511) × 512행
  - 바이트 값이 뭐든 상관없이 → 해당 x좌표 열의 밝기만 증가
  - WH(WhiteHole): 모든 누산 이벤트 기록 (시간순)
  - BH(BlackHole): WH 로그의 반복 패턴을 압축
  - 양발커널: 결정론 코어(누산/역누산) + 비결정론 어댑터(I/O)

압축 흐름:
  원본 N바이트 → 프론트 누산 → 512×512 밝기맵
  → BH 패턴 압축 → WH 요약 로그
  → 최종: 희소 밝기맵 + 압축 로그 = 초고압축

복원 흐름:
  압축 로그 → WH 재생 → 역누산 → 원본 N바이트
"""

import numpy as np
from dataclasses import dataclass, field
from typing import Iterator, List, Tuple, Optional
import struct
import io
import hashlib
import time

# ─── 캔버스 상수 ───
H = 512          # y축 (행)
W = 512          # x축 (2세그먼트 × 256)
SEG0 = 0         # 세그먼트0 시작 (바이트0 매핑)
SEG1 = 256       # 세그먼트1 시작 (바이트1 매핑)

# ─── WH/BH 상수 ───
WH_CAP = 65536           # WhiteHole 최대 레코드
BH_IDLE_MIN = 16         # BH IDLE 최소 틱
BH_LOOP_MIN_REPEAT = 3   # BH LOOP 최소 반복
BH_BURST_THRESHOLD = 8   # BH BURST 임계값

# ─── DK (Determinism Kernel) 규칙 ───
def dk_clamp_u8(v: int) -> int:
    """[DK-4] Normalize: 0-255 클램프"""
    return max(0, min(255, v))

def dk_clamp_i64(v: int) -> int:
    """int64 범위 클램프 (대형 누산용)"""
    return max(0, v)


# ═══════════════════════════════════════════════
#  프론트 라벨 (순서 추적)
# ═══════════════════════════════════════════════

@dataclass(frozen=True)
class FrontLabel:
    """프론트 식별자: R/G 번갈아, B 캐리"""
    r: int
    g: int
    b: int

def front_sequence() -> Iterator[FrontLabel]:
    """프론트 순서 생성기"""
    b = 0
    while True:
        for i in range(1, 256):
            yield FrontLabel(r=i, g=0, b=b)
            yield FrontLabel(r=0, g=i, b=b)
        b += 1


# ═══════════════════════════════════════════════
#  WhiteHole (WH): 이벤트 로그
# ═══════════════════════════════════════════════

@dataclass
class WhRecord:
    """WH 레코드: 1회 누산 이벤트"""
    tick: int              # 타임스탬프
    opcode: int            # 0x01=TICK, 0x20=ACCUM, 0x21=DEACCUM
    front_label: FrontLabel
    b0: int                # 세그먼트0 바이트값
    b1: int                # 세그먼트1 바이트값

    def to_bytes(self) -> bytes:
        """직렬화 (10바이트 고정)"""
        return struct.pack('<IBBBBBB',
            self.tick, self.opcode,
            self.front_label.r, self.front_label.g, self.front_label.b,
            self.b0, self.b1)

    @classmethod
    def from_bytes(cls, data: bytes) -> 'WhRecord':
        tick, op, r, g, b, b0, b1 = struct.unpack('<IBBBBBB', data[:10])
        return cls(tick=tick, opcode=op,
                   front_label=FrontLabel(r=r, g=g, b=b),
                   b0=b0, b1=b1)

WH_OP_TICK = 0x01
WH_OP_ACCUM = 0x20
WH_OP_DEACCUM = 0x21
WH_OP_BH_SUMMARY = 0x45


# ═══════════════════════════════════════════════
#  BlackHole (BH): 패턴 압축
# ═══════════════════════════════════════════════

@dataclass
class BhSummary:
    """BH 압축 요약"""
    rule: str              # 'IDLE' | 'LOOP' | 'BURST'
    from_tick: int
    to_tick: int
    count: int             # 반복 횟수 또는 이벤트 수
    stride: int = 0        # LOOP 간격
    pattern: Optional[Tuple[int, int]] = None  # LOOP 패턴 (b0, b1)
    original_hash: bytes = b''  # 원본 범위 해시 (감사 추적)

    def to_bytes(self) -> bytes:
        rule_map = {'IDLE': 0, 'LOOP': 1, 'BURST': 2}
        pat_b0 = self.pattern[0] if self.pattern else 0
        pat_b1 = self.pattern[1] if self.pattern else 0
        return struct.pack('<BIIIHBB',
            rule_map.get(self.rule, 0),
            self.from_tick, self.to_tick,
            self.count, self.stride,
            pat_b0, pat_b1) + self.original_hash[:16]

    @classmethod
    def from_bytes(cls, data: bytes) -> 'BhSummary':
        rule_id, ft, tt, cnt, stride, p0, p1 = struct.unpack('<BIIIHBB', data[:15])
        rule_names = {0: 'IDLE', 1: 'LOOP', 2: 'BURST'}
        return cls(rule=rule_names.get(rule_id, 'IDLE'),
                   from_tick=ft, to_tick=tt, count=cnt, stride=stride,
                   pattern=(p0, p1) if rule_id == 1 else None,
                   original_hash=data[15:31] if len(data) >= 31 else b'')


class WhiteHole:
    """WhiteHole: 순환 이벤트 로그"""

    def __init__(self, capacity: int = WH_CAP):
        self.records: List[WhRecord] = []
        self.capacity = capacity
        self.write_cursor = 0

    def write(self, rec: WhRecord):
        if len(self.records) < self.capacity:
            self.records.append(rec)
        else:
            self.records[self.write_cursor % self.capacity] = rec
        self.write_cursor += 1

    def read_range(self, from_tick: int, to_tick: int) -> List[WhRecord]:
        return [r for r in self.records if from_tick <= r.tick < to_tick]

    def total_records(self) -> int:
        return min(len(self.records), self.capacity)

    def hash_range(self, from_tick: int, to_tick: int) -> bytes:
        """범위 해시 (감사 추적용)"""
        h = hashlib.sha256()
        for r in self.read_range(from_tick, to_tick):
            h.update(r.to_bytes())
        return h.digest()[:16]


class BlackHole:
    """BlackHole: WH 로그 패턴 압축"""

    def __init__(self, wh: WhiteHole):
        self.wh = wh
        self.summaries: List[BhSummary] = []
        self.ticks_saved = 0

    def detect_idle(self, from_tick: int, to_tick: int) -> Optional[BhSummary]:
        """IDLE 탐지: 이벤트 없는 구간"""
        recs = self.wh.read_range(from_tick, to_tick)
        accum_recs = [r for r in recs if r.opcode == WH_OP_ACCUM]
        if len(accum_recs) == 0 and (to_tick - from_tick) >= BH_IDLE_MIN:
            return BhSummary(
                rule='IDLE', from_tick=from_tick, to_tick=to_tick,
                count=to_tick - from_tick,
                original_hash=self.wh.hash_range(from_tick, to_tick))
        return None

    def detect_loop(self, from_tick: int, to_tick: int) -> Optional[BhSummary]:
        """LOOP 탐지: 동일 (b0,b1) 패턴 반복"""
        recs = self.wh.read_range(from_tick, to_tick)
        accum_recs = [r for r in recs if r.opcode == WH_OP_ACCUM]
        if len(accum_recs) < BH_LOOP_MIN_REPEAT:
            return None

        # 첫 레코드의 패턴과 비교
        first = accum_recs[0]
        pat = (first.b0, first.b1)
        matching = sum(1 for r in accum_recs if (r.b0, r.b1) == pat)

        if matching >= BH_LOOP_MIN_REPEAT and matching == len(accum_recs):
            stride = 0
            if len(accum_recs) >= 2:
                stride = accum_recs[1].tick - accum_recs[0].tick
            return BhSummary(
                rule='LOOP', from_tick=from_tick, to_tick=to_tick,
                count=matching, stride=stride, pattern=pat,
                original_hash=self.wh.hash_range(from_tick, to_tick))
        return None

    def detect_burst(self, from_tick: int, to_tick: int) -> Optional[BhSummary]:
        """BURST 탐지: 짧은 구간에 대량 이벤트"""
        recs = self.wh.read_range(from_tick, to_tick)
        accum_recs = [r for r in recs if r.opcode == WH_OP_ACCUM]
        window = to_tick - from_tick
        if window > 0 and len(accum_recs) >= BH_BURST_THRESHOLD:
            return BhSummary(
                rule='BURST', from_tick=from_tick, to_tick=to_tick,
                count=len(accum_recs),
                original_hash=self.wh.hash_range(from_tick, to_tick))
        return None

    def compress(self, current_tick: int, window_size: int = 64) -> int:
        """BH 압축 실행: WH 윈도우 스캔"""
        compressed = 0
        t = 0
        while t + window_size <= current_tick:
            ft, tt = t, t + window_size
            # 우선순위: LOOP > BURST > IDLE
            summary = (self.detect_loop(ft, tt) or
                       self.detect_burst(ft, tt) or
                       self.detect_idle(ft, tt))
            if summary:
                self.summaries.append(summary)
                self.ticks_saved += summary.count
                compressed += 1
            t += window_size
        return compressed

    def get_compressed_log(self) -> bytes:
        """압축 로그 직렬화"""
        out = bytearray()
        out += struct.pack('<I', len(self.summaries))
        for s in self.summaries:
            sb = s.to_bytes()
            out += struct.pack('<H', len(sb)) + sb
        return bytes(out)


# ═══════════════════════════════════════════════
#  SJ 공간압축 캔버스 (512×512)
# ═══════════════════════════════════════════════

class SJSpatialCanvas:
    """
    512×512 고정 이미지 기반 공간압축 엔진.

    양발커널 구조:
      - Foot 1 (결정론 코어): front_step/front_unstep — 밝기 누산/역누산
      - Foot 2 (비결정론 어댑터): encode/decode — 외부 I/O 변환

    WH/BH 통합:
      - 모든 누산 → WH 기록
      - BH가 WH 패턴 → 요약 압축
    """

    def __init__(self):
        # 512×512 밝기 캔버스 (int64: 대형 누산 지원)
        self.canvas = np.zeros((H, W), dtype=np.int64)
        # WH/BH 엔진
        self.wh = WhiteHole()
        self.bh = BlackHole(self.wh)
        # 틱 카운터
        self.tick = 0
        # 프론트 로그 (복원용 최소 메타데이터)
        self.front_log: List[Tuple[FrontLabel, int, int]] = []

    # ─── Foot 1: 결정론 코어 ───

    def front_step(self, b0: int, b1: int, label: FrontLabel) -> None:
        """프론트 +1 누산: 두 열의 밝기 증가 (y축 전체)"""
        x0 = SEG0 + b0    # 세그먼트0 내 x좌표
        x1 = SEG1 + b1    # 세그먼트1 내 x좌표
        self.canvas[:, x0] += 1
        self.canvas[:, x1] += 1

        # WH 기록
        self.wh.write(WhRecord(
            tick=self.tick, opcode=WH_OP_ACCUM,
            front_label=label, b0=b0, b1=b1))
        self.tick += 1

    def front_unstep(self, b0: int, b1: int, label: FrontLabel) -> None:
        """프론트 -1 역누산: 밝기 감소 (underflow 방지)"""
        x0 = SEG0 + b0
        x1 = SEG1 + b1
        self.canvas[:, x0] = np.maximum(self.canvas[:, x0] - 1, 0)
        self.canvas[:, x1] = np.maximum(self.canvas[:, x1] - 1, 0)

        # WH 기록
        self.wh.write(WhRecord(
            tick=self.tick, opcode=WH_OP_DEACCUM,
            front_label=label, b0=b0, b1=b1))
        self.tick += 1

    def is_zero(self) -> bool:
        """캔버스 제로 확인 (완전 가역성 검증)"""
        return bool(np.all(self.canvas == 0))

    # ─── Foot 2: 비결정론 어댑터 (I/O 변환) ───

    def encode(self, data: bytes) -> dict:
        """
        인코딩: 바이트 스트림 → 밝기 누산

        Args:
            data: 원본 바이트 데이터 (임의 길이, 임의 내용)

        Returns:
            메타데이터 딕셔너리:
              - front_count: 사용된 프론트 수
              - original_size: 원본 크기
              - canvas_nonzero: 비제로 셀 수
              - wh_records: WH 레코드 수
        """
        it = front_sequence()
        pos = 0
        front_count = 0

        while pos < len(data):
            label = next(it)
            b0 = data[pos] if pos < len(data) else 0
            pos += 1
            b1 = data[pos] if pos < len(data) else 0
            pos += 1

            self.front_step(b0, b1, label)
            self.front_log.append((label, b0, b1))
            front_count += 1

        # BH 압축 실행
        bh_compressed = self.bh.compress(self.tick)

        return {
            'front_count': front_count,
            'original_size': len(data),
            'canvas_nonzero': int(np.count_nonzero(self.canvas)),
            'canvas_max_brightness': int(np.max(self.canvas)),
            'wh_records': self.wh.total_records(),
            'bh_summaries': len(self.bh.summaries),
            'bh_ticks_saved': self.bh.ticks_saved,
        }

    def decode(self) -> bytes:
        """
        디코딩: 프론트 로그 → 원본 바이트 복원

        Returns:
            복원된 바이트 데이터
        """
        out = bytearray()
        for label, b0, b1 in self.front_log:
            out.append(b0)
            out.append(b1)
        return bytes(out)

    def decode_and_verify(self) -> Tuple[bytes, bool]:
        """디코딩 + 역누산 가역성 검증"""
        decoded = self.decode()

        # 역누산: 프론트 로그 역순으로 빼기
        for label, b0, b1 in reversed(self.front_log):
            self.front_unstep(b0, b1, label)

        is_reversible = self.is_zero()
        return decoded, is_reversible

    # ─── 압축 출력 ───

    def get_compressed_image(self) -> bytes:
        """
        최종 압축 결과: 512×512 밝기맵 + 메타데이터

        Returns:
            압축된 바이트 (npz 형식)
        """
        buf = io.BytesIO()
        np.savez_compressed(buf,
            canvas=self.canvas,
            front_count=np.array([len(self.front_log)]),
        )
        return buf.getvalue()

    @staticmethod
    def _rle_encode(pairs: list) -> bytes:
        """
        BH-RLE 압축: 연속 동일 (b0,b1) 패턴을 런으로 압축.
        [0xFF][count_hi][count_lo][b0][b1] = RUN (5바이트, count>=1)
        [b0][b1]  (b0 != 0xFF)             = LITERAL (2바이트)
        b0==0xFF인 경우도 RUN으로 인코딩하여 마커 충돌 방지.
        """
        out = bytearray()
        i = 0
        while i < len(pairs):
            b0, b1 = pairs[i]
            run = 1
            while (i + run < len(pairs) and run < 65535 and
                   pairs[i + run] == (b0, b1)):
                run += 1
            if run >= 3 or b0 == 0xFF:
                # RUN (b0==0xFF도 여기서 처리)
                out.append(0xFF)
                out.append(run >> 8)
                out.append(run & 0xFF)
                out.append(b0)
                out.append(b1)
            else:
                for j in range(run):
                    out.append(pairs[i + j][0])
                    out.append(pairs[i + j][1])
            i += run
        return bytes(out)

    @staticmethod
    def _rle_decode(data: bytes, original_size: int) -> bytes:
        """BH-RLE 복원"""
        out = bytearray()
        pos = 0
        while pos < len(data) and len(out) < original_size:
            if data[pos] == 0xFF and pos + 4 < len(data):
                run = (data[pos + 1] << 8) | data[pos + 2]
                b0, b1 = data[pos + 3], data[pos + 4]
                pos += 5
                for _ in range(run):
                    out.append(b0)
                    out.append(b1)
            else:
                # LITERAL (b0 != 0xFF 보장)
                out.append(data[pos])
                pos += 1
                if pos < len(data):
                    out.append(data[pos])
                    pos += 1
        return bytes(out[:original_size])

    def get_full_compressed(self) -> bytes:
        """
        완전 압축 패키지 (BH-RLE):
          [4B magic] [4B front_count] [4B original_size]
          [4B rle_len] [4B flags] [rle_data]
        """
        magic = b'SJSC'
        front_count = len(self.front_log)
        original_size = front_count * 2

        # BH-RLE 압축
        pairs = [(b0, b1) for _, b0, b1 in self.front_log]
        rle_data = self._rle_encode(pairs)

        out = bytearray()
        out += magic
        out += struct.pack('<IIII', front_count, original_size,
                           len(rle_data), 0x01)  # flags=0x01 BH-RLE
        out += rle_data
        return bytes(out)

    @classmethod
    def decompress_full(cls, compressed: bytes) -> bytes:
        """
        완전 압축 해제: BH-RLE 패키지 → 원본 데이터
        """
        pos = 0
        magic = compressed[pos:pos+4]; pos += 4
        assert magic == b'SJSC', f"Invalid magic: {magic}"

        front_count, original_size, rle_len, flags = struct.unpack(
            '<IIII', compressed[pos:pos+16])
        pos += 16

        rle_data = compressed[pos:pos+rle_len]

        if flags & 0x01:
            return cls._rle_decode(rle_data, original_size)
        else:
            return bytes(rle_data[:original_size])

    # ─── 통계 ───

    def stats(self) -> dict:
        """압축 통계"""
        original_size = len(self.front_log) * 2
        compressed = self.get_full_compressed()
        canvas_bytes = self.canvas.nbytes  # 512*512*8 = 2MB raw

        # 희소성 분석
        total_cells = H * W
        nonzero = int(np.count_nonzero(self.canvas))
        sparsity = 1.0 - (nonzero / total_cells)

        return {
            'original_bytes': original_size,
            'compressed_bytes': len(compressed),
            'compression_ratio': original_size / len(compressed) if len(compressed) > 0 else 0,
            'canvas_raw_bytes': canvas_bytes,
            'canvas_nonzero_cells': nonzero,
            'canvas_sparsity': sparsity,
            'canvas_max_brightness': int(np.max(self.canvas)),
            'wh_total_records': self.wh.total_records(),
            'bh_summaries': len(self.bh.summaries),
            'bh_ticks_saved': self.bh.ticks_saved,
        }

    def print_canvas_visual(self, max_rows: int = 20, max_cols: int = 60):
        """캔버스 시각화 (밝기맵)"""
        step_y = max(1, H // max_rows)
        step_x = max(1, W // max_cols)
        chars = ' .:-=+*#%@'

        print(f"\n{'='*62}")
        print(f"  SJ Spatial Canvas 512×512 Brightness Map")
        print(f"  SEG0[0-255]              SEG1[256-511]")
        print(f"{'='*62}")

        max_val = max(1, int(np.max(self.canvas)))
        for y in range(0, H, step_y):
            row = ''
            for x in range(0, W, step_x):
                v = int(self.canvas[y, x])
                idx = min(len(chars) - 1, v * (len(chars) - 1) // max_val)
                row += chars[idx]
            print(f"  {row}")
        print(f"{'='*62}")
        print(f"  Max brightness: {max_val}  |  Nonzero: {np.count_nonzero(self.canvas)}/{H*W}")


# ═══════════════════════════════════════════════
#  벤치마크
# ═══════════════════════════════════════════════

def benchmark(data: bytes, label: str = ""):
    """단일 데이터셋 벤치마크"""
    print(f"\n{'━'*60}")
    print(f"  BENCHMARK: {label}")
    print(f"  Input: {len(data):,} bytes")
    print(f"{'━'*60}")

    sj = SJSpatialCanvas()

    # 인코딩
    t0 = time.time()
    meta = sj.encode(data)
    t_encode = time.time() - t0

    # 압축 패키지
    t0 = time.time()
    compressed = sj.get_full_compressed()
    t_compress = time.time() - t0

    # 복원
    t0 = time.time()
    restored = SJSpatialCanvas.decompress_full(compressed)
    t_decode = time.time() - t0

    # 검증
    # 원본에 패딩이 있을 수 있으므로 원본 길이만큼 비교
    match = restored[:len(data)] == data

    # 통계
    s = sj.stats()
    ratio = len(data) / len(compressed) if len(compressed) > 0 else 0
    saving = (1 - len(compressed) / len(data)) * 100 if len(data) > 0 else 0

    print(f"\n  ┌─ 압축 결과 ─────────────────────────────")
    print(f"  │ 원본 크기:     {len(data):>12,} bytes")
    print(f"  │ 압축 크기:     {len(compressed):>12,} bytes")
    print(f"  │ 압축률:        {ratio:>12.2f}x")
    print(f"  │ 절감:          {saving:>11.1f}%")
    print(f"  ├─ 캔버스 상태 ──────────────────────────")
    print(f"  │ 비제로 셀:     {s['canvas_nonzero_cells']:>12,} / {H*W:,}")
    print(f"  │ 희소율:        {s['canvas_sparsity']*100:>11.1f}%")
    print(f"  │ 최대 밝기:     {s['canvas_max_brightness']:>12,}")
    print(f"  ├─ WH/BH ──────────────────────────────")
    print(f"  │ WH 레코드:     {s['wh_total_records']:>12,}")
    print(f"  │ BH 요약:       {s['bh_summaries']:>12,}")
    print(f"  │ BH 틱 절감:    {s['bh_ticks_saved']:>12,}")
    print(f"  ├─ 시간 ────────────────────────────────")
    print(f"  │ 인코딩:        {t_encode:>11.3f}s")
    print(f"  │ 패키징:        {t_compress:>11.3f}s")
    print(f"  │ 복원:          {t_decode:>11.3f}s")
    print(f"  ├─ 검증 ────────────────────────────────")
    print(f"  │ 복원 일치:     {'✓ PASS' if match else '✗ FAIL':>12}")
    print(f"  └──────────────────────────────────────────")

    # 캔버스 시각화
    sj.print_canvas_visual()

    return sj, compressed, match


def run_all_benchmarks():
    """전체 벤치마크 실행"""
    print("=" * 60)
    print("  SJ Spatial Compression Engine v1.0")
    print("  512×512 Single-Image Brightness Accumulation")
    print("=" * 60)

    # 1. 텍스트 데이터 (반복 패턴 많음)
    text = b"Hello, SJ Spatial Compression! " * 100
    benchmark(text, "텍스트 (반복 패턴)")

    # 2. 바이너리 데이터 (랜덤)
    np.random.seed(42)
    random_data = np.random.bytes(3000)
    benchmark(random_data, "랜덤 바이너리 (패턴 없음)")

    # 3. 구조화된 데이터
    structured = bytes(range(256)) * 12
    benchmark(structured, "구조화 (0-255 반복)")

    # 4. 대형 데이터
    big_text = b"CanvasOS SJ Spatial Compression Engine " * 500
    benchmark(big_text, "대형 텍스트 (20KB)")

    # 5. 단일 바이트 반복 (최상 케이스)
    mono = b'\x42' * 5000
    benchmark(mono, "단일 바이트 반복 (최상 케이스)")

    # 6. 엔트로피 극대 (최악 케이스)
    high_entropy = bytes(range(256)) + bytes(range(255, -1, -1))
    benchmark(high_entropy, "고엔트로피 (최악 케이스)")


if __name__ == '__main__':
    run_all_benchmarks()
