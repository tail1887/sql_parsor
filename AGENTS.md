# AGENTS.md

## Project Overview

This repository follows a docs-first workflow for building a C-based SQL processor.

Source of truth order:

1. `docs/01-product-planning.md`
2. `docs/02-architecture.md`
3. `docs/03-api-reference.md`
4. `docs/04-development-guide.md`
5. `README.md`

If documents conflict, follow the order above.

**Supplementary (not specification):** 주차별 자료는 `docs/weeks/` 아래에 둔다.

- `docs/weeks/WEEK6/assignment.md` — SQL 처리기 주차 과제 명세(지난 수요 코딩회 정리).
- `docs/weeks/WEEK6/learning-guide.md` — 위 주차 단계별 학습·자기 점검 (스펙 아님).
- `docs/weeks/WEEK7/assignment.md`, `docs/weeks/WEEK7/learning-guide.md` — B+ 트리 연계 과제·학습 틀 (스펙 아님).

## How Codex Should Work In This Repo

Before writing code, Codex should:

1. Read planning and architecture docs first.
2. Confirm task scope is inside MVP.
3. Check whether parser behavior, CLI contract, or storage format changes require doc updates.
4. Implement in small, focused tasks.
5. Update docs together when behavior changes.
6. Keep `README.md` short and demo-friendly.

## Codex-First Development Order

When building this project, use this order:

1. Project bootstrap (`include/`, `src/`, `tests/`, `data/`)
2. Tokenizer (lexer)
3. SQL parser for `INSERT`
4. SQL parser for `SELECT`
5. CSV storage read/write layer
6. Execution layer
7. CLI integration
8. Error handling hardening
9. Unit and integration tests
10. README and docs polish

## Task Sizing Rules

- One branch should focus on one clear outcome.
- Avoid unrelated changes in one task.
- Prefer prompts like "implement INSERT parser with tests".
- Do not run concurrent edits on same file without coordination.

## Collaboration Rules

- Each teammate uses a separate branch.
- Do not push directly to `main`; all changes must go through a PR.
- Merge to `main` is allowed only after CI passes on the PR.
- Parser behavior changes must update `docs/03-api-reference.md`.
- Data model/storage rule changes must update `docs/02-architecture.md`.
- Process updates must update `docs/04-development-guide.md`.
- `README.md` remains a demo summary, not a daily log.

## Branch Naming

Recommended branch types:

- `feature/<name>`
- `fix/<name>`
- `docs/<name>`
- `test/<name>`
- `chore/<name>`

## Commands For This Project

- Bootstrap: `mkdir include src tests data` (already present in repo layout)
- Dev run: `./build/sql_processor sample.sql` (Linux/macOS) or `build\Release\sql_processor.exe sample.sql` / `build\Debug\sql_processor.exe sample.sql` (Windows VS generator)
- Local test: `ctest --test-dir build --output-on-failure`
- Build: `cmake -S . -B build && cmake --build build`
- Lint/format: `clang-format -i src/*.c include/*.h tests/*.c`

## Definition of Done

A task is complete when:

- Code is implemented
- Relevant tests pass
- Related docs are updated
- PR summary explains what changed and why
- Known limitations are explicitly noted when unsolved

## Good Prompt Examples For Teammates

- `Read docs/01-product-planning.md and docs/03-api-reference.md, then implement INSERT parser with unit tests.`
- `Read docs/02-architecture.md and create csv_storage module only.`
- `Using docs/04-development-guide.md, split parser and executor work into safe branches.`
- `Review this branch for regressions against docs/03-api-reference.md.`
- `Polish README.md for demo based on implemented CLI flow.`

## Things Codex Should Avoid

- Inventing requirements not documented in the docs
- Editing unrelated files for a small feature
- Leaving parser behavior and docs out of sync
- Turning README into an internal scratchpad
- Implementing non-MVP SQL features without explicit approval