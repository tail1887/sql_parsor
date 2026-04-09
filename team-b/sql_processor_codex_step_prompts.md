# Codex 단계별 구현 프롬프트 가이드

이 문서는 **한 단계 구현 → 코드 읽고 이해 → 다음 단계** 방식으로 진행하기 위한 프롬프트 모음이다.

진행 원칙:
1. 한 번에 한 단계만 구현시킨다.
2. 각 단계가 끝나면 반드시 코드를 직접 읽고, Codex에게 설명을 시킨다.
3. 테스트가 통과한 뒤에만 다음 단계로 넘어간다.
4. 과도한 일반화가 보이면 바로 되돌린다.
5. 매 단계마다 `git commit` 을 남긴다.

권장 커밋 형식:
- `step1: bootstrap cli and statement splitter`
- `step2: add tokenizer and token tests`
- `step3: add parser and ast`
- `step4: implement student csv storage`
- `step5: implement entry log binary storage`
- `step6: wire executor and end-to-end tests`

---

## 시작 전에 할 일

### 0-1. 리포지토리 루트에 `AGENTS.md` 추가
아래 템플릿을 그대로 두는 것을 권장한다.

```md
# Repository expectations

- Language: C99
- Build with: make
- Test with: make test
- Goal: build a tiny file-based SQL processor in C
- Keep the implementation minimal and easy to explain
- Only support:
  - INSERT INTO STUDENT_CSV VALUES (id, 'name', class);
  - INSERT INTO ENTRY_LOG_BIN VALUES ('YYYY-MM-DD HH:MM:SS', id);
  - SELECT * FROM STUDENT_CSV;
  - SELECT * FROM STUDENT_CSV WHERE id = <int>;
  - SELECT * FROM ENTRY_LOG_BIN WHERE id = <int>;
- Do not implement CREATE TABLE, UPDATE, DELETE, JOIN, ORDER BY, GROUP BY, subqueries
- Do not build a general SQL engine
- Separate tokenizer, parser, executor, and storage
- Never write the whole struct to binary with one fwrite
- Add tests for every new behavior
- Stop at the smallest implementation that satisfies the spec
```

### 0-2. Codex에게 먼저 큰 방향을 못 박기
아래 프롬프트를 먼저 한 번 넣는다.

```text
아래 명세를 기준으로 아주 작은 파일 기반 SQL 처리기를 C99로 구현할 것이다.

중요 제약:
- 범용 DBMS처럼 만들지 말 것
- 범용 SQL parser를 만들지 말 것
- 오직 명세에 있는 문법만 처리할 것
- tokenizer / parser / executor / storage 를 분리할 것
- 한 단계씩만 구현하고, 불필요한 파일이나 복잡한 추상화는 만들지 말 것
- 테스트 가능한 작은 함수들로 나눌 것
- 구현 후에는 어떤 파일을 왜 만들었는지 설명도 해줄 것

이제부터는 내가 단계별 프롬프트를 줄 테니, 그 단계 범위만 구현해라.
```

---

## Step 1. 프로젝트 뼈대 + CLI + SQL 파일 읽기 + 문장 분리

### 1-A. 구현 프롬프트
```text
Step 1만 구현해라. 다른 단계는 건드리지 마라.

목표:
- C99 프로젝트 뼈대 생성
- Makefile 추가
- main.c 에서 ./sql_processor <sql_file_path> 형태로 실행
- SQL 파일 전체를 읽는 함수 구현
- 세미콜론(;) 기준으로 여러 문장을 분리하는 최소 함수 구현
- 아직 tokenizer / parser / executor / storage는 구현하지 말 것
- 일단 분리된 raw statement를 stdout에 출력하는 수준까지만 구현
- 테스트 추가:
  - 파일 읽기 테스트
  - 문장 분리 테스트
  - 세 문장이 순서대로 분리되는지 확인

제약:
- 문자열 내부 세미콜론은 지원하지 않아도 된다
- 학생 이름에 공백은 미허용이므로 splitter는 단순하게 가도 된다
- 프로젝트 구조는 앞으로 확장 가능하게 하되 지금 필요한 최소한만 만들 것

완료 조건:
- make 성공
- make test 성공
- sample.sql 을 넣으면 문장들이 분리되어 순서대로 출력됨
- 변경한 파일과 각 파일의 역할을 설명할 수 있음
```

