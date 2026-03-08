# SJ CanvasOS

**A spatial operating system where memory, files, execution, and time converge on a single 2D canvas.**

**메모리, 파일, 실행, 시간이 하나의 2D 캔버스 위에서 수렴하는 공간형 운영체제.**

[![Version](https://img.shields.io/badge/version-v1.0.8--patchH-blue)]()
[![Tests](https://img.shields.io/badge/tests-160%2F160_PASS-brightgreen)]()
[![Language](https://img.shields.io/badge/language-C11-orange)]()
[![Platform](https://img.shields.io/badge/platform-Linux%20|%20Android-lightgrey)]()

---

## Overview / 개요

CanvasOS runs programs by planting VM instructions as cells on a 1024×1024 canvas grid. Every state mutation is recorded in the WhiteHole log, enabling deterministic replay and time travel. Branches split the canvas into parallel realities that can be independently evolved and merged.

CanvasOS는 1024×1024 캔버스 격자 위에 VM 명령어를 셀로 심어 프로그램을 실행합니다. 모든 상태 변화는 WhiteHole 로그에 기록되어 결정론적 재생과 시간 여행이 가능하며, 분기(branch)로 캔버스를 병렬 현실로 분리한 뒤 다시 병합할 수 있습니다.

## Architecture / 아키텍처

```
┌──────────────────────────────────────────────────────────┐
│  Presentation   shell · tervas · live demo · renderer    │
├──────────────────────────────────────────────────────────┤
│  Pixel Runtime  VM · PixelCode · pixel_loader            │
├──────────────────────────────────────────────────────────┤
│  Runtime        proc · fd · pipe · syscall               │
├──────────────────────────────────────────────────────────┤
│  Temporal       snapshot · branch · merge · timewarp     │
├──────────────────────────────────────────────────────────┤
│  CanvasFS       slot · payload · metadata · path         │
├──────────────────────────────────────────────────────────┤
│  Core Engine    scan · gate · active set · determinism   │
└──────────────────────────────────────────────────────────┘
```

## Quick Start / 빠른 시작

```bash
cd build

# Build and run all 160 tests (13 test suites)
# 전체 160개 테스트 빌드 및 실행 (13개 테스트 스위트)
make test_all CC=clang

# Run the live demo / 라이브 데모 실행
make demo_patchF && ./examples/demo_patchF

# Run Tervas canvas terminal / Tervas 캔버스 터미널 실행
make tervas && ./tervas

# Build dashboard (tests + status JSON for devdict)
# 대시보드 빌드 (테스트 + devdict용 상태 JSON)
make dashboard

# Sanitizer build (ASan + UBSan)
make sanitize

# Release check (all tests + sanitizer)
make release_check
```

## Cell Data Model (ABGR) / 셀 데이터 모델

Every cell on the 1024×1024 canvas is 8 bytes:

캔버스의 모든 셀은 8바이트입니다:

| Channel | Size | Role / 역할 |
|---------|------|-------------|
| **A** | 32-bit | Address / spatial coordinate / 주소·공간 좌표 |
| **B** | 8-bit | Behavior / opcode / 행동·명령어 |
| **G** | 8-bit | State / energy / 상태·에너지 |
| **R** | 8-bit | Stream / data payload / 스트림·데이터 |

## Core Features / 핵심 기능

### Deterministic Execution / 결정론적 실행
Same input → same output, guaranteed by DK rules:

동일 입력 → 동일 출력, DK 규칙으로 보장:

- **DK-1**: Delta commit/merge only at tick boundary / 틱 경계에서만 커밋·병합
- **DK-2**: Integer-only operations / 정수 연산만 허용
- **DK-3**: Merge in cell_index ascending order / 셀 인덱스 오름차순 병합
- **DK-4**: Integer clamp for overflow / 오버플로우 정수 클램프
- **DK-5**: Noise-floor filtering / 노이즈 플로어 필터링

### PixelCode Self-Hosting / 자체 호스팅
Utilities (echo, cat, info, hash, help) run as PixelCode programs planted on the canvas, not as C functions. The OS runs programs *on itself*.

유틸리티(echo, cat, info, hash, help)는 C 함수가 아닌 캔버스 위에 심어진 PixelCode 프로그램으로 실행됩니다. OS가 *자기 자신 위에서* 프로그램을 실행합니다.

### Time Travel / 시간 여행
`timewarp <tick>` restores the canvas to any previous state. Every mutation is recorded in the WhiteHole log for deterministic replay.

`timewarp <tick>`으로 캔버스를 이전 상태로 복원합니다. 모든 변경은 WhiteHole 로그에 기록되어 결정론적 재생이 가능합니다.

### Spatial Branching / 공간 분기
`branch create <name>` splits reality. Each branch tracks its own write-set. `merge <a> <b>` combines branches with conflict detection.

`branch create <name>`으로 현실을 분기합니다. 각 분기는 자체 쓰기 집합을 추적하며, `merge <a> <b>`로 충돌 감지와 함께 병합합니다.

### Tervas Canvas Terminal / Tervas 캔버스 터미널
Read-only canvas viewer with projection-based filtering. Supports A/B value matching, WH/BH region focus, zoom/pan, and multiple renderer backends (ASCII, NCurses, SDL2, OpenGL upgrade path).

투영(projection) 기반 필터링을 제공하는 읽기 전용 캔버스 뷰어. A/B 값 매칭, WH/BH 영역 포커스, 줌/팬, 다중 렌더러 백엔드(ASCII, NCurses, SDL2, OpenGL 업그레이드 경로)를 지원합니다.

**Fixed Rules / 불변 규칙:**
- R-1: Y-axis = time axis / Y축 = 시간축
- R-2: 4-quadrant layout / 4분면 레이아웃
- R-3: No layer system — projection only / 레이어 없음, 투영만
- R-4: READ-ONLY — never modifies engine state / 엔진 상태를 절대 수정하지 않음
- R-5: Snapshot only at tick boundary / 틱 경계에서만 스냅샷
- R-6: Integer arithmetic only / 정수 연산만

### DevDictionary Dashboard / 개발자사전 대시보드
Built-in web UI for browsing OS internals — 264 symbol entries, 8 spec documents, and live build status. Auto-generated from test results.

OS 내부를 탐색하는 내장 웹 UI — 264개 심볼, 8개 스펙 문서, 실시간 빌드 상태. 테스트 결과에서 자동 생성됩니다.

## Shell Commands / 셸 명령어

| Command | Description / 설명 |
|---------|---------------------|
| `echo <text>` | Print text (PixelCode) / 텍스트 출력 |
| `cat <path>` | Display file or virtual path / 파일·가상경로 출력 |
| `info` | System information / 시스템 정보 |
| `hash` | Canvas hash / 캔버스 해시 |
| `help` | Command help / 명령어 도움말 |
| `ps` | Process list / 프로세스 목록 |
| `kill <pid>` | Send signal / 시그널 전송 |
| `ls [path]` | List directory / 디렉토리 목록 |
| `cd <path>` | Change directory / 디렉토리 이동 |
| `mkdir <name>` | Create directory / 디렉토리 생성 |
| `rm <path>` | Remove entry / 항목 삭제 |
| `snapshot <name>` | Save canvas state / 캔버스 상태 저장 |
| `branch create\|list\|switch` | Branch management / 분기 관리 |
| `merge <a> <b>` | Merge branches / 분기 병합 |
| `timewarp <tick>` | Time travel / 시간 여행 |
| `timeline` | Timeline status / 타임라인 상태 |
| `det on\|off` | Toggle determinism / 결정론 모드 전환 |

## Tervas Commands / Tervas 명령어

| Command | Description / 설명 |
|---------|---------------------|
| `view all` | Full canvas projection / 전체 캔버스 투영 |
| `view a <values>` | Filter by A channel values / A 채널 값 필터 |
| `view b <values>` | Filter by B channel values / B 채널 값 필터 |
| `view wh` / `view bh` | Focus WH/BH region / WH/BH 영역 포커스 |
| `inspect <x> <y>` | Single cell detail / 단일 셀 상세 조회 |
| `tick now` / `tick goto <n>` | View tick state / 틱 상태 조회 |
| `region <name>` | Jump to named region / 영역 이동 |
| `zoom <1-8>` | Set zoom level / 줌 레벨 설정 |
| `pan <x> <y>` | Move viewport / 뷰포트 이동 |
| `quick wh\|bh\|all\|overlap` | Quick shortcuts / 빠른 단축키 |

## Virtual Paths / 가상 경로

| Path | Content / 내용 |
|------|----------------|
| `/proc/<pid>` | Process information / 프로세스 정보 |
| `/proc/self` | Current process / 현재 프로세스 |
| `/dev/null` | Discard sink / 폐기 싱크 |
| `/dev/canvas` | Canvas metadata / 캔버스 메타데이터 |
| `/wh/<tick>` | WhiteHole log entry / WhiteHole 로그 |
| `/bh/<id>` | BlackHole summary / BlackHole 요약 |

## Test Suite / 테스트 스위트

```
Phase 6:    6 tests  — Deterministic core engine / 결정론 코어 엔진
Phase 7:   10 tests  — Tervas canvas terminal / Tervas 캔버스 터미널
Phase 8:   18 tests  — Kernel primitives / 커널 프리미티브
Phase 9:   20 tests  — PixelCode VM / PixelCode 가상머신
Phase 10:  20 tests  — Userland / 사용자 영역
Bridge:    16 tests  — System bridges / 시스템 브릿지
Patch-B:   10 tests  — CanvasFS roundtrip / CanvasFS 왕복
Patch-C:   10 tests  — Real pipe + VM bridge / 실제 파이프 + VM 브릿지
Patch-D:   10 tests  — PixelCode self-hosting / PixelCode 자체 호스팅
Patch-E:   10 tests  — Timewarp + Branch + Merge / 시간여행 + 분기 + 병합
Patch-F:   10 tests  — Graphical rendering / 그래픽 렌더링
Patch-G:   10 tests  — Release quality gate / 릴리스 품질 게이트
Patch-H:   10 tests  — Deterministic stress test / 결정론 스트레스 테스트
───────────────────────────────────────────────
Total:    160 tests  — 0 failures / 실패 없음
```

## Project Structure / 프로젝트 구조

```
build/
├── src/                    — C source files (~50 modules) / C 소스 파일
│   └── tervas/             — Tervas terminal (6 modules) / Tervas 터미널
├── include/                — Header files (~57 headers) / 헤더 파일
├── tests/                  — Test suites (13 files) / 테스트 스위트
├── examples/               — Demo programs / 데모 프로그램
├── docs/                   — Design documents / 설계 문서
├── scripts/                — Build & release scripts / 빌드·릴리스 스크립트
├── tools/                  — Development tools / 개발 도구
├── devdict_site/           — Developer dictionary web UI / 개발자사전 웹 UI
│   ├── index.html          — Dashboard (Build Status + DevDict) / 대시보드
│   ├── data.js             — Symbol database (264 entries) / 심볼 데이터베이스
│   ├── data/               — JSON data (opcodes, regions, build status)
│   └── docs/               — Spec documents (HTML) / 스펙 문서
├── Makefile                — Build system / 빌드 시스템
└── VERSION                 — v1.0.8-patchH
```

## Build Requirements / 빌드 요구사항

- C11 compiler (clang or gcc) / C11 컴파일러
- POSIX threads (pthread) / POSIX 스레드
- Tested on / 테스트 환경: Linux x86_64, Android aarch64

## Benchmark / 벤치마크

```
Patch-H stress test: ~246M ticks/s on aarch64
50-run determinism: identical hash across all runs
500-tick throughput: < 0.001 ms/tick
```

## License / 라이선스

CanvasOS — sjpupro-lab

## Contact / 연락처

SJPUPRO@GMAIL.COM — Busan, South Korea / 부산, 대한민국

## Repository

https://github.com/sjpupro-lab/SJ-CANVAOS
