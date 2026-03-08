# CanvasOS v1.0.2-patchA Release Notes

**Date:** 2026-03-08
**Base:** CanvasOS v1.0.1-p10 merged verified
**Scope:** Bridge layer completion, syscall extension, shell implementation

---

## Summary

This release fills the five critical gaps identified in the v1.0.1-p10 audit,
transforming the integrated research prototype into a product-grade system.
All 90 tests pass (6+10+18+20+20+16), including 16 new bridge layer tests.

## New Files

| File | Purpose |
|------|---------|
| `src/shell.c` | Full shell implementation with builtins, pipes, redirection, variables |
| `src/syscall_bindings.c` | Registers 23 syscalls covering file, process, IPC, gate, and special ops |
| `src/fd_canvas_bridge.c` | Connects fd table with CanvasFS for real file I/O roundtrip |
| `src/path_virtual.c` | Virtual path resolution for /proc, /dev, /wh, /bh, ~ |
| `src/vm_runtime_bridge.c` | Connects VM opcodes (SEND/RECV/SPAWN) to actual proc/pipe systems |
| `include/canvasos_bridge.h` | Unified header for all bridge declarations |
| `tests/test_bridge.c` | 16 extended tests covering all bridge features |

## Modified Files

| File | Change |
|------|--------|
| `src/detmode.c` | WH audit logging implemented (was TODO) |
| `src/utils.c` | `cmd_cat` now reads virtual and regular paths |
| `src/path.c` | Integrates virtual path resolution as fallback |
| `src/vm.c` | VM_SEND/RECV/SPAWN use bridge when active, WH fallback otherwise |
| `src/fd.c` | FD_FILE read/write now call CanvasFS bridge |
| `src/canvas_branch.c` | branch_commit_delta, branch_merge, branch_scan_y_range implemented |
| `src/canvas_multiverse.c` | mve_tick executes lane_tick_all instead of stub |
| `Makefile` | Updated build targets for new sources and bridge test |

## Patch Details by Priority

### Priority 1: Shell Implementation
The shell provides builtins (ps, kill, ls, cd, mkdir, rm, cat, echo, hash,
info, det, timewarp, env, help, exit), pipe execution (cmd_a | cmd_b),
output redirection (cmd > file), environment variables (VAR=value, $VAR
expansion), and comment support (#).

### Priority 2: Syscall Extension Bindings
23 syscalls registered: SYS_OPEN/READ/WRITE/CLOSE/SEEK (file I/O),
SYS_MKDIR/RM (directory), SYS_SPAWN/EXIT/WAIT/KILL/SIGNAL (process),
SYS_PIPE/DUP (IPC), SYS_GATE_OPEN/GATE_CLOSE/MPROTECT (gate/protection),
SYS_GETPPID/HASH (info), SYS_TIMEWARP/DET_MODE/SNAPSHOT (CanvasOS special).

### Priority 3: FD ↔ CanvasFS Bridge
fd_file_read_slot and fd_file_write_slot connect file descriptors to
CanvasFS slot payloads using cursor-based sequential access with
read-modify-write for whole-slot operations.

### Priority 4: Virtual Path Layer
Resolves /proc/<pid> (process info), /proc/self, /dev/null (discard),
/dev/canvas (canvas metadata), /wh/<tick> (WhiteHole log entry),
/bh/<id> (BlackHole summary), and ~ (home directory).

### Priority 5: VM Runtime Bridge
VM_SEND writes to actual pipes via pipe_write. VM_RECV reads from actual
pipes via pipe_read. VM_SPAWN creates real processes via proc_spawn.
All operations log to WH for determinism replay. Bridge is opt-in:
inactive bridge preserves backward-compatible WH-only behavior.

### Phase 6 Enhancements
branch_commit_delta records deltas to WH and applies to canvas.
branch_merge records merge events to WH. branch_scan_y_range executes
cells within branch's spatial bounds. mve_tick executes all active lanes
via lane_tick_all.

## Test Results

```
Phase-6:  PASS 6  / FAIL 0
Phase-7:  PASS 10 / FAIL 0
Phase-8:  PASS 18 / FAIL 0
Phase-9:  PASS 20 / FAIL 0
Phase-10: PASS 20 / FAIL 0
Bridge:   PASS 16 / FAIL 0
─────────────────────────
Total:    PASS 90 / FAIL 0
```

## Build

```bash
make test_all      # Full test suite
make bridge_test   # Bridge tests only
make launcher      # Mobile launcher
```

## What Remains (Future Patches)

- Patch-B: Full CanvasFS roundtrip with slot allocation
- Patch-C: VM bridged execution with trace/watch/debug
- Patch-D: Userland utilities (cp, top, source scripts, background jobs)
- Patch-E: P6 advanced extensions (multiverse fork/merge, BH compress/decompress)