### 1-B. 이해용 설명 프롬프트
```text
방금 만든 코드를 초보자에게 설명해라.

반드시 포함할 것:
1. 파일을 읽는 함수가 어떻게 동작하는지
2. 세미콜론 기준 문장 분리 로직이 어떻게 동작하는지
3. 현재 구현에서 아직 안 되는 것
4. 왜 tokenizer/parser를 아직 붙이지 않았는지
5. 이번 단계에서 가장 중요한 함수 2개
```

### 1-C. 팀 체크리스트
- `main.c` 흐름을 말로 설명할 수 있는가?
- 파일 전체 읽기에서 메모리 할당/해제를 이해했는가?
- 문장 분리 함수의 입력/출력을 말할 수 있는가?
- 왜 지금은 raw statement 출력만 하는지 이해했는가?

### 1-D. 넘어가기 조건
- 두 팀원 모두 `main -> read_file -> split_statements` 호출 흐름을 설명 가능
- 테스트가 모두 통과
- 다음 단계에서 토큰화를 어디에 붙일지 합의 완료

---

## Step 2. Tokenizer 구현

### 2-A. 구현 프롬프트
```text
Step 2만 구현해라. 기존 Step 1 동작은 유지하라.

목표:
- 최소 tokenizer 구현
- 지원 토큰:
  - SELECT
  - INSERT
  - INTO
  - VALUES
  - FROM
  - WHERE
  - STAR(*)
  - LPAREN, RPAREN
  - COMMA
  - SEMICOLON
  - EQUAL
  - IDENTIFIER
  - INTEGER
  - STRING
- 공백 스킵 처리
- 작은따옴표 문자열 처리
- 토큰 배열을 만드는 API 설계
- tokenizer 단위 테스트 작성

제약:
- 지원 문법 밖의 일반 SQL 토큰은 추가하지 말 것
- 문자열 내부 escape는 지원하지 않아도 된다
- 이름 문자열에 공백은 미허용이므로 단순 문자열 처리로 충분하다
- 에러 메시지는 간단명료하게 유지할 것

완료 조건:
- raw statement를 tokenizer에 넣었을 때 올바른 토큰 배열이 생성됨
- INSERT 예제와 SELECT 예제를 토큰화하는 테스트가 모두 통과함
- 토큰 타입 enum과 token struct가 설명 가능한 수준으로 단순함
```

### 2-B. 이해용 설명 프롬프트
```text
방금 만든 tokenizer 코드를 초보자에게 설명해라.

반드시 포함할 것:
1. tokenizer가 왜 필요한지
2. IDENTIFIER, INTEGER, STRING을 어떻게 구분하는지
3. 작은따옴표 문자열을 어떻게 읽는지
4. 토큰 배열은 어떤 구조로 저장되는지
5. tokenizer에서 가장 버그가 나기 쉬운 부분 3개
```

### 2-C. 팀 체크리스트
- `SELECT * FROM STUDENT_CSV WHERE id = 302;` 의 토큰 시퀀스를 손으로 적을 수 있는가?
- `INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);` 의 STRING/INTEGER 구분을 설명할 수 있는가?
- 토큰 구조체가 어떤 필드를 가지는지 이해했는가?

### 2-D. 넘어가기 조건
- 두 팀원 모두 토큰화 결과를 예측 가능
- tokenizer 테스트 통과
- tokenizer 에러 케이스 2개 이상 설명 가능

---

## Step 3. Parser + AST 구현

### 3-A. 구현 프롬프트
```text
Step 3만 구현해라. tokenizer까지의 기존 동작은 유지하라.

목표:
- 최소 AST 정의
- Statement 타입:
  - INSERT_STUDENT
  - INSERT_ENTRY_LOG
  - SELECT_STUDENT_ALL
  - SELECT_STUDENT_BY_ID
  - SELECT_ENTRY_LOG_BY_ID
- parser 구현
- tokenizer 결과를 받아 AST 하나를 생성
- 여러 문장 파일의 경우 문장별로 parser를 호출할 수 있게 설계
- parser 단위 테스트 추가

제약:
- 범용 parser 금지
- 명세에 있는 5개 statement 패턴만 허용
- SELECT 컬럼 리스트 지원 금지
- WHERE는 id = <int> 형태만 허용
- AST는 설명하기 쉬운 구조체로 유지
- unnecessary abstraction 금지

완료 조건:
- 각 지원 SQL 문장 예제에 대해 올바른 AST 생성
- 잘못된 문법에 대해 parser 에러 반환
- AST 필드가 최소한으로 유지됨
```

