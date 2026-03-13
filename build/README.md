# CanvasOS Build

이 디렉토리에는 CanvasOS 전체 빌드 시스템이 포함되어 있습니다.

## Build

```bash
make test          # Core 5 test suites
make test_all      # All 15+ test suites (201+ tests)
make gui_test      # GUI system tests (17 tests)
make gui_bridge_test  # GUI-Engine bridge tests (24 tests)
make stream_test   # SJ-Stream Packet roundtrip
make tervas        # Tervas canvas terminal
make launcher      # Mobile launcher
make sanitize      # ASan + UBSan build
make release_check # Full release verification
make dashboard     # Tests + devdict status JSON
```

## Targets

| Target | Description |
|--------|-------------|
| `test` | Core engine tests (scan, gate, canvasfs, scheduler, cvp) |
| `test_all` | All 15+ test suites (201+ tests) |
| `gui_test` | GUI buffer/font/element/home/event tests |
| `gui_bridge_test` | GUI↔Engine bridge: vis modes, gate overlay, timeline, WH |
| `stream_test` | SJ-Stream 1024B packet roundtrip |
| `tervas` | Tervas canvas terminal |
| `cli` | canvasos_cli |
| `launcher` | Mobile launcher |
| `sanitize` | ASan/UBSan sanitizer build |
| `release_check` | test_all + sanitize |
| `clean` | Remove all binaries and artifacts |

## Diagram Images

`docs/` 폴더에 README용 다이어그램 이미지가 포함되어 있습니다:

- `img_cell_abgr.bmp` — Cell ABGR 구조
- `img_architecture.bmp` — 아키텍처 레이어
- `img_pipeline.bmp` — 실행 파이프라인
- `img_advantages.bmp` — 핵심 장점 6가지

이미지 재생성: `./tools/gen_readme_images`

## DevDictionary

```bash
cd devdict_site && python3 -m http.server 8080
# Open http://localhost:8080
```

See the root [README](../README.md) for full documentation.
