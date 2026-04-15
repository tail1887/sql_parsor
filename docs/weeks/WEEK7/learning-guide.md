# 학습 가이드 (WEEK7)

이 문서는 **MVP 규격이 아닙니다.** 동작·문법의 기준은 `docs/01`~`04`와` AGENTS.md`입니다.   주차 과제 한 줄 요약은` [assignment.md](assignment.md)`, **구현 순서·완료 기준**은` [implementation-order.md](implementation-order.md)`를 본다.

WEEK6 가이드와 같이, 아래는 **이번 주차 구현 순서**에 맞춰 무엇을 짚고 공부하면 좋은지 정리한 것입니다.

---

## 선행: 프로젝트 문서로 맥락 잡기

구현에 들어가기 전에 한 번 읽고 가면 단계별 공부가 연결됩니다.


| 순서  | 문서                                                         | 짚을 포인트                                                                    |
| --- | ---------------------------------------------------------- | ------------------------------------------------------------------------- |
| 1   | `[01-product-planning.md](../../01-product-planning.md)`   | INSERT/SELECT 범위, CSV 사전 존재 — **자동 `id`·WHERE 정책이 여기와 어긋나지 않는지**          |
| 2   | `[02-architecture.md](../../02-architecture.md)`           | §6.1~6.2 MVP 시퀀스, §6.3에서 WEEK7 상세는 `[sequences.md](sequences.md)`로 보낸다는 점 |
| 3   | `[03-api-reference.md](../../03-api-reference.md)`         | WEEK7: 첫 컬럼 `id` PK, `WHERE id = 정수`, miss 시 헤더만, 종료 코드                   |
| 4   | `[04-development-guide.md](../../04-development-guide.md)` | 브랜치·테스트·`ctest` 루프                                                        |
| 5   | `[sequences.md](sequences.md)`                             | INSERT/SELECT 시 Mermaid와 **실제 심볼명**(`week7_`*, `csv_storage_`*)           |


이후 단계 번호는 `[implementation-order.md](implementation-order.md)`의 **단계 1~7**과 맞춥니다.

---

## 단계 1 — B+ 트리 단독 모듈

**하는 일**: `include/week7/bplus_tree.h`, `src/week7/bplus_tree.c`에 SQL·CSV 의존 없이 **삽입·검색**(과제 범위에 삭제 없음). `tests/test_bplus_tree.c`로 단위 검증.

**공부 포인트**

- **왜 B+인가**: 리프에 키가 모이고 내부는 구간만 — 범위 스캔·디스크 페이지와의 대응(본 구현은 메모리지만 개념 연결).
- **차수·분할**: `BP_MAX_KEYS` 등 고정 차수에서 리프/내부 분할 시 키·자식 배열을 **어떻게 반으로 나누는지** — 오프바이원이 버그의 대부분.
- **`bplus_insert_or_replace`**: 동일 키 재삽입 시 payload(행 번호)만 갱신 — **CSV에 같은 `id`가 여러 행**일 때 “마지막 행”이 인덱스에 남아야 하는 이유.
- 테스트를 **executor 없이** 트리만 링크하는 이유(플랜: 단계 1에서 storage/executor 수정 금지).

**스스로 점검**

- 빈 트리, 한 키, 연속 split, 검색 miss/hit를 손으로 그린 트리와 코드 결과가 맞는가?
- `bplus_destroy` 후 누수·이중 해제가 없는가?

---

## 단계 2 — 자동 `id` + 행 참조(`row_ref`)

**하는 일**: 테이블 첫 컬럼이 `id`일 때 PK로 보고, `next_id`를 단조 증가시킨다. `append` 성공 후 **0-based 데이터 행 인덱스**를 확정한다(API A: `csv_storage_data_row_count` 등).

**공부 포인트**

- **row_ref 의미**: `csv_storage_read_table`로 읽은 `rows[i]`의 `i` — executor가 SELECT 한 행을 찾을 때의 논리 주소.
- **단일 프로세스 가정**: append 직후 row count로 인덱스를 구하는 방식이 **동시 쓰기 없을 때**만 안전하다는 전제.
- `week7_ensure_loaded`가 CSV를 읽어 **기존 최대 id**와 트리를 채우는 이유(재실행·다음 INSERT).

**스스로 점검**

- INSERT 한 번 후 CSV 첫 필드와 `next_id`가 기대와 같은가?
- 헤더가 `id`가 아닌 테이블에서는 자동 id·인덱스가 **켜지지 않는지**?

---

## 단계 3 — INSERT 경로에 인덱스 연결

**하는 일**: `executor_execute_insert`에서 append 성공 → `row_ref` 확정 → `week7_on_append_success`(내부에서 `bplus_insert_or_replace`) 순서 고정.

**공부 포인트**

- **순서가 깨지면**: 파일에는 행이 있는데 트리에 없거나, 반대로 불일치 발생.
- **실패 정책**: 인덱스 삽입 실패 시 CSV 롤백은 비범위일 수 있음 — `sequences.md`·`03-api-reference`에 **현재 동작 한 줄**으로 고정해 발표 시 설명.

**스스로 점검**

