# eositis project rules

These rules apply to all agent work on this Bramble tree unless the user explicitly overrides them in the current message.

## 1. Session activity log

- **File:** `docs/eositis/SESSION-LOG.md`
- **When:** At the **start** of a task, skim the last entry for context. At the **end** of every completed task (success, partial, or blocked), append one dated entry.
- **Entry format:** Date, user request (one line), actions taken, files touched, test/run commands, outcome, blockers, link to transcript if useful.
- Do not delete or rewrite historical entries; only append.

## 2. Change log

- **File:** `docs/eositis/CHANGELOG.md`
- **When:** After any change that alters tracked source, scripts, or project rules, add or update an entry under **Unreleased** (or the dated section for that commit).
- Each item: **what** changed, **why** (firmware symptom, test failure, or user goal).
- On each task-commit, move relevant **Unreleased** bullets into the new commit section.

## 3. Commit on task completion

- **When:** After finishing a user-requested task (not after every intermediate tool call).
- **What to commit:** Only files that belong to that task; never commit secrets, `.env`, or debug junk paths under `~/Documents/junk/`.
- **Message:** 1–2 sentences on *why*; body may list major files or behaviors.
- **Report to user:** State commit hash, branch, and a short summary of change actions.
- **Conflict with global rules:** For this repo, eositis rule #3 overrides “commit only when asked” in the user’s global Cursor rules.

## 4. Build and test expectations

- After emulator changes: `make -C build bramble bramble_tests` and run `./build/bramble_tests` when feasible.
- MegaFlash runs: `-arch m33 -clock 150 -cores 2` and `scripts/megaflash-bus.stub` when validating dual-core / bus behavior.
