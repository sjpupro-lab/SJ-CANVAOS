# CanvasOS

**A spatial operating system where memory, files, execution, and time converge on a single 2D canvas.**

CanvasOS runs programs by planting VM instructions as cells on a 1024×1024 canvas grid. Every state mutation is recorded in the WhiteHole log, enabling deterministic replay and time travel debugging. Branches split the canvas into parallel realities that can be independently evolved and merged back together.

## Quick Start

```bash
# Build and test
make test_all          # 150 tests across 11 test suites

# Run the live demo
make demo_patchF
./examples/demo_patchF

# Sanitizer build (ASan + UBSan)
make sanitize

# Release check (tests + sanitizer)
make release_check

# Package release
make release
```

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│  Presentation    shell · tervas · live demo · renderer  │
├─────────────────────────────────────────────────────────┤
│  Pixel Runtime   VM · PixelCode · pixel_loader          │
├─────────────────────────────────────────────────────────┤
│  Runtime         proc · fd · pipe · syscall             │
├─────────────────────────────────────────────────────────┤
│  Temporal        snapshot · branch · merge · timewarp   │
├─────────────────────────────────────────────────────────┤
│  CanvasFS        slot · payload · metadata · path       │
├─────────────────────────────────────────────────────────┤
│  Core Engine     scan · gate · active set · determinism │
└─────────────────────────────────────────────────────────┘
```

## Cell Data Model (ABGR)

Every cell on the 1024×1024 canvas is 8 bytes:

| Channel | Size | Role |
|---------|------|------|
| **A** | 32-bit | Address / spatial coordinate / lane ID |
| **B** | 8-bit | Behavior / opcode (VM instruction) |
| **G** | 8-bit | State / energy |
| **R** | 8-bit | Stream / data payload |

## Core Features

### Deterministic Execution
Same input → same output, guaranteed by DK rules:
- DK-1: Delta commit/merge only at tick boundary
- DK-2: Integer-only operations (no float in execution paths)
- DK-3: Merge applies deltas in cell_index ascending order

### PixelCode Self-Hosting
Utilities (echo, cat, info, hash, help) run as PixelCode programs planted on the canvas, not as C functions. The OS runs programs *on itself*.

### Time Travel (Timewarp)
`timewarp <tick>` restores the canvas to any previous state. Every mutation is recorded in the WhiteHole log for deterministic replay.

### Spatial Branching
`branch create <name>` splits reality. Each branch tracks its own write-set. `merge <a> <b>` combines branches with conflict detection.

### Live Visualization
The demo renders canvas state, VM activity, timeline, and branch status in real-time with ANSI-colored terminal output.

## Shell Commands

| Command | Description |
|---------|-------------|
| `echo <text>` | Print text (PixelCode) |
| `cat <path>` | Display file or virtual path (PixelCode) |
| `info` | System information (PixelCode) |
| `hash` | Canvas hash (PixelCode) |
| `help` | Command help (PixelCode) |
| `ps` | Process list |
| `kill <pid>` | Send signal |
| `ls [path]` | List directory |
| `cd <path>` | Change directory |
| `mkdir <name>` | Create directory |
| `rm <path>` | Remove entry |
| `snapshot <name>` | Save canvas state |
| `branch create\|list\|switch` | Branch management |
| `merge <a> <b>` | Merge branches |
| `timewarp <tick>` | Time travel |
| `timeline` | Show timeline status |
| `det on\|off` | Toggle determinism mode |
| `env` | Show variables |
| `exit` | Exit shell |

## Virtual Paths

| Path | Content |
|------|---------|
| `/proc/<pid>` | Process information |
| `/proc/self` | Current process |
| `/dev/null` | Discard sink |
| `/dev/canvas` | Canvas metadata |
| `/wh/<tick>` | WhiteHole log entry |
| `/bh/<id>` | BlackHole summary |
| `~` | Home directory |

## Build Requirements

- C11 compiler (gcc or clang)
- POSIX threads (pthread)
- Tested on: Linux x86_64, Android Termux (aarch64)

## Test Suite

```
Phase 6:   6 tests   — Deterministic core engine
Phase 7:  10 tests   — Tervas read-only terminal
Phase 8:  18 tests   — Kernel primitives
Phase 9:  20 tests   — PixelCode VM
Phase 10: 20 tests   — Userland
Bridge:   16 tests   — System bridges
Patch-B:  10 tests   — CanvasFS roundtrip
Patch-C:  10 tests   — Real pipe + VM bridge
Patch-D:  10 tests   — PixelCode self-hosting
Patch-E:  10 tests   — Timeline UX
Patch-F:  10 tests   — Graphical rendering
Patch-G:  10 tests   — Release quality gate
─────────────────────────────────────────
Total:   150 tests   — 0 failures
```

## Project Structure

```
src/            — C source files (~50 modules)
include/        — Header files (~50 headers)
include/tervas/ — Tervas terminal headers
src/tervas/     — Tervas terminal sources
tests/          — Test suites (12 test files)
examples/       — Demo programs
docs/           — Design documents and specs
scripts/        — Build and release scripts
tools/          — Development tools
devdict_site/   — Developer dictionary web UI
```

## License

CanvasOS — sjpupro-lab

## Repository

https://github.com/sjpupro-lab/Canvas-OS-0.1.git