### 3-B. 이해용 설명 프롬프트
```text
방금 만든 parser와 AST를 초보자에게 설명해라.

반드시 포함할 것:
1. AST가 왜 필요한지
2. SELECT_STUDENT_ALL 과 SELECT_STUDENT_BY_ID 를 왜 분리했는지
3. INSERT_STUDENT 와 INSERT_ENTRY_LOG 의 필드가 각각 무엇인지
4. parser가 토큰을 어떤 순서로 소비하는지
5. parser에서 가장 중요한 함수 3개
```

### 3-C. 팀 체크리스트
- AST 구조체를 그림처럼 설명할 수 있는가?
- SELECT ALL과 SELECT WHERE를 왜 다른 statement type으로 뒀는지 이해했는가?
- parser의 “현재 토큰 인덱스” 개념을 이해했는가?

### 3-D. 넘어가기 조건
- 두 팀원 모두 SQL 예제를 AST로 구두 변환 가능
- parser 테스트 통과
- 잘못된 문법 2개 이상에 대해 어디서 실패하는지 설명 가능

---

## Step 4. STUDENT_CSV 저장/조회 구현

### 4-A. 구현 프롬프트
```text
Step 4만 구현해라. parser까지의 기존 동작은 유지하라.

목표:
- data/student.csv 관리 구현
- 파일이 없으면 헤더 포함 빈 CSV 생성
- 학생 INSERT 구현:
  - INSERT INTO STUDENT_CSV VALUES (id, 'name', class)
  - authorization은 class로부터 자동 계산
- 중복 id 검사 구현
- SELECT * FROM STUDENT_CSV 구현
- SELECT * FROM STUDENT_CSV WHERE id = <int> 구현
- 출력 형식을 명세대로 맞출 것
- student storage 단위 테스트 + 기능 테스트 추가

제약:
- CSV 파서는 명세 범위에 맞는 최소 구현만 할 것
- name에는 공백/쉼표/줄바꿈 미허용
- authorization은 T/F로 저장
- student storage는 tokenizer/parser를 몰라야 한다
- storage layer와 executor responsibility를 섞지 말 것

완료 조건:
- student.csv 생성/읽기/추가/조회 모두 동작
- authorization 계산이 정확함
- 중복 id INSERT 시 에러 처리
- SELECT 결과가 명세 형식대로 출력
```

### 4-B. 이해용 설명 프롬프트
```text
방금 만든 STUDENT_CSV 관련 코드를 초보자에게 설명해라.

반드시 포함할 것:
1. student.csv의 헤더와 row 포맷
2. authorization을 어떤 함수에서 계산하는지
3. 중복 id를 어떻게 검사하는지
4. SELECT ALL과 SELECT BY ID가 파일을 어떻게 읽는지
5. 왜 storage 코드는 SQL 문법을 몰라야 하는지
```

### 4-C. 팀 체크리스트
- `authorization = f(class)` 규칙을 설명할 수 있는가?
- 왜 student.csv에 헤더가 필요한지 말할 수 있는가?
- 중복 검사 로직이 파일 전체 스캔인지 설명할 수 있는가?

### 4-D. 넘어가기 조건
- STUDENT_CSV 관련 테스트 통과
- 학생 INSERT/SELECT 데모 가능
- 두 팀원 모두 CSV 저장 형식을 손으로 적을 수 있음

---

## Step 5. ENTRY_LOG_BIN 저장/조회 + datetime 구현

### 5-A. 구현 프롬프트
```text
Step 5만 구현해라. 기존 student csv 동작은 유지하라.

목표:
- datetime 문자열 파싱 함수 구현
- Unix timestamp 변환 함수 구현
- 출력용 datetime 포맷 함수 구현
- data/entry_log.bin 관리 구현
- 파일이 없으면 빈 binary 파일 생성
- 레코드 1개 = entered_at(8-byte signed integer) + id(4-byte signed integer)
- binary append 구현
- ENTRY_LOG_BIN SELECT * WHERE id = <int> 구현
- binary storage 테스트 + datetime 테스트 추가

제약:
- struct 전체를 한 번에 fwrite 금지
- 필드를 순서대로 직접 쓰고 직접 읽을 것
- 입력 datetime 형식은 'YYYY-MM-DD HH:MM:SS'만 허용
- SELECT 출력은 사람이 읽을 수 있는 문자열로 변환할 것
- 아직 권한 검사와 student existence check는 executor 최종 결합 단계에서 붙여도 된다

완료 조건:
- entry_log.bin 생성/append/read 동작
- binary 한 레코드가 12바이트 규칙을 따른다
- datetime parse/format 테스트 통과
- ENTRY_LOG_BIN SELECT WHERE id 동작
```

