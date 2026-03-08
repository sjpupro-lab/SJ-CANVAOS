# CanvasOS Changelog

All notable changes to CanvasOS are documented in this file.

## [v1.0.8-patchH] — 2026-03-08

### Deterministic OS Tester
- `tests/test_patchH.c`: 10-test deterministic OS stress/benchmark suite
- H1: DK-1 tick boundary guard lifecycle verification
- H2: DK-2/4/5 integer clamp (DK_CLAMP_U8/U16/U32) and noise-floor (DK_ABSORB_NOISE) primitives
- H3: DK-3 fixed reduction order (dk_cell_index strict ascending invariant)
- H4: Stress test — 50 identical runs produce identical canvas hash
- H5: Stress test — 100-tick sequence reproducibility across two independent contexts
- H6: Gate + mutation order invariance (DK-3 compliance)
- H7: CVP save/load 3-round stress (deterministic persistence)
- H8: Timewarp snapshot hash invariance + CVP-based state restoration
- H9: Benchmark — tick throughput measurement (ticks/s, ms/tick, final hash)
- H10: Full DK-1~5 integration regression gate
- `make patchH_test` and `make sanitize_patchH` build targets added
- Patch-H included in `make test_all`
- ASan/UBSan CLEAN
- 160 total tests, 0 failures

## [v1.0.7-patchG] — 2026-03-08

### Release Quality Gate
- ASan/UBSan sanitizer build target (`make sanitize`)
- Automated release packaging (`make release`, `scripts/release.sh`)
- Release quality gate (`make release_check`)
- gcc/clang build matrix support (`CC=clang make test_all`)
- 150 total tests, 0 failures
- README, CHANGELOG, and documentation updated

## [v1.0.6-patchF] — 2026-03-08

### Graphical Rendering + Live Demo
- Heatmap projection tracking recent cell modifications
- Canvas grid panel with ANSI color rendering
- Status panel (tick, hash, gates, modified cells, branch)
- Timeline panel (snapshots, branches, timeline bar with markers)
- VM activity panel (registers, PC, running state)
- Composite frame renderer with box-drawing layout
- Interactive demo (`make demo_patchF`) with 3 scenarios
- 10 new tests (F1–F10)

## [v1.0.5-patchE] — 2026-03-08

### Timewarp + Branch + Merge UX
- Snapshot table with create/find/find-by-name
- Write-set tracking per branch for conflict detection
- Merge with conflict/non-conflict result reporting
- Timeline unified interface (snapshot + branch + timewarp)
- Shell commands: snapshot, branch create/list/switch, merge, timeline
- WH audit records for all temporal operations
- 10 new tests (E1–E10)

## [v1.0.4-patchD] — 2026-03-08

### PixelCode Self-Hosting
- Utility registry (echo, cat, info, hash, help → PixelCode)
- Program planter: generates VM cells on canvas for each utility
- PXL_MODE_PIXELCODE / PXL_MODE_C_FALLBACK execution modes
- Shell integration: PixelCode utilities dispatched before C builtins
- VM_PRINT routed through fd_write(FD_STDOUT) for capture
- pixel_loader.c + canvasos_pixel_loader.h
- 10 new tests (PD1–PD10)

## [v1.0.3-patchBC] — 2026-03-08

### Patch-B: CanvasFS Roundtrip
- fd_open_bridged: path-aware file open with real CanvasFS slot allocation
- File name → FsKey registry for reopen persistence
- fd_file_read_slot / fd_file_write_slot: cursor-based CanvasFS I/O
- Append semantics (O_APPEND flag)
- 10 new tests (A1–A5, C1–C5)

### Patch-C: Real Pipe + VM Bridge
- fd_pipe_create: returns read_fd + write_fd pair
- FD_PIPE read/write via actual pipe_read/pipe_write
- fd_dup preserves pipe_id for duped fds
- VM_SEND/RECV/SPAWN use real proc/pipe when bridge active
- 10 new tests (B1–B5, D1–D5)

## [v1.0.2-patchA] — 2026-03-08

### Bridge Layer
- shell.c: 15 builtins, pipe, redirect, variables, comments
- syscall_bindings.c: 23 syscalls registered
- fd_canvas_bridge.c: FD ↔ CanvasFS bridge
- path_virtual.c: /proc, /dev, /wh, /bh, ~ resolution
- vm_runtime_bridge.c: VM ↔ proc/pipe/syscall connection
- detmode.c: WH audit logging implemented
- 16 bridge tests

## [v1.0.1-p10] — 2026-03-07

### Phase 6–10 Integrated Build
- Phase 6: Deterministic core engine (scan, gate, lane, merge, workers)
- Phase 7: Tervas read-only canvas terminal
- Phase 8: Kernel primitives (proc, signal, mprotect, pipe, syscall)
- Phase 9: PixelCode VM (fetch-decode-execute on canvas cells)
- Phase 10: Userland (fd, path, user, utils)
- 74 tests passing
