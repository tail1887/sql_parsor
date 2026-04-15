# 2026-04-15 Data Integrity Hardening

## Summary

This change hardens three data-integrity gaps in the WEEK7 path:

1. Duplicate `id` values in an `id` PK table no longer silently overwrite an earlier row reference.
2. Blank CSV rows are handled consistently by all CSV readers and row counters.
3. Corrupted non-integer `id` values no longer get accepted into the WEEK7 index.

## What Was Wrong

### 1. Duplicate `id` values

`week7_ensure_loaded()` used `bplus_insert_or_replace()`, so a repeated `id` replaced the earlier row reference. `SELECT ... WHERE id = ...` could therefore return only the last duplicate row even though a full table scan would reveal multiple matching rows.

### 2. Blank CSV rows

`csv_storage_read_table()` treated a blank data row as a malformed row, while `csv_storage_data_row_count()` skipped blank rows. That meant table loading, row counting, and indexed row lookups could disagree on the same file.

### 3. Corrupted `id` fields

`week7_ensure_loaded()` parsed stored ids with `strtoll(..., NULL, 10)`. Inputs such as `abc`, `12x`, or an empty field could slip through as partial or zero-like values and pollute the WEEK7 index.

## Why These Were Dangerous

- They could return wrong results without a clear error.
- The same table could behave differently depending on whether the query used the WEEK7 index path or a full scan path.
- Broken CSV data could look "mostly fine" while still contaminating internal state.

## What Changed

### Storage layer

- Added a shared blank-line check in `src/csv_storage.c`.
- `csv_storage_read_table()`, `csv_storage_read_table_row()`, and `csv_storage_data_row_count()` now all ignore whitespace-only rows in the same way.

### WEEK7 index loader

- Replaced permissive stored-id parsing with a strict integer parser in `src/week7/week7_index.c`.
- Duplicate ids now fail index loading instead of overwriting an older row reference.
- Load failure clears partial WEEK7 state so repeated attempts do not reuse a half-built cache.

### Repository sample data

- Normalized `data/users.csv` so the repository sample data no longer violates the new WEEK7 integrity rules.

## Regression Tests

Added `tests/test_data_integrity.c` with 150 total regression cases:

- 50 duplicate-id cases
- 50 blank-line cases
- 50 invalid-id cases

The blank-line cases intentionally vary blank-row position, count, trailing blanks, and whitespace-only rows. The invalid-id cases include empty fields, leading/trailing spaces, junk suffixes, decimal/scientific-looking inputs, tabs, and overflow values.

## Verification

All existing test binaries plus the new integrity suite were compiled and executed successfully.

Because `cmake` was not on PATH and Visual Studio CMake configuration failed in this environment with a Windows SDK permission error, verification used direct MSVC `cl.exe` builds after calling `vcvars64.bat`.

The executed coverage included:

- existing lexer/parser/storage/executor/integration/B+ tree tests
- the new 150-case integrity suite

## Iteration Notes

### Integration issue found while validating

After hardening WEEK7 loading, the repository sample `data/users.csv` immediately violated the new duplicate-id rule. That was not a bug in the new loader; it exposed that the sample data itself was inconsistent with a primary-key table. The sample file was updated to unique ids before re-running the full regression pass.

### Post-fix status

After the sample-data cleanup, the full manual regression run passed without further code changes.
