CC     ?= gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
LDFLAGS = -lpthread

# Sanitizer flags (make sanitize)
SFLAGS = -Wall -Wextra -std=c11 -g -fsanitize=address,undefined -Iinclude
SLDFLAGS = -lpthread -fsanitize=address,undefined

ENGINE_SRC = src/canvasos_cli.c \
             src/engine.c src/scan_ringmh.c src/active_set.c src/canvasfs.c \
             src/canvasfs_bpage.c src/scheduler.c src/control_region.c src/engine_time.c \
             src/gate_ops.c src/canvasos_opcodes.c src/cvp_io.c src/engine_ctx.c \
             src/lane_exec.c src/bpage_table.c src/inject.c src/wh_io.c \
             src/canvas_lane.c src/canvas_merge.c src/canvas_multiverse.c src/canvas_branch.c \
             src/canvas_bh_compress.c src/workers.c src/canvas_gpu_stub.c src/sjptl_parser.c

CORE_SRC   = src/engine.c \
             src/scan_ringmh.c src/active_set.c src/canvasfs.c src/canvasfs_bpage.c \
             src/scheduler.c src/control_region.c src/engine_time.c src/gate_ops.c \
             src/canvasos_opcodes.c src/cvp_io.c src/engine_ctx.c src/lane_exec.c \
             src/bpage_table.c src/inject.c src/wh_io.c src/canvas_lane.c \
             src/canvas_merge.c src/canvas_multiverse.c src/canvas_branch.c src/canvas_bh_compress.c \
             src/workers.c src/canvas_gpu_stub.c src/sjptl_parser.c

ENGINE_BIN = canvasos

TEST_SCAN     = tests/test_scan
TEST_GATE     = tests/test_gate
TEST_CANVASFS = tests/test_canvasfs
TEST_SCHED    = tests/test_scheduler
TEST_CVP      = tests/test_cvp

.PHONY: all run test clean

all: $(ENGINE_BIN)

$(ENGINE_BIN): $(ENGINE_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

run: $(ENGINE_BIN)
	./$(ENGINE_BIN)

test: $(TEST_SCAN) $(TEST_GATE) $(TEST_CANVASFS) $(TEST_SCHED) $(TEST_CVP)
	./$(TEST_SCAN)
	./$(TEST_GATE)
	./$(TEST_CANVASFS)
	./$(TEST_SCHED)
	./$(TEST_CVP)

$(TEST_SCAN): tests/test_scan.c src/scan_ringmh.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_GATE): tests/test_gate.c src/active_set.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_CANVASFS): tests/test_canvasfs.c src/canvasfs.c src/canvasfs_bpage.c src/active_set.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_SCHED): tests/test_scheduler.c src/scheduler.c src/active_set.c src/canvasfs.c src/canvasfs_bpage.c src/engine_time.c src/gate_ops.c src/canvasos_opcodes.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST_CVP): tests/test_cvp.c src/cvp_io.c src/engine_ctx.c src/lane_exec.c src/bpage_table.c src/inject.c src/wh_io.c src/canvas_lane.c src/canvas_merge.c src/canvas_multiverse.c src/canvas_branch.c src/canvas_bh_compress.c src/workers.c src/canvas_gpu_stub.c src/sjptl_parser.c src/scheduler.c src/engine_time.c src/gate_ops.c src/canvasos_opcodes.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(ENGINE_BIN) $(TEST_SCAN) $(TEST_GATE) $(TEST_CANVASFS) $(TEST_SCHED) $(TEST_CVP) *.cvp
	rm -f tests/test_phase6 tests/test_tervas tests/test_phase8 tests/test_phase9
	rm -f tests/test_phase10 tests/test_bridge
	rm -f tests/test_patchB tests/test_patchC tests/test_patchD tests/test_patchE
	rm -f tests/test_patchF tests/test_patchG tests/test_patchG_sanitize
	rm -f examples/demo_patchF examples/hello_canvas
	rm -f canvasos_cli tervas sjterm canvasos_launcher

CLI_SRC = src/canvasos_cli.c src/scan_ringmh.c src/active_set.c \
          src/canvasfs.c src/canvasfs_bpage.c \
          src/scheduler.c src/control_region.c src/engine_time.c \
          src/gate_ops.c src/canvasos_opcodes.c \
          src/cvp_io.c src/engine_ctx.c src/lane_exec.c src/bpage_table.c src/inject.c src/wh_io.c src/canvas_lane.c src/canvas_merge.c src/canvas_multiverse.c src/canvas_branch.c src/canvas_bh_compress.c src/workers.c src/canvas_gpu_stub.c src/sjptl_parser.c 
