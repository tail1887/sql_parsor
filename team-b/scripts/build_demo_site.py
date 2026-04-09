#!/usr/bin/env python3

import json
import os
import shutil
import struct
import sys
from datetime import datetime, timedelta
from pathlib import Path


CASE_METADATA = [
    {
        "id": "01_happy_path",
        "short_label": "샘플 01",
        "title": "정상 흐름",
        "purpose": "학생 등록, 학생 조회, 입장 기록 등록, 입장 기록 조회가 모두 성공하는 케이스",
    },
    {
        "id": "02_select_student_by_id",
        "short_label": "샘플 02",
        "title": "학생 단건 조회",
        "purpose": "WHERE id = 2 로 학생 한 줄만 조회하는 케이스",
    },
    {
        "id": "03_duplicate_student_id",
        "short_label": "샘플 03",
        "title": "중복 학생 ID",
        "purpose": "같은 학생 id 를 두 번 INSERT 해서 duplicate id 에러를 만드는 케이스",
    },
    {
        "id": "04_entry_log_unauthorized",
        "short_label": "샘플 04",
        "title": "권한 없는 학생",
        "purpose": "authorization=F 인 학생이 입장 기록을 남기려 할 때 실패하는 케이스",
    },
    {
        "id": "05_entry_log_missing_student",
        "short_label": "샘플 05",
        "title": "없는 학생 ID",
        "purpose": "학생 테이블에 없는 id 로 입장 기록을 남기려 할 때 실패하는 케이스",
    },
]


def read_text_if_exists(path: Path) -> str:
    if not path.exists():
        return ""
    return path.read_text(encoding="utf-8")


def relative_link(from_dir: Path, to_path: Path) -> str:
    return os.path.relpath(to_path, start=from_dir)


def build_command(repo_root: Path, case_id: str) -> str:
    del repo_root
    return f"./sql_processor manual_samples/{case_id}.sql"


def parse_student_csv_rows(text: str) -> list[dict[str, str]]:
    rows = []

    if not text:
        return rows

    for line in text.splitlines()[1:]:
        parts = line.split(",")

        if len(parts) != 4:
            continue

        rows.append(
            {
                "id": parts[0],
                "name": parts[1],
                "class": parts[2],
                "authorization": parts[3],
            }
        )

    return rows


def format_timestamp(timestamp: int) -> str:
    base = datetime(1970, 1, 1)
    return (base + timedelta(seconds=timestamp)).strftime("%Y-%m-%d %H:%M:%S")


def parse_entry_log_rows(path: Path) -> list[dict[str, str]]:
    rows = []

    if not path.exists():
        return rows

    data = path.read_bytes()
    record_size = 12

    if len(data) % record_size != 0:
        return rows

    for offset in range(0, len(data), record_size):
        entered_at = struct.unpack_from("<q", data, offset)[0]
        student_id = struct.unpack_from("<i", data, offset + 8)[0]
        rows.append(
            {
                "entered_at": format_timestamp(entered_at),
                "id": str(student_id),
            }
        )

    return rows


def build_case_payload(repo_root: Path, output_root: Path, site_root: Path, meta: dict[str, str]) -> dict[str, object]:
    case_dir = output_root / meta["id"]
    query_path = case_dir / "query.sql"
    stdout_path = case_dir / "stdout.txt"
    stderr_path = case_dir / "stderr.txt"
    exit_code_path = case_dir / "exit_code.txt"
    student_csv_path = case_dir / "data" / "student.csv"
    entry_log_bin_path = case_dir / "data" / "entry_log.bin"
    entry_log_hex_path = case_dir / "entry_log.bin.hex.txt"

    student_present = student_csv_path.exists()
    entry_present = entry_log_bin_path.exists()
    exit_code_text = read_text_if_exists(exit_code_path).strip() or "0"
    student_csv_text = read_text_if_exists(student_csv_path)
    entry_log_hex_text = read_text_if_exists(entry_log_hex_path)
    student_rows = parse_student_csv_rows(student_csv_text)
    entry_rows = parse_entry_log_rows(entry_log_bin_path)

    artifacts = [
        {
            "label": "query.sql",
            "href": relative_link(site_root, query_path),
        },
        {
            "label": "stdout.txt",
            "href": relative_link(site_root, stdout_path),
        },
        {
            "label": "stderr.txt",
            "href": relative_link(site_root, stderr_path),
        },
    ]

    if student_present:
        artifacts.append(
            {
                "label": "data/student.csv",
                "href": relative_link(site_root, student_csv_path),
            }
        )

    if entry_present:
        artifacts.append(
            {
                "label": "data/entry_log.bin",
                "href": relative_link(site_root, entry_log_bin_path),
            }
        )
        artifacts.append(
            {
                "label": "entry_log.bin.hex.txt",
                "href": relative_link(site_root, entry_log_hex_path),
            }
        )

    return {
        "id": meta["id"],
        "shortLabel": meta["short_label"],
        "title": meta["title"],
        "purpose": meta["purpose"],
        "exitCode": int(exit_code_text),
        "command": build_command(repo_root, meta["id"]),
        "sqlText": read_text_if_exists(query_path),
        "stdoutText": read_text_if_exists(stdout_path),
        "stderrText": read_text_if_exists(stderr_path),
        "studentCsv": {
            "present": student_present,
            "text": student_csv_text,
            "rows": student_rows,
        },
        "entryLog": {
            "present": entry_present,
            "sizeBytes": entry_log_bin_path.stat().st_size if entry_present else 0,
            "hexText": entry_log_hex_text,
            "rows": entry_rows,
        },
        "links": {
            "query": relative_link(site_root, query_path),
            "stdout": relative_link(site_root, stdout_path),
            "stderr": relative_link(site_root, stderr_path),
            "studentCsv": relative_link(site_root, student_csv_path) if student_present else "",
            "entryBin": relative_link(site_root, entry_log_bin_path) if entry_present else "",
            "entryHex": relative_link(site_root, entry_log_hex_path) if entry_present else "",
        },
        "artifacts": artifacts,
    }


def main() -> int:
    if len(sys.argv) != 3:
        print("usage: build_demo_site.py <repo_root> <output_root>", file=sys.stderr)
        return 1

    repo_root = Path(sys.argv[1]).resolve()
    output_root = Path(sys.argv[2]).resolve()
    source_root = repo_root / "demo_web"
    site_root = output_root / "web_demo"

    if site_root.exists():
        shutil.rmtree(site_root)
    site_root.mkdir(parents=True)

    for asset_name in ("index.html", "styles.css", "app.js"):
        shutil.copy2(source_root / asset_name, site_root / asset_name)

    payload = {
        "title": "SQL 처리기 발표 데모",
        "generatedAt": datetime.now().isoformat(timespec="seconds"),
        "links": {
            "overview": relative_link(site_root, output_root / "DEMO_OVERVIEW.md"),
            "manualSamplesReadme": relative_link(site_root, repo_root / "manual_samples" / "README.md"),
        },
        "cases": [
            build_case_payload(repo_root, output_root, site_root, meta)
            for meta in CASE_METADATA
        ],
    }

    demo_data_path = site_root / "demo_data.js"
    demo_data_path.write_text(
        "window.SQL_PROCESSOR_DEMO = "
        + json.dumps(payload, ensure_ascii=False, indent=2)
        + ";\n",
        encoding="utf-8",
    )

    print(site_root / "index.html")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