### 5-B. 이해용 설명 프롬프트
```text
방금 만든 ENTRY_LOG_BIN 관련 코드를 초보자에게 설명해라.

반드시 포함할 것:
1. 왜 entered_at을 문자열이 아니라 timestamp로 저장하는지
2. binary 레코드 12바이트가 어떻게 구성되는지
3. struct 전체 fwrite를 왜 피하는지
4. SELECT 시 timestamp를 다시 문자열로 바꾸는 흐름
5. binary 읽기/쓰기에서 가장 실수하기 쉬운 부분 3개
```

### 5-C. 팀 체크리스트
- `entered_at` 이 입력에서는 문자열, 저장에서는 정수인 이유를 설명할 수 있는가?
- binary 레코드 레이아웃을 바이트 단위로 말할 수 있는가?
- datetime 파싱 함수의 실패 조건을 설명할 수 있는가?

### 5-D. 넘어가기 조건
- binary 관련 테스트 통과
- 두 팀원 모두 entry_log.bin 포맷을 설명 가능
- SELECT WHERE id 결과 출력이 명세와 일치

---

## Step 6. Executor 연결 + 권한 검사 + 여러 문장 순차 실행

### 6-A. 구현 프롬프트
```text
Step 6만 구현해라. 지금까지 만든 parser/storage/datetime을 연결하라.

목표:
- executor 구현
- AST를 받아 실제 동작 실행
- SQL 파일의 여러 문장을 순차 실행
- 에러 발생 시 즉시 중단
- ENTRY_LOG_BIN INSERT 실행 시:
  1. STUDENT_CSV 에 id 존재 확인
  2. authorization == T 확인
  3. 둘 다 만족해야 binary append
- stdout/stderr 출력 정리
- end-to-end 기능 테스트 추가

필수 기능 테스트:
- 학생 3명 등록 후 SELECT * FROM STUDENT_CSV
- 권한 있는 학생의 ENTRY_LOG_BIN INSERT 성공
- 권한 없는 학생의 ENTRY_LOG_BIN INSERT 실패
- 존재하지 않는 학생의 ENTRY_LOG_BIN INSERT 실패
- ENTRY_LOG_BIN SELECT WHERE id
- 한 파일 안의 여러 문장 순차 실행
- 중간 문장에서 에러 발생 시 이후 문장 미실행

제약:
- executor가 tokenizer 구현 세부사항까지 알 필요는 없다
- storage와 parser를 과도하게 결합하지 말 것
- 에러 메시지는 짧고 명확하게 유지할 것
- 명세 밖의 문법 확장 금지

완료 조건:
- 전체 파이프라인 동작:
  SQL file -> split -> tokenize -> parse -> execute -> storage/output
- end-to-end 테스트 통과
- 에러 발생 시 중단 정책 동작
```

### 6-B. 이해용 설명 프롬프트
```text
전체 파이프라인을 초보자에게 설명해라.

반드시 포함할 것:
1. main에서 시작해서 최종 저장/출력까지 함수 호출 순서
2. STUDENT_CSV INSERT가 끝까지 어떻게 처리되는지
3. ENTRY_LOG_BIN INSERT가 권한 검사와 함께 어떻게 처리되는지
4. SELECT * FROM STUDENT_CSV WHERE id = 302; 가 어떤 흐름으로 실행되는지
5. 지금 구조에서 parser / executor / storage 책임이 각각 무엇인지
```

### 6-C. 팀 체크리스트
- “입력 SQL 1문장이 어떤 파일/함수를 거쳐 실행되는지” 끝까지 설명 가능한가?
- 권한 없는 학생이 ENTRY_LOG_BIN에 왜 저장되지 않는지 설명 가능한가?
- 왜 에러가 나면 즉시 중단하는지 말할 수 있는가?

