# Phase 6 + Phase 7 merge note

This package uses Phase 7 as the primary baseline and carries forward the unique
Phase 6 delta-buffer assets:

- `include/canvas_delta.h`
- `src/canvas_delta.c`
- `tests/test_delta_merge.c`

Notes:
- Shared files from Phase 7 remain authoritative unless explicitly patched here.
- Worker barrier portability was fixed so the tree can build on environments
  where `pthread_barrier_t` is unavailable.
- The Phase 6 delta-buffer APIs are included for follow-up integration, but are
  not yet wired into the default Phase 7 runtime path.
