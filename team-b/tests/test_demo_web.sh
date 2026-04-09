#!/bin/sh

set -eu

if [ -n "${WEEK6_SQL_REPO_ROOT:-}" ]; then
    REPO_ROOT=$(CDPATH= cd -- "$WEEK6_SQL_REPO_ROOT" && pwd)
else
    SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
    REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
fi

cd "$REPO_ROOT"
export WEEK6_SQL_REPO_ROOT="$REPO_ROOT"
sed 's/\r$//' ./scripts/run_demo_samples.sh | sh >/dev/null
test -f "$REPO_ROOT/manual_runs/latest/web_demo/index.html"
test -f "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
test -f "$REPO_ROOT/manual_runs/latest/web_demo/styles.css"
test -f "$REPO_ROOT/manual_runs/latest/web_demo/app.js"

grep -q "SQL 처리기 발표 데모" "$REPO_ROOT/manual_runs/latest/web_demo/index.html"
grep -q "테이블 정의" "$REPO_ROOT/manual_runs/latest/web_demo/index.html"
grep -q "현재 테이블 상태" "$REPO_ROOT/manual_runs/latest/web_demo/index.html"
grep -q "크래프톤 정글 교육생 정보 DB" "$REPO_ROOT/manual_runs/latest/web_demo/app.js"
grep -q "302호 출입 기록 LOG" "$REPO_ROOT/manual_runs/latest/web_demo/app.js"
grep -q '"id": "01_happy_path"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"id": "02_select_student_by_id"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"id": "03_duplicate_student_id"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"id": "04_entry_log_unauthorized"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"id": "05_entry_log_missing_student"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"id": "1"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"id": "2"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"authorization": "T"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q '"entered_at": "2026-04-08 09:00:00"' "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q "failed to insert student: duplicate id 1" "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q "failed to insert entry log: unauthorized student id 2" "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"
grep -q "failed to insert entry log: student id 9 not found" "$REPO_ROOT/manual_runs/latest/web_demo/demo_data.js"

printf '%s\n' "demo web tests passed"