### 6-D. 넘어가기 조건
- 전체 테스트 통과
- 팀원 모두 INSERT 한 건과 SELECT 한 건을 끝까지 추적 설명 가능
- 데모 가능 상태

---

## Step 7. README + 마무리 리뷰 + 발표 준비

### 7-A. 구현/문서 프롬프트
```text
Step 7만 수행해라. 구현 범위를 넓히지 말고 정리 작업만 하라.

목표:
- README.md 작성 또는 보강
- 아래 내용을 포함:
  - 프로젝트 개요
  - 지원 문법
  - 테이블 구조
  - 저장 포맷(CSV/Binary)
  - authorization 규칙
  - 빌드/실행 방법
  - 테스트 방법
  - 예시 SQL와 예시 출력
  - 제한사항
  - 향후 개선점
- 코드 정리:
  - 불필요한 debug print 제거
  - 에러 메시지 일관성 확인
  - 함수/파일 역할 주석 보강
- 테스트 실행 결과 정리
- 발표용 데모 시나리오 1개 제안

완료 조건:
- README만 보고 실행 가능
- 발표 4분 안에 설명 가능한 구조
- 구현 범위와 제한사항이 분명하게 적혀 있음
```

### 7-B. 이해용 설명 프롬프트
```text
이 프로젝트를 발표한다고 가정하고 4분 발표용 설명 순서를 짜라.

반드시 포함할 것:
1. 문제 정의
2. 지원 문법
3. 전체 구조
4. CSV/Binary 저장 방식
5. 권한 검사
6. 테스트 포인트
7. 한계와 개선 방향
```

### 7-C. 팀 체크리스트
- README만 보고 실행 가능 여부 확인
- 발표 4분 시나리오 리허설
- 질문 대비:
  - 왜 STUDENT는 CSV, ENTRY_LOG는 Binary인가?
  - authorization을 왜 입력받지 않고 class로 계산하는가?
  - 왜 CREATE TABLE은 구현하지 않았는가?
  - 왜 SELECT는 *만 지원하는가?

---

## 단계마다 공통으로 넣으면 좋은 보조 프롬프트

### A. 변경 사항 설명 요청
```text
방금 변경한 파일을 파일별로 요약해라.
각 파일의 역할, 핵심 함수, 입력/출력, 테스트 포인트를 정리해라.
```

### B. 코드 읽기 요청
```text
내가 이 코드를 직접 설명해야 한다.
중요 함수 3개를 골라서, 각 함수의 입력/출력/부작용을 초보자 관점에서 설명해라.
```

### C. 리스크 점검 요청
```text
현재 단계 구현에서 가장 위험한 버그 가능성 5개를 뽑고, 각각 어떻게 테스트하면 잡을 수 있는지 제안해라.
```

### D. 과한 구현 되돌리기 요청
```text
지금 구현이 과한 것 같다.
명세 범위를 벗어난 일반화, 불필요한 추상화, 불필요한 파일을 찾아서 더 단순하게 리팩터링해라.
동작은 유지하고 설명 가능성을 높여라.
```

### E. 리뷰 요청
```text
지금 단계의 코드만 리뷰해라.
버그 가능성, 테스트 누락, 명세 불일치, 설명하기 어려운 부분을 지적해라.
```

---

## 팀 학습 운영 팁

### 1. 매 단계 끝날 때 둘 다 답해야 하는 질문
- 이 단계의 입력은 무엇인가?
- 출력은 무엇인가?
- 가장 중요한 자료구조는 무엇인가?
- 에러는 어디서 처리하는가?
- 다음 단계는 왜 필요한가?

### 2. 한 단계 끝날 때 남길 메모
- 새로 생긴 파일
- 새로 생긴 핵심 함수
- 아직 헷갈리는 점 1개
- 다음 단계 전에 확인할 테스트 1개

### 3. 절대 하지 말 것
- 한 번에 끝까지 구현시키기
- 이해 안 된 상태로 다음 단계로 넘어가기
- 테스트 없이 “돌아가는 것 같음”으로 판단하기
- Codex가 만든 추상화를 이해하지 못한 채 유지하기
- 명세에 없는 기능 욕심내기

---

## 추천 실제 진행 순서

1. Step 1 구현
2. 코드 설명 받기
3. 팀원끼리 함수 흐름 설명
4. 테스트 확인
5. git commit
6. Step 2로 이동

이 패턴을 Step 6까지 반복하고, 마지막에 Step 7로 정리한다.