CLI_BIN = canvasos_cli

cli: $(CLI_BIN)

$(CLI_BIN): $(CLI_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)


SJTERM_SRC = src/sjterm.c
SJTERM_BIN = sjterm


$(SJTERM_BIN): $(SJTERM_SRC) $(ENGINE_SRC)
	$(CC) $(CFLAGS) -o $(SJTERM_BIN) $(SJTERM_SRC) $(ENGINE_SRC) $(LDFLAGS)


tests/test_phase6: tests/test_phase6.c $(CORE_SRC)
	$(CC) $(CFLAGS) -o tests/test_phase6 tests/test_phase6.c $(CORE_SRC) $(LDFLAGS)

TEST_PHASE6    = tests/test_phase6

# ─────────────────────────────────────────────────────────────────
# Phase-7: Tervas Canvas Terminal
# ─────────────────────────────────────────────────────────────────

TERVAS_SRC = src/tervas/tervas_core.c     \
             src/tervas/tervas_bridge.c   \
             src/tervas/tervas_cli.c      \
             src/tervas/tervas_projection.c \
             src/tervas/tervas_render_ascii.c

TERVAS_BIN      = tervas
TEST_TERVAS     = tests/test_tervas
TERVAS_CFLAGS   = $(CFLAGS)

$(TERVAS_BIN): $(TERVAS_SRC) src/tervas/tervas_main.c $(CORE_SRC)
	$(CC) $(TERVAS_CFLAGS) -o $@ \
	    src/tervas/tervas_main.c $(TERVAS_SRC) $(CORE_SRC) $(LDFLAGS)

$(TEST_TERVAS): tests/test_tervas.c $(TERVAS_SRC) $(CORE_SRC)
	$(CC) $(TERVAS_CFLAGS) -o $@ \
	    tests/test_tervas.c $(TERVAS_SRC) $(CORE_SRC) $(LDFLAGS)

test_tervas: $(TEST_TERVAS)
	./$(TEST_TERVAS)

test_all: tests/test_phase6 tests/test_tervas tests/test_phase8 tests/test_phase9 tests/test_phase10 tests/test_bridge tests/test_patchB tests/test_patchC tests/test_patchD tests/test_patchE tests/test_patchF tests/test_patchG tests/test_patchH
	@echo "=== Phase-6 ==="
	@./tests/test_phase6
	@echo "=== Phase-7 Tervas ==="
	@./$(TEST_TERVAS)
	@echo "=== Phase-8 Userland ==="
	@./$(TEST_PHASE8)
	@echo "=== Phase-9 PixelCode VM ==="
	@./$(TEST_PHASE9)
	@echo "=== Phase-10 Userland ==="
	@./$(TEST_PHASE10)
	@echo "=== Bridge Layer ==="
	@./$(TEST_BRIDGE)
	@echo "=== Patch-B CanvasFS Roundtrip ==="
	@./$(TEST_PATCHB)
	@echo "=== Patch-C Real Pipe + VM ==="
	@./$(TEST_PATCHC)
	@echo "=== Patch-D PixelCode Self-Hosting ==="
	@./$(TEST_PATCHD)
	@echo "=== Patch-E Timewarp + Branch + Merge ==="
	@./$(TEST_PATCHE)
	@echo "=== Patch-F Graphical Rendering ==="
	@./$(TEST_PATCHF)
	@echo "=== Patch-G Release Quality Gate ==="
	@./$(TEST_PATCHG)
	@echo "=== Patch-H Deterministic OS Tester ==="
	@./$(TEST_PATCHH)

tervas: $(TERVAS_BIN)

.PHONY: test_tervas test_all tervas

# ─────────────────────────────────────────────────────────────────
# Examples
# ─────────────────────────────────────────────────────────────────

HELLO_CANVAS = examples/hello_canvas

$(HELLO_CANVAS): examples/hello_canvas.c $(TERVAS_SRC) $(CORE_SRC)
	$(CC) $(TERVAS_CFLAGS) -o $@ \
	    examples/hello_canvas.c $(TERVAS_SRC) $(CORE_SRC) $(LDFLAGS)

