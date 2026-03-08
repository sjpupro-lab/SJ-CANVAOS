# Push to GitHub + GitLab

## Init (if needed)
```bash
git init
git add .
git commit -m "chore: initial import (phase6 release-ready)"
```

## Add remotes
```bash
git remote add github https://github.com/<USER>/<REPO>.git
git remote add gitlab  https://gitlab.com/<USER>/<REPO>.git
```

## Push
```bash
git branch -M main
git push -u github main
git push -u gitlab main
git push github --tags
git push gitlab --tags
```

## Credentials
Use PAT(HTTPS) or SSH keys.
