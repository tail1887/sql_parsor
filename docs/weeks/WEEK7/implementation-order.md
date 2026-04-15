# WEEK7 — 개발 순서 (확정안)

팀이 **이 순서를 그대로 따른다**고 가정한 로드맵이다. 단계가 끝날 때마다 `ctest`(또는 해당 단계 테스트)로 고정한 뒤 다음 단계로 넘어간다.

참고: 실행 흐름 그림은 `[sequences.md](sequences.md)`, 과제 체크는 `[assignment.md](assignment.md)`.

---

## 단계 0 — 작업 방식·문서 고정

코드는 `src/week7/` 등으로, 문서는 `docs/weeks/WEEK7/` 등으로 **직관적으로 분리**할 계획이면 **feature 브랜치 없이 `main`에 바로 머지**해도 된다. 팀이 한 가지만 정하면 된다.

- **옵션 A — `main` 직행**: 중간 커밋도 `main`에 쌓임. 폴더 분리가 잘 되어 있고 팀이 1~2명이면 단순하다.
- **옵션 B — feature 브랜치**: 발표 직전 `main`을 “작동하는 데모”로 고정해 두고 싶거나, 다른 브랜치 작업과 겹칠 때 유리하다.

공통으로 할 일:

- 정본 갱신이 필요해지면 `docs/02-architecture.md`, `docs/03-api-reference.md`를 **동시에** 수정하기로 팀 합의

**완료 기준**: 옵션 A/B 중 하나를 골랐고, `sequences.md`·본 문서를 팀이 읽었다.

---

## 단계 1 — B+ 트리 단독 모듈

- `include/week7/bplus_tree.h`, `src/week7/bplus_tree.c`
- 삽입·검색 + `bplus_insert_or_replace`
- `tests/test_bplus_tree.c` + CTest `bplus_tree_unit`

**완료 기준**: 달성.

---

## 단계 2 — 자동 `id` + CSV 한 줄 규칙

- `week7_index`, `csv_storage_data_row_count`, `csv_storage_column_count`
- append 직후 `row_ix = total - 1`

**완료 기준**: 달성.

---

## 단계 3 — INSERT 경로에 인덱스 삽입 연결

- `executor_execute_insert` → `week7_on_append_success`
- 실패 정책 문서화(`sequences.md`, `03-api`)

**완료 기준**: 달성.

---

## 단계 4 — 파서·AST: `WHERE id = 정수` (최소)

- `TOKEN_WHERE`, `TOKEN_EQ`, `SelectStmt` 필드
- `docs/03-api-reference.md`

**완료 기준**: 달성.

---

## 단계 5 — SELECT 실행 분기

- 인덱스 경로 vs 풀스캔, `tests/test_executor.c`

**완료 기준**: 달성.

---

## 단계 6 — 엣지·에러

- miss 헤더만, 비 PK WHERE 오류, `sql_trace` 토큰명

**완료 기준**: 달성.

---

## 단계 7 — 대량 벤치 + README

- `bench_bplus` 타깃(트리-only) + `compare` 서브모드
- README 표: `compare 1000000 10000` 실측(인덱스 vs 선형 스캔, I/O 제외)
- README에 벤치 명령

**완료 기준**: 달성.

---

## 한 줄 요약

**트리 단독 → id·CSV 규칙 → INSERT에 트리 연결 → WHERE 파싱 → SELECT 분기 → 엣지 → 벤치·README**

이 순서를 바꿀 경우, 보통 **단계 1과 2 사이** 또는 **4와 5 사이**에서만 조정하는 것이 리스크가 작다.