hello_canvas: $(HELLO_CANVAS)

.PHONY: hello_canvas

# ─────────────────────────────────────────────────────────────────
# Launcher — CanvasOS Mobile Launcher (unified shell)
# ─────────────────────────────────────────────────────────────────

LAUNCHER_BIN = canvasos_launcher

$(LAUNCHER_BIN): src/canvasos_launcher.c $(TERVAS_SRC) $(CORE_SRC)
	$(CC) $(TERVAS_CFLAGS) -o $@ \
	    src/canvasos_launcher.c $(TERVAS_SRC) $(CORE_SRC) $(LDFLAGS)

launcher: $(LAUNCHER_BIN)

.PHONY: launcher


TEST_PHASE8 = tests/test_phase8

$(TEST_PHASE8): tests/test_phase8.c src/proc.c src/signal.c src/mprotect.c src/pipe.c src/syscall.c src/detmode.c src/engine_ctx.c src/cvp_io.c src/gate_ops.c src/wh_io.c src/engine_time.c src/canvasos_opcodes.c src/scheduler.c src/active_set.c
	$(CC) $(CFLAGS) -o $@ tests/test_phase8.c src/proc.c src/signal.c src/mprotect.c src/pipe.c src/syscall.c src/detmode.c src/engine_ctx.c src/cvp_io.c src/gate_ops.c src/wh_io.c src/engine_time.c src/canvasos_opcodes.c src/scheduler.c src/active_set.c $(LDFLAGS)

phase8_test: $(TEST_PHASE8)
	./$(TEST_PHASE8)

.PHONY: phase8_test

# ─────────────────────────────────────────────────────────────────
# Phase-9: PixelCode VM
# ─────────────────────────────────────────────────────────────────

VM_SRC = src/vm.c src/pixelcode.c src/syscall.c src/vm_runtime_bridge.c

TEST_PHASE9 = tests/test_phase9

$(TEST_PHASE9): tests/test_phase9.c $(VM_SRC) $(CORE_SRC) src/proc.c src/signal.c src/pipe.c src/fd.c src/fd_canvas_bridge.c src/path.c src/path_virtual.c
	$(CC) $(CFLAGS) -o $@ tests/test_phase9.c $(VM_SRC) $(CORE_SRC) src/proc.c src/signal.c src/pipe.c src/fd.c src/fd_canvas_bridge.c src/path.c src/path_virtual.c $(LDFLAGS)

phase9_test: $(TEST_PHASE9)
	./$(TEST_PHASE9)

.PHONY: phase9_test

# ─────────────────────────────────────────────────────────────────
# Phase-10: Userland
# ─────────────────────────────────────────────────────────────────

P10_SRC = src/fd.c src/path.c src/user.c src/utils.c src/shell.c \
          src/fd_canvas_bridge.c src/path_virtual.c src/syscall_bindings.c \
          src/vm_runtime_bridge.c src/pixel_loader.c src/timeline.c \
          src/livedemo.c
P8_KERN = src/proc.c src/signal.c src/mprotect.c src/pipe.c src/syscall.c src/detmode.c src/timewarp.c
P9_VM   = src/vm.c src/pixelcode.c

TEST_PHASE10 = tests/test_phase10

$(TEST_PHASE10): tests/test_phase10.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_phase10.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

phase10_test: $(TEST_PHASE10)
	./$(TEST_PHASE10)

.PHONY: phase10_test

# ─────────────────────────────────────────────────────────────────
# Bridge Layer Extended Tests
# ─────────────────────────────────────────────────────────────────

TEST_BRIDGE = tests/test_bridge

$(TEST_BRIDGE): tests/test_bridge.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_bridge.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

bridge_test: $(TEST_BRIDGE)
	./$(TEST_BRIDGE)

.PHONY: bridge_test

# ─────────────────────────────────────────────────────────────────
# Patch-B: CanvasFS Roundtrip + Redirect
# ─────────────────────────────────────────────────────────────────

TEST_PATCHB = tests/test_patchB

$(TEST_PATCHB): tests/test_patchB.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchB.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchB_test: $(TEST_PATCHB)
	./$(TEST_PATCHB)

.PHONY: patchB_test

# ─────────────────────────────────────────────────────────────────
# Patch-C: Real Pipe + VM Bridge
# ─────────────────────────────────────────────────────────────────

TEST_PATCHC = tests/test_patchC

