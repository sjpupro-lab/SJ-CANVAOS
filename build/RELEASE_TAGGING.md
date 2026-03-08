# Version tagging (Git)

This repo uses a `VERSION` file.

```bash
echo "v1.0.1" > VERSION
git add VERSION
git commit -m "chore(release): v1.0.1"
git tag -a v1.0.1 -m "v1.0.1"
git push --follow-tags
```

Tip: date-based tags
- `vYYYY.MM.DD` (example: `v2026.03.04`)


## v1.0.1-p6
- Added GitHub Actions CI workflow (.github/workflows/ci.yml)
- Added GitLab CI pipeline (.gitlab-ci.yml)
- Added Termux build guide (docs/ANDROID_TERMUX.md)
- Added Termux build script (scripts/build_android_termux.sh)
