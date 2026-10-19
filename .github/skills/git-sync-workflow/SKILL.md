---
name: git-sync-workflow
description: 'Sync this repo between machines safely. Use when the user says git pull, code differs from another PC, fixes seem missing, branches, merge, or push.'
---

# Git sync workflow

Fixes are often on a feature branch, not `main`. Check before redoing work.

1. `git fetch --all --prune`
2. `git branch -a -vv` and `git log --oneline --all --graph -15`
3. Look for branches such as `fix/*` or `copilot/*` that are ahead of `main`.
4. `git status -sb` for local changes.

## Rules

- Prefer `git stash` over discarding local changes; drop the stash only after the user confirms.
- Do not set `git config` identity for the user; ask them to set `user.name` and `user.email`.
- Do not push, force-push, delete branches or reset --hard without explicit confirmation.
- `.vscode/` is gitignored; editor settings are not synced through git.
- After merging, run the `platformio-build-fix` build before pushing.
