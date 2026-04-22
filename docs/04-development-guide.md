# 04. Development Guide

구현 단계 진입 시 본 문서와 `AGENTS.md` 를 함께 따른다.  
단계별로 무엇을 공부할지는 `[weeks/WEEK6/learning-guide.md](weeks/WEEK6/learning-guide.md)` (스펙 아님).

## 1) 브랜치 전략

권장 브랜치 타입:

- `feature/<name>`
- `fix/<name>`
- `docs/<name>`
- `test/<name>`
- `chore/<name>`

SQL 처리기에 맞춘 예시 브랜치 분리:

1. `feature/bootstrap-cmake` — CMakeLists, `main` 골격, 디렉터리
2. `feature/lexer` — tokenizer 단위
3. `feature/insert-parser` — INSERT AST
4. `feature/select-parser` — SELECT AST
5. `feature/csv-storage` — 경로 매핑, 읽기/append, 이스케이프
6. `feature/executor` — INSERT/SELECT 실행
7. `feature/cli` — 인자 처리, 파일 읽기, 문장 루프, 종료 코드
8. `test/integration-sql` — `tests/sql/*.sql` 픽스처
9. `docs/readme-demo` — 발표용 README 정리

브랜치 분리 기준:

- **한 브랜치 = 한 책임**(파서 변경과 저장 포맷 변경을 한 PR에 섞지 않기)
- 문서 변경이 동작 변경을 수반하면 **같은 PR**에 `docs/` 포함

## 2) 커밋 규칙

```text
<type>: <subject>
```

예시:

- `feat: add insert parser`
- `fix: handle escaped quotes in csv append`
- `docs: align select output format with 03-api-reference`
- `test: add lexer edge cases`

## 3) 구현 순서 (권장)

`AGENTS.md` 의 **Codex-First Development Order** 와 동일하게 유지한다.

1. 프로젝트 부트스트랩 (`include/`, `src/`, `tests/`, `data/`, CMake)
2. Lexer (tokenizer)
3. INSERT 파서
4. SELECT 파서
5. CSV storage 읽기/쓰기
6. Executor (INSERT/SELECT)
7. CLI 통합 (`sql_processor`)
8. 에러 처리·엣지 케이스
9. 단위·통합 테스트 보강
10. README 발표용 다듬기

## 3-1) WEEK8 API 서버 구현 순서

기존 엔진 위에 서버를 붙일 때는 아래 순서를 권장한다.

1. `sql_processor_run_text` + `SqlExecutionResult` 로 **단일 문장 구조화 결과** 노출
2. `executor` 에서 SELECT 결과를 **구조체 + TSV 렌더링** 으로 분리
3. `http_parser` 로 request line / header / JSON body 파싱
4. `response_builder` 로 엔진 결과 → JSON / HTTP 응답 생성
5. `engine_adapter` 로 전역 mutex 보호
6. `api_server` 로 `accept -> queue -> worker pool` 완성
7. `sql_api_server` 실행 파일, 서버 테스트, 문서 갱신

## 4) Codex / AI 태스크 요청 방식

좋은 요청 예시:

- `docs/01-product-planning.md 와 docs/03-api-reference.md 를 읽고 INSERT 파서와 단위 테스트만 추가해줘.`
- `docs/02-architecture.md 기준으로 csv_storage 모듈만 구현해줘.`
- `AGENTS.md 규칙에 맞춰 feature 브랜치 단위로 다음 작업을 쪼개줘.`

피해야 할 요청 예시:

- `프로젝트 전부 한 번에 만들어줘`
- `README만 보고 SQL 문법을 추측해서 구현해줘`

## 5) 테스트 전략

### 단위 테스트

- Lexer: 식별자, 숫자, 문자열, 따옴표 이스케이프, 주석, 세미콜론
- Parser: INSERT/SELECT 성공·실패 입력
- csv_storage: 한 줄 직렬화/역직렬화, 컬럼 수 검증
- `sql_processor_run_text`: 단일 문장 제약, `SqlExecutionResult` 메모리/메시지
- `http_parser`: request line, `Content-Length`, JSON `sql` 추출
- `response_builder`: SELECT/INSERT/error JSON 렌더링

### 통합(기능) 테스트

- `tests/sql/` 에 `.sql` 파일과 **기대 stdout/stderr/exit code** 를 정의한다.
- 실행 방법(예시):
  - CTest에서 `add_test` 로 `sql_processor` 에 픽스처 경로를 넘기고, `cmake -E compare_files` 또는 스크립트로 기대 출력과 비교한다.
  - 또는 테스트 전용 C 프로그램이 픽스처를 실행하고 assert 한다.

최소 체크리스트:

- INSERT 후 파일 끝에 행이 추가되는지
- SELECT 가 헤더·행을 기대와 일치하게 출력하는지
- 구문 오류 시 비제로 exit code
- 테이블 파일 없음 / 컬럼 수 불일치
- 문서(`docs/03-api-reference.md`)와 출력·exit code 동기화
- API 서버의 `POST /query` 가 SELECT/INSERT/오류/queue full 을 기대대로 반환하는지

## 6) PR 체크리스트

- 변경 목적이 명확하다
- 테스트를 추가했거나 기존 테스트가 통과한다
- parser 동작·CLI·저장 포맷이 바뀌면 `docs/03-api-reference.md` 또는 `docs/02-architecture.md` 를 갱신했다
- HTTP 계약이나 thread pool/queue 동작이 바뀌면 `docs/api-server-layer-spec.md` 를 갱신했다
- 리뷰어가 이해할 수 있게 범위를 설명했다
- `main` 직접 push 없이 PR로만 머지한다
- PR의 CI가 통과한 뒤에만 머지한다

## 6-1) 보호 브랜치 정책 (`main`)

- `main` 은 직접 push를 금지한다.
- `main` 으로의 변경은 PR을 통해서만 반영한다.
- PR 머지는 CI 체크(`CI / ci`) 성공 후에만 허용한다.

## 7) 마일스톤 예시

### Milestone 1. 기반 구성

- CMake 빌드 + `sql_processor` 실행 파일
- `main` 에서 인자 검증 및 플레이스홀더 동작
- CTest 골격

### Milestone 2. 핵심 기능

- INSERT/SELECT end-to-end
- `data/*.csv` 와 연동

### Milestone 3. 마감 준비

- 엣지 케이스·에러 메시지 정리
- 통합 테스트·README 데모 시나리오 동기화
- (선택) Stretch WHERE

### Milestone 4. API 서버

- `sql_api_server` 실행 파일
- `POST /query` JSON 응답
- bounded queue + worker pool + 전역 mutex
- HTTP 통합 테스트와 문서 동기화

## 8) 로컬 명령 참고

프로젝트 루트에서:

- 설정 및 빌드: `cmake -S . -B build && cmake --build build`
- 테스트: `ctest --test-dir build --output-on-failure`
- API 서버 실행: `./build/sql_api_server 8080 4`
- 포맷(선택): `clang-format -i src/*.c include/*.h tests/*.c`

실행 파일 경로는 `README.md` 의 Quick Start 를 따른다.