- INSERT 직후 같은 `id`로 트리 검색(또는 SELECT WHERE)이 **같은 행**을 가리키는가?

---

## 단계 4 — 파서·AST: `WHERE id = 정수`

**하는 일**: `SelectStmt`에 `has_where_id_eq`, `where_id_value` 등. `SELECT ... FROM t WHERE id = <int>` 만 허용, 그 외 WHERE는 **파싱 실패**.

**공부 포인트**

- WHERE를 executor가 아니라 **parser에서 제한**하는 이유(지원 문법을 좁게 명시).
- Lexer에 `WHERE`, `TOKEN_EQ` 추가와 AST 해제(`ast.c`) 동기화.

**스스로 점검**

- `WHERE name = 1`, `WHERE id > 1` 등은 **파싱 단계**에서 걸리는가?
- 성공 시 AST에 조건 값이 정확히 들어가는가?

---

## 단계 5 — SELECT 실행 분기

**하는 일**: `has_where_id_eq`이면 인덱스 루업 후 **해당 행만** stdout 포맷; 아니면 기존 `csv_storage_read_table` 풀스캔.

**공부 포인트**

- **인덱스 hit vs miss**: miss 시 헤더만(성공 0) — `03-api-reference`와 일치.
- **id PK가 아닌 테이블**에 WHERE id 사용 시 실행 오류로 처리하는 이유(인덱스 없음과 혼동 방지).
- 선택 과제: `read_row_by_index`로 I/O 축소 — 없으면 **풀 로드 후 한 행만 출력**해도 동작은 맞으나, 벤치 공정성은 문서에 명시.

**스스로 점검**

- 동일 데이터에 대해 `SELECT * FROM t`와 `SELECT * FROM t WHERE id = k`(존재하는 k)의 **해당 행 내용**이 일치하는가?

---

## 단계 6 — 엣지·에러

**하는 일**: 없는 id, 빈 테이블, 파싱 실패, I/O 실패 등 **종료 코드·stderr**를 정본과 맞춤. `sequences.md`에 lookup miss 한 줄 확정.

**공부 포인트**

- split 경계·빈 트리·첫 INSERT 등 **인덱스와 storage 상태 조합**을 나열해 두고 테스트 또는 “스킵 사유”를 남긴다.

**스스로 점검**

- 팀이 정한 엣지 목록에 대해 테스트 또는 문서상 스킵 이유가 있는가?

---

## 단계 7 — 대량 벤치 + README

**하는 일**: CMake `bench_bplus`(CTest 비필수). `bench_bplus [n]`, `bench_bplus compare n k`로 삽입·룩업·선형 스캔 비교. README에 **환경·재현 명령·표**(또는 팀 로컬 측정 안내).

**공부 포인트**

- `clock()`의 한계(해상도·최적화·콜드 스타트) — 발표에서는 **동일 바이너리·동일 명령** 재현을 강조.
- `compare`는 **CPU 룩업 vs 배열 스캔**만 격리하고, SQL 1M 전체 경로는 I/O가 지배적임을 구분해 설명.

**스스로 점검**

- 동료가 README만 보고 `bench_bplus`를 빌드·실행할 수 있는가?

---

## 한눈에: 단계 ↔ 이 레포 파일


| 단계  | 구현 초점          | 자주 대조할 문서·코드                                          |
| --- | -------------- | ----------------------------------------------------- |
| 1   | B+ 트리 단독       | `bplus_tree.c` / `bplus_tree.h`, `test_bplus_tree.c`  |
| 2   | 자동 id, row_ref | `week7_index.c`, `csv_storage.h` (`data_row_count` 등) |
| 3   | INSERT 후 인덱스   | `executor.c`, `sequences.md` §1                       |
| 4   | WHERE 파싱       | `parser.c`, `ast.h`, `03-api-reference.md`            |
| 5   | SELECT 분기      | `executor.c`, `week7_index.h`                         |
| 6   | 엣지·에러          | `03-api-reference.md`, 테스트                            |
| 7   | 벤치·README      | `bench_bplus.c`, 루트 `README.md`                       |

### 브라우저 실습 (`demo/`)

저장소 루트에서 `demo/` 로 이동해 `npm start` 한 뒤 `http://localhost:4010/week7.html` — **위와 같은 단계 1~7** 네비, 단계별 공부/점검 문구, SQL 프리셋(단계 1·7은 터미널 `ctest` / `bench_bplus` 명령 안내)이 붙어 있다.

---

## 용어 메모

- **row_ref / payload**: 0-based 데이터 행 인덱스(`CsvTable.rows` 인덱스).
- **id_pk**: 헤더 첫 셀 이름이 `id`(ASCII 대소문자 무시).

---

## 외부로 깊게 파고들 때 (막힐 때만)

- B 트리 vs B+ 트리, 분할·재분배 그림 — 블로그·강의 노트 검색 시 **“in-memory B+ tree insert split”** 키워드.
- Crafting Interpreters 등은 WEEK6 단계(lexer/parser)와 더 직접 맞닿습니다. WEEK7은 **자료구조 + 기존 파이프라인 훅**이 중심입니다.