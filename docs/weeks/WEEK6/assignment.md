# WEEK6 과제 명세 (SQL 처리기)

> **수요 코딩회 — 지난 주차** 공식 과제를 이 레포 구조에 맞게 정리한 문서입니다. 세부 동작·문법의 정본은 `docs/01-product-planning.md` ~ `docs/04-development-guide.md`입니다.

## 1. 목표 한 줄

텍스트 파일로 작성된 SQL을 **CLI**로 SQL 처리기에 전달하고, **파싱 → 실행 → 파일 저장·조회**까지 이어지는 **INSERT / SELECT** 최소 기능을 C로 구현한다.

## 2. 범위

### 2.1 필수

- [ ] **CLI**: SQL이 담긴 **텍스트 파일**을 커맨드라인 인자 등으로 처리기에 넘길 수 있을 것
- [ ] **SQL 파싱**: 입력 SQL 문장을 분석해 구조화(AST 등 팀 설계)
  - 최소 지원: **INSERT**, **SELECT**
- [ ] **실행(Execution)**
  - **INSERT**: 파싱 결과를 바탕으로 **파일에 데이터 추가**
  - **SELECT**: **파일에서 데이터를 읽어** 출력(형식은 정본 문서·README와 일치)
- [ ] **파일 기반 DB**: 논리 테이블(또는 동일 개념)마다 파일로 관리
  - 저장 포맷: **CSV / binary / custom 등 자유 설계**
- [ ] **전제**: schema·table은 이미 존재 — **CREATE TABLE 미구현 허용**
- [ ] **기술 조건**: 구현 언어 **C**
- [ ] **학습 vs 구현**: 구현·AI 활용 우선. 다만 **핵심 로직**은 설명 가능할 것. AI 생성 코드도 **학습·이해** 후 사용

### 2.2 품질

- [ ] 단위 테스트로 함수 검증
- [ ] 기능 테스트로 SQL 처리기 동작 검증
- [ ] 엣지 케이스를 최대한 고려
- [ ] 이력서·포트폴리오에 넣을 만한 완성도

### 2.3 발표 (README 기준, 4분 + Q&A 1분)

- [ ] 별도 발표 슬라이드 없이 **GitHub README.md**를 기준으로 설명
- [ ] **테스트 작성·검증 과정**을 수행한 뒤, 그 내용을 데모·README에 포함
- [ ] 다른 팀과 차별될 수 있는 **추가 구현**을 검토(선택이 아니라 고민 권장)

## 3. 비범위 / 보류

- CREATE TABLE 및 런타임 스키마 변경(과제 전제에서 제외)
- (팀이 MVP에서 제외하기로 한 SQL 기능 — `docs/01-product-planning.md` 의 Optional 등과 정렬)

## 4. 연동 포인트 (본 레포 기준)

행사에서 강조한 **중점**: 입력(SQL) → **처리기(Processor)** → 파싱 → 실행 → 저장, **파일 저장 설계**, DB 처리 흐름 이해, **CLI**.

| 단계 | 이 레포에서의 대응(예) |
| --- | --- |
| CLI / 문장 분리 | `src/main.c`, `src/sql_processor.c` |
| Lexer | `src/lexer.c`, `include/lexer.h` |
| Parser / AST | `src/parser.c`, `include/ast.h` |
| Storage | `src/csv_storage.c`, `include/csv_storage.h` |
| Executor | `src/executor.c`, `include/executor.h` |
| 검증 | `tests/`, `ctest --test-dir build --output-on-failure` |

## 5. 수용 기준 (Definition of Done)

- [ ] `docs/02-architecture.md`, `docs/03-api-reference.md`와 실제 동작·CLI 계약이 맞물림
- [ ] 로컬/CI에서 빌드 및 테스트 통과(팀 기준)
- [ ] README에 **빌드·실행·테스트 재현** 절차가 명시됨

## 6. 차별화 아이디어 (선택)

- (채움) 예: 웹 데모, 실행 트레이스, 바이너리 로그, 추가 진단 플래그 등
