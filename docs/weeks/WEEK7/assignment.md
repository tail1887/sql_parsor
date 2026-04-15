# WEEK7 과제 명세 (B+ 트리 인덱스)

> 동작·문법의 정본: `docs/03-api-reference.md` §4.1·§4.2 WEEK7 절, `docs/02-architecture.md` §6.

## 1. 목표 한 줄

기존 C SQL 처리기에 **메모리 기반 B+ 트리**(최대 3키 리프, 내부 노드 분할)를 붙이고, CSV 첫 컬럼이 `id` 인 테이블에 대해 **자동 증가 ID** + `**WHERE id = <정수>`** 시 인덱스 룩업으로 조회한다.

## 2. 범위

### 2.1 필수

- B+ 트리: 삽입·검색, `bplus_insert_or_replace`(CSV 로드 시 중복 id → 마지막 행 기준). 삭제 없음.
- INSERT 시 첫 컬럼 `id` 테이블: 자동 ID + append 후 `week7_on_append_success` 로 인덱스 등록
- `SELECT … WHERE id = <정수>`: `week7_lookup_row` + `bplus_search` 경로
- 그 외 SELECT: 기존 `csv_storage_read_table` 풀스캔
- **100만 건 이상** 트리 삽입 후 **인덱스 룩업 vs 선형 스캔** 비교 — `bench_bplus compare 1000000 <k>` + README 표(전체 SQL 1M 경로는 I/O 분리를 위해 벤치 도구로 근사)

### 2.2 품질

- 단위 테스트: `test_bplus_tree`, `parser_select`(WHERE), `executor`(WHERE hit/miss)
- 기존 CTest 회귀 통과
- 엣지: `WHERE id` miss 시 헤더만 출력; `id` 비PK 테이블에 WHERE 사용 시 실행 오류

### 2.3 발표 (README 기준, 4분 + Q&A 1분)

- README: WEEK7 기능 요약, `bench_bplus` 사용법
- 실측 표: 루트 `README.md` WEEK7 벤치 절(예: `compare 1000000 10000`)

## 3. 비범위 / 보류

- 디스크 페이지, 다중 인덱스, 동시성, 인덱스 삽입 실패 시 CSV 롤백
- `WHERE` 의 다른 컬럼·연산자
- 인덱스 영속화(프로세스 종료 시 메모리 인덱스 소실 — 재기동 시 `week7_ensure_loaded` 가 CSV에서 재구축)

## 4. 연동 포인트 (구현 맵)


| 단계           | 파일/모듈                                                                                                                          |
| ------------ | ------------------------------------------------------------------------------------------------------------------------------ |
| B+ 트리        | `[include/week7/bplus_tree.h](../../include/week7/bplus_tree.h)`, `[src/week7/bplus_tree.c](../../src/week7/bplus_tree.c)`     |
| 인덱스·자동 ID    | `[include/week7/week7_index.h](../../include/week7/week7_index.h)`, `[src/week7/week7_index.c](../../src/week7/week7_index.c)` |
| CSV 보조       | `[csv_storage_data_row_count](../../include/csv_storage.h)`, `csv_storage_column_count`                                        |
| Executor     | `[src/executor.c](../../src/executor.c)`                                                                                       |
| Parser·Lexer | `[src/parser.c](../../src/parser.c)`, `[include/lexer.h](../../include/lexer.h)`                                               |
| 벤치           | `[src/bench_bplus.c](../../src/bench_bplus.c)`, CMake 타깃 `bench_bplus`                                                         |


## 5. 수용 기준 (Definition of Done)

- `ctest --test-dir build --output-on-failure` 전부 통과(또는 동일 구성 `build-ninja`)
- `docs/03-api-reference.md` 가 실제 SQL·동작과 일치
- README에 `bench_bplus` 재현 방법 명시

## 6. 차별화 아이디어 (선택)

- `sql_processor_trace` 로 WHERE 경로 토큰 확인
- 단일 행만 읽는 `csv_storage` API로 I/O 최적화