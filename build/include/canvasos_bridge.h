#pragma once
/*
 * canvasos_bridge.h — Phase-10: System Bridge Declarations
 *
 * Unified header for bridge layers:
 *   - fd_canvas_bridge: FD ↔ CanvasFS
 *   - syscall_bindings: Extended syscall table
 *   - path_virtual:     Virtual path resolution
 *   - vm_runtime_bridge: VM ↔ Proc/Pipe/Syscall
 */
#include "canvasos_fd.h"
#include "canvasos_path.h"
#include "canvasos_proc.h"
#include "canvasos_pipe.h"
#include "canvasos_vm.h"
#include "canvasos_timewarp.h"
#include "canvasos_detmode.h"
#include "canvasos_mprotect.h"
#include "canvasfs.h"

/* ── fd_canvas_bridge.c ──────────────────────────────── */
void fd_bridge_init(CanvasFS *fs);
void fd_bridge_set_volume(uint16_t volh);
int  fd_file_bind(FileDesc *fd, FsKey key, uint8_t flags);
int  fd_file_read_slot(void *ctx, FileDesc *fd, uint8_t *buf, uint16_t len);
int  fd_file_write_slot(void *ctx, FileDesc *fd, const uint8_t *buf, uint16_t len);
int  fd_open_bridged(void *ctx, PathContext *pc,
                     uint32_t pid, const char *path, uint8_t flags);
int  fd_bridge_stat(const char *path, PathContext *pc,
                    EngineContext *ctx, size_t *out_len);

/* ── fd.c extensions (Patch-B/C) ─────────────────────── */
void fd_bind_key(uint32_t pid, int fd, FsKey key);
void fd_set_pipe_table(void *pt);
int  fd_pipe_create(void *ctx, void *pipes,
                    uint32_t pid, int *read_fd, int *write_fd);

/* ── syscall_bindings.c ──────────────────────────────── */
void syscall_bind_context(ProcTable *pt, PipeTable *pipe,
                          PathContext *pc, TimeWarp *tw,
                          DetMode *dm, TileProtection *tp);
void syscall_register_phase10(void);

/* ── path_virtual.c ──────────────────────────────────── */
int  path_resolve_virtual(EngineContext *ctx, PathContext *pc,
                          const char *path, FsKey *out);
int  path_render_virtual(const ProcTable *pt, EngineContext *ctx,
                         FsKey key, char *buf, size_t cap);
int  path_is_virtual(FsKey key);
int  path_ls_virtual(FsKey dir, char names[][16], FsKey keys[], int max);

/* ── vm_runtime_bridge.c ─────────────────────────────── */
void vm_bridge_init(ProcTable *pt, PipeTable *pipe);
bool vm_bridge_is_active(void);
int  vm_exec_send(PipeTable *pipes, EngineContext *ctx, VmState *vm);
int  vm_exec_recv(PipeTable *pipes, EngineContext *ctx, VmState *vm);
int  vm_exec_spawn(ProcTable *pt, EngineContext *ctx, VmState *vm);
int  vm_exec_exit(ProcTable *pt, EngineContext *ctx, VmState *vm);
int  vm_exec_syscall(EngineContext *ctx, VmState *vm);
int  vm_create_pipe(EngineContext *ctx, VmState *sender, VmState *receiver);
int  vm_run_bridged(EngineContext *ctx, VmState *vm,
                    ProcTable *pt, PipeTable *pipes);
