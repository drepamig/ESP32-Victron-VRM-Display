# Repository Instructions

## Branch policy

- Always work directly on `develop` unless the user explicitly instructs otherwise.
- Before making changes, verify the current branch with `git branch --show-current`.
- Do not create or switch to another branch, or create a worktree on another branch, unless the user explicitly requests it.
- If the checkout is not on `develop` and the user has not requested an exception, safely switch to `develop` before editing. If switching would risk existing work or is blocked, stop and ask the user; never discard changes or force a checkout.