$(TEST_PATCHC): tests/test_patchC.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchC.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchC_test: $(TEST_PATCHC)
	./$(TEST_PATCHC)

.PHONY: patchC_test

# ─────────────────────────────────────────────────────────────────
# Patch-D: PixelCode Self-Hosting
# ─────────────────────────────────────────────────────────────────

TEST_PATCHD = tests/test_patchD

$(TEST_PATCHD): tests/test_patchD.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchD.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchD_test: $(TEST_PATCHD)
	./$(TEST_PATCHD)

.PHONY: patchD_test

# ─────────────────────────────────────────────────────────────────
# Patch-E: Timewarp + Branch + Merge UX
# ─────────────────────────────────────────────────────────────────

TEST_PATCHE = tests/test_patchE

$(TEST_PATCHE): tests/test_patchE.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchE.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchE_test: $(TEST_PATCHE)
	./$(TEST_PATCHE)

.PHONY: patchE_test

# ─────────────────────────────────────────────────────────────────
# Patch-F: Graphical Rendering + Live Demo
# ─────────────────────────────────────────────────────────────────

TEST_PATCHF = tests/test_patchF

$(TEST_PATCHF): tests/test_patchF.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchF.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchF_test: $(TEST_PATCHF)
	./$(TEST_PATCHF)

DEMO_PATCHF = examples/demo_patchF

$(DEMO_PATCHF): examples/demo_patchF.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ examples/demo_patchF.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

demo_patchF: $(DEMO_PATCHF)

.PHONY: patchF_test demo_patchF

# ─────────────────────────────────────────────────────────────────
# Patch-G: Release Quality Gate
# ─────────────────────────────────────────────────────────────────

TEST_PATCHG = tests/test_patchG

$(TEST_PATCHG): tests/test_patchG.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchG.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchG_test: $(TEST_PATCHG)
	./$(TEST_PATCHG)

# ─────────────────────────────────────────────────────────────────
# Patch-H: Deterministic OS Tester (stress + benchmark)
# ─────────────────────────────────────────────────────────────────

TEST_PATCHH = tests/test_patchH

$(TEST_PATCHH): tests/test_patchH.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(CFLAGS) -o $@ tests/test_patchH.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(LDFLAGS)

patchH_test: $(TEST_PATCHH)
	./$(TEST_PATCHH)

# Sanitizer build for Patch-H
SANITIZE_PATCHH_BIN = tests/test_patchH_sanitize

$(SANITIZE_PATCHH_BIN): tests/test_patchH.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(SFLAGS) -o $@ tests/test_patchH.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(SLDFLAGS)

sanitize_patchH: $(SANITIZE_PATCHH_BIN)
	@echo "=== Patch-H ASan/UBSan Test ==="
	@./$(SANITIZE_PATCHH_BIN)
	@echo "=== Patch-H Sanitizer: CLEAN ==="

.PHONY: patchH_test sanitize_patchH

# Sanitizer build (ASan + UBSan)
SANITIZE_BIN = tests/test_patchG_sanitize

$(SANITIZE_BIN): tests/test_patchG.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC)
	$(CC) $(SFLAGS) -o $@ tests/test_patchG.c $(P10_SRC) $(P8_KERN) $(P9_VM) $(CORE_SRC) $(SLDFLAGS)

sanitize: $(SANITIZE_BIN)
	@echo "=== ASan/UBSan Test ==="
	@./$(SANITIZE_BIN)
	@echo "=== Sanitizer: CLEAN ==="

# Release check (all tests + sanitizer)
release_check: test_all sanitize
	@echo ""
	@echo "╔══════════════════════════════════════╗"
	@echo "║   CanvasOS Release Check: ALL PASS   ║"
	@echo "╚══════════════════════════════════════╝"

# Release package
release:
	bash scripts/release.sh $$(cat VERSION)

# ─────────────────────────────────────────────────────────────────
# DevDictionary Dashboard — generate build status JSON
# ─────────────────────────────────────────────────────────────────

dashboard: test_all
	bash scripts/gen_build_status.sh
	@echo ""
	@echo "╔══════════════════════════════════════════╗"
	@echo "║  DevDict Dashboard: devdict_site/        ║"
	@echo "║  Open index.html → Build Status tab     ║"
	@echo "╚══════════════════════════════════════════╝"

.PHONY: dashboard patchG_test sanitize release_check release
