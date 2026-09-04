# Repository Instructions

## Branch policy

- Always work directly on `develop` unless the user explicitly instructs otherwise.
- Before making changes, verify the current branch with `git branch --show-current`.
- Do not create or switch to another branch, or create a worktree on another branch, unless the user explicitly requests it.
- If the checkout is not on `develop` and the user has not requested an exception, safely switch to `develop` before editing. If switching would risk existing work or is blocked, stop and ask the user; never discard changes or force a checkout.

## Wokwi quota

- Conserve the user's limited Wokwi CI minutes. Default routine verification to local host/tooling tests and firmware builds, which consume no Wokwi minutes.
- Select only Wokwi scenarios relevant to the changed behavior. State the selected scenarios and why they are needed before starting a cloud run.
- `tools/dev.ps1 all` and unfiltered `sim-test` use the supported local Velxio suite. Reserve full cloud suites for an explicit full-suite request or an agreed release checkpoint.
- Reuse captures from the current attested build for local inspection and comparison. Avoid repeated cloud runs that do not resolve a concrete verification gap.

## Approved simulation direction

- Velxio is the selected local simulator backend alongside host tests. Follow the approved strategy and integration record in `docs/superpowers/plans/2026-09-03-cyd-virtual-bench.md`.
- The maintained runner follows `docs/superpowers/plans/2026-09-03-velxio-local-runner.md`. Keep its runtime, DIO build, timing, touch, and display adapters pinned; retain explicit coverage gaps until their tests pass.
- Use Velxio for supported routine UI/navigation/simulated-behavior checks. Wokwi is for occasional targeted comparisons or agreed release checkpoints, and physical hardware remains required for real Wi-Fi/AP/NAPT and deployment acceptance.
- `sim-build`, `sim-test`, and `all` default to Velxio. Cloud execution requires `-Backend wokwi` plus `-Scenario NAME` or an explicitly requested `-FullSuite`. Never fall back to cloud.
- `sim-update-goldens -Run RUN_ID -Scenario NAME` promotes current recorded local captures without rerunning simulation. Review actual images before promotion.
