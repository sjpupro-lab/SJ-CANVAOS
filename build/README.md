# CanvasOS Build

This directory contains the complete CanvasOS build system.

이 디렉토리에는 CanvasOS 전체 빌드 시스템이 포함되어 있습니다.

## Build

```bash
make test_all CC=clang    # Build + run all 160 tests
make tervas               # Build Tervas canvas terminal
make dashboard            # Run tests + generate devdict status
make sanitize             # ASan + UBSan build
make release_check        # Full release verification
```

## Targets

| Target | Description |
|--------|-------------|
| `test_all` | Build and run all 13 test suites (160 tests) |
| `tervas` | Build Tervas canvas terminal binary |
| `cli` | Build canvasos_cli |
| `launcher` | Build mobile launcher |
| `dashboard` | test_all + generate devdict_site/data/build_status.json |
| `sanitize` | ASan/UBSan sanitizer build |
| `release_check` | test_all + sanitize |
| `release` | Package release archive |
| `clean` | Remove all binaries and artifacts |

## DevDictionary

Open `devdict_site/index.html` in a browser, or:

```bash
cd devdict_site && python3 -m http.server 8080
# Open http://localhost:8080
```

See the root [README](../README.md) for full documentation.
