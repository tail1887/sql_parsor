# SQL 처리기 구현 명세서 (Codex 전달용)

## 1. 프로젝트 목적

C 언어로 동작하는 **파일 기반 SQL 처리기(SQL Processor)** 를 구현한다.  
이 프로그램은 SQL 파일을 입력으로 받아, 파일 안의 SQL 문장들을 순차적으로 실행한다.

이번 프로젝트의 목적은 범용 DBMS를 만드는 것이 아니라,  
**정해진 2개의 테이블에 대해 INSERT / SELECT를 처리할 수 있는 최소 기능의 SQL 처리기**를 구현하는 것이다.

핵심 흐름은 아래와 같다.

- SQL 파일 입력
- SQL 파싱
- 실행(Execution)
- 파일 기반 저장 / 조회
- 결과 출력
- 테스트

---

## 2. 구현 범위

### 반드시 구현할 기능
- CLI를 통해 SQL 파일 경로를 입력받아 실행
- SQL 파일 안의 **여러 문장**을 순차 실행
- `INSERT`
- `SELECT`
- CSV 파일 기반 저장 / 조회
- Binary 파일 기반 저장 / 조회
- 권한 검사 후 입장 기록 저장
- 단위 테스트
- 기능 테스트
- README 정리

### 구현하지 않을 기능
- `CREATE TABLE`
- `UPDATE`
- `DELETE`
- `JOIN`
- `ORDER BY`
- `GROUP BY`
- 서브쿼리
- 임의 컬럼 선택 (`SELECT id, name ...`)
- 복잡한 WHERE 조건 (`AND`, `OR`, `<`, `>`, `LIKE` 등)
- 범용 SQL 엔진
- 동적 스키마 로딩
- 여러 건물/여러 방에 대한 일반화된 권한 모델

---

## 3. 도메인 설명

이 시스템은 **크래프톤 정글 캠퍼스의 302호 입장 기록 관리**를 위한 최소 DB 처리기이다.

학생은 ID 카드를 가지고 있으며, 카드 ID를 기준으로 학생 정보와 입장 기록이 연결된다.

현재 시스템은 **302호 입장 기록만** 저장한다.  
따라서 입장 권한은 아래 규칙으로 고정한다.

- `class == 302` 이면 입장 권한 O
- `class == 100` 이면 입장 권한 O  
  - 100은 코치/직원 역할로 간주
- 그 외는 입장 권한 X

즉, 입장 권한은 입력값으로 받지 않고, **학생의 class 값으로부터 자동 계산**한다.

---

## 4. 고정 테이블 정의

이번 프로젝트는 테이블이 이미 존재한다고 가정하며, 아래 2개 테이블만 지원한다.

### 4.1 STUDENT_CSV
학생 정보 테이블이다.  
논리 테이블명은 `STUDENT_CSV` 이고, 실제 파일은 CSV 형식으로 저장한다.

속성:
- `id` : int, PK, 학생 카드 ID
- `name` : string, 이름
- `class` : int, 반 정보
  - 예: 301, 302, 303, 100
- `authorization` : boolean
  - `T` 또는 `F`
  - `class` 값으로부터 자동 계산되어 저장됨

실제 저장 파일:
- `data/student.csv`

### 4.2 ENTRY_LOG_BIN
입장 기록 테이블이다.  
논리 테이블명은 `ENTRY_LOG_BIN` 이고, 실제 파일은 binary 형식으로 저장한다.

속성:
- `entered_at` : datetime
- `id` : int, FK, 학생 카드 ID

실제 저장 파일:
- `data/entry_log.bin`

---

## 5. 테이블 간 관계

- `STUDENT_CSV.id` 와 `ENTRY_LOG_BIN.id` 는 같은 학생 카드 ID를 의미한다.
- `ENTRY_LOG_BIN.id` 는 `STUDENT_CSV.id` 를 참조한다고 가정한다.
- 입장 기록 INSERT 시, 해당 `id` 가 학생 테이블에 존재해야 한다.

---

## 6. 지원 SQL 문법

### 6.1 공통 규칙
- SQL 파일에는 **여러 문장**이 들어갈 수 있다.
- 각 문장은 반드시 `;` 로 종료한다.
- 문장들은 파일 안에서 순차 실행한다.
- 중간에 에러가 발생하면 즉시 실행을 중단하고 종료한다.
- MVP 기준으로 SQL 키워드와 테이블명은 **명세 예시와 동일한 대문자 입력만 지원해도 된다**.
- 문자열은 작은따옴표 `'` 로 감싼다.
- SQL 주석은 지원하지 않는다.
- 문자열 내부에 세미콜론(`;`)은 지원하지 않는다.
- 학생 이름에는 공백을 허용하지 않는다.

### 6.2 SELECT 지원 문법

#### 지원 1
```sql
SELECT * FROM STUDENT_CSV;
```

의미:
- 학생 테이블의 전체 row를 모두 조회한다.

#### 지원 2
```sql
SELECT * FROM STUDENT_CSV WHERE id = 123;
```

의미:
- 학생 테이블에서 `id == 123` 인 row만 조회한다.

#### 지원 3
```sql
SELECT * FROM ENTRY_LOG_BIN WHERE id = 123;
```

의미:
- 입장 기록 테이블에서 `id == 123` 인 row만 조회한다.

### 6.3 INSERT 지원 문법

#### STUDENT_CSV INSERT
```sql
INSERT INTO STUDENT_CSV VALUES (123, 'Hong', 302);
```

의미:
- 학생 정보를 1건 추가한다.
- `authorization` 값은 입력받지 않는다.
- `authorization` 은 아래 규칙으로 자동 계산하여 CSV에 함께 저장한다.
  - `class == 302` 또는 `class == 100` → `T`
  - 그 외 → `F`

주의:
- `id` 는 중복되면 안 된다.
- `name` 은 문자열이며 작은따옴표로 감싸야 한다.
- `name` 에는 공백을 허용하지 않는다.
- `name` 에는 쉼표(`,`), 줄바꿈, 작은따옴표 내부 포함을 허용하지 않는다.

#### ENTRY_LOG_BIN INSERT
```sql
INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 123);
```

의미:
- 302호 입장 기록 1건을 추가한다.

실행 조건:
1. `id` 가 `STUDENT_CSV` 에 존재해야 한다.
2. 해당 학생의 `authorization` 값이 `T` 이어야 한다.
3. 위 조건 중 하나라도 만족하지 못하면 INSERT 실패

주의:
- 같은 학생이 여러 번 입장하는 것은 허용한다.
- 즉, `ENTRY_LOG_BIN` 에서는 중복 `id` 기록이 가능하다.

---

## 7. datetime 입력 형식

`ENTRY_LOG_BIN` INSERT 에서 datetime은 아래 형식의 문자열만 허용한다.

```text
'YYYY-MM-DD HH:MM:SS'
```

예:
```sql
INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);
```

지원하지 않는 예:
- `'2026/04/08 09:00:00'`
- `'2026-04-08T09:00:00'`
- Unix timestamp 숫자 직접 입력

---

## 8. 저장 방식

### 8.1 STUDENT_CSV 저장 포맷

파일 경로:
- `data/student.csv`

포맷:
- CSV 텍스트 파일
- 헤더 포함
- UTF-8 저장
- delimiter는 `,`
- boolean 값은 `T` / `F`

헤더:
```text
id,name,class,authorization
```

예시:
```text
id,name,class,authorization
302,Kim,302,T
303,Lee,303,F
100,Coach,100,T
```

주의:
- `name` 에는 공백 미허용
- `name` 에 `,` 미허용
- `name` 에 줄바꿈 미허용

### 8.2 ENTRY_LOG_BIN 저장 포맷

파일 경로:
- `data/entry_log.bin`

포맷:
- append-only binary 파일
- 레코드 크기 고정
- 레코드 1개는 아래 두 필드로 구성
  - `entered_at` : 8바이트 signed integer
  - `id` : 4바이트 signed integer

즉, 레코드 1개 크기는 총 **12바이트**이다.

#### entered_at 저장 규칙
- SQL 입력에서는 `'YYYY-MM-DD HH:MM:SS'` 문자열로 받는다.
- 내부 저장 시에는 **Unix timestamp(초 단위)** 로 변환하여 8바이트 정수로 저장한다.

#### id 저장 규칙
- 4바이트 정수로 저장한다.

#### 구현 시 주의사항
- **구조체 전체를 그대로 `fwrite` 하지 말 것**
- 반드시 필드를 순서대로 직접 읽고 쓰는 방식으로 구현할 것
- binary 파일 포맷은 아래 순서를 따른다.
  1. `entered_at`
  2. `id`

같은 실행 환경에서 읽고 쓴다고 가정하며, 이식성보다 **명확한 직렬화 방식과 안정적인 동작**을 우선한다.

---

## 9. 권한(authorization) 규칙

### 9.1 STUDENT_CSV INSERT 시
`authorization` 은 사용자가 직접 입력하지 않는다.

아래 규칙으로 자동 계산한다.

- `class == 302` → `T`
- `class == 100` → `T`
- 나머지 → `F`

예:
- `(302, 'Kim', 302)` → `authorization = T`
- `(100, 'Coach', 100)` → `authorization = T`
- `(303, 'Lee', 303)` → `authorization = F`

### 9.2 ENTRY_LOG_BIN INSERT 시
입장 기록을 추가하기 전에 아래 순서로 검사한다.

1. `STUDENT_CSV` 에 해당 `id` 가 존재하는지 확인
2. 해당 row의 `authorization` 값이 `T` 인지 확인
3. 둘 다 만족할 때만 `entry_log.bin` 에 append

실패 예:
- 존재하지 않는 학생 ID로 입장 기록 INSERT
- `class = 303` 인 학생이 입장 기록 INSERT

---

## 10. SELECT 결과 출력 형식

출력은 사람이 읽기 쉬운 **CSV 형태의 텍스트**로 표준 출력(stdout)에 출력한다.

### 10.1 STUDENT_CSV 출력 형식
헤더:
```text
id,name,class,authorization
```

예:
```text
id,name,class,authorization
302,Kim,302,T
303,Lee,303,F
```

### 10.2 ENTRY_LOG_BIN 출력 형식
헤더:
```text
entered_at,id
```

예:
```text
entered_at,id
2026-04-08 09:00:00,302
2026-04-08 18:30:00,302
```

주의:
- binary 내부의 timestamp는 SELECT 시 다시 사람이 읽을 수 있는 datetime 문자열로 변환해서 출력할 것

---

## 11. 에러 처리 정책

### 11.1 공통
- 에러 발생 시 표준 에러(stderr)에 메시지를 출력한다.
- 에러가 발생한 문장에서 즉시 실행을 중단한다.
- 이후 문장은 실행하지 않는다.

### 11.2 에러 케이스

#### SQL 문법 오류
예:
- 세미콜론 누락
- VALUES 괄호 누락
- 지원하지 않는 문법 사용
- 잘못된 테이블명 사용

처리:
- 에러 메시지 출력 후 종료

#### 존재하지 않는 테이블
예:
```sql
SELECT * FROM UNKNOWN_TABLE;
```

처리:
- 에러 메시지 출력 후 종료

#### 존재하지 않는 학생 ID로 입장 기록 INSERT
예:
```sql
INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 999);
```

처리:
- 에러 메시지 출력 후 종료

#### 권한 없는 학생의 입장 기록 INSERT
예:
- `class = 303` 인 학생

처리:
- 에러 메시지 출력 후 종료
- binary 파일에는 아무것도 저장하지 않음

#### STUDENT_CSV 중복 ID INSERT
예:
```sql
INSERT INTO STUDENT_CSV VALUES (302, 'Kim2', 302);
```
단, 이미 `id = 302` row가 있는 경우

처리:
- 에러 메시지 출력 후 종료

#### 잘못된 datetime 형식
예:
```sql
INSERT INTO ENTRY_LOG_BIN VALUES ('2026/04/08 09:00:00', 302);
```

처리:
- 에러 메시지 출력 후 종료

#### SELECT 결과 없음
예:
```sql
SELECT * FROM STUDENT_CSV WHERE id = 999;
```

처리:
- 에러로 보지 않는다.
- 아래와 같이 출력한다.
```text
no rows found
```

---

## 12. CLI 실행 형식

실행 명령:
```bash
./sql_processor <sql_file_path>
```

예:
```bash
./sql_processor queries/test1.sql
```

동작:
- SQL 파일을 읽는다.
- 파일 안의 여러 문장을 순서대로 실행한다.
- SELECT 결과는 stdout에 출력한다.
- 에러는 stderr에 출력한다.

---

## 13. 파일 초기 상태

프로젝트는 아래 파일을 사용한다.

- `data/student.csv`
- `data/entry_log.bin`

권장 동작:
- `student.csv` 가 없으면 헤더만 포함한 빈 CSV 파일을 생성한다.
- `entry_log.bin` 이 없으면 빈 binary 파일을 생성한다.

단, CREATE TABLE 기능은 구현하지 않는다.  
즉, 테이블 정의 자체는 프로젝트에 고정되어 있다고 가정한다.

---

## 14. 내부 구현 제약

Codex가 과도하게 일반화하지 않도록 아래 제약을 지킨다.

- 범용 DBMS처럼 만들지 말 것
- 범용 SQL parser를 만들지 말 것
- 오직 이 프로젝트에서 필요한 최소 문법만 처리할 것
- tokenizer / parser / executor / storage 를 분리할 것
- CSV 저장 로직과 Binary 저장 로직을 분리할 것
- ENTRY_LOG_BIN binary 저장 시 `struct` 전체 fwrite 금지
- 테스트 가능한 작은 함수들로 나눌 것

---

## 15. 권장 모듈 분리

아래와 같이 분리하는 것을 권장한다.

- `main.c`
  - CLI 진입점
  - SQL 파일 읽기
  - 문장 단위 실행

- `tokenizer.c / tokenizer.h`
  - SQL 문자열을 토큰으로 분해

- `parser.c / parser.h`
  - 지원 문법만 파싱
  - SELECT / INSERT AST 생성

- `executor.c / executor.h`
  - AST 기반 실행

- `student_storage.c / student_storage.h`
  - `student.csv` 읽기/쓰기
  - 중복 ID 검사
  - authorization 계산

- `entry_log_storage.c / entry_log_storage.h`
  - `entry_log.bin` append/read
  - timestamp 직렬화/역직렬화

- `datetime_utils.c / datetime_utils.h`
  - datetime 문자열 파싱
  - timestamp 변환
  - 출력용 포맷 변환

- `tests/`
  - 단위 테스트
  - 기능 테스트

---

## 16. 테스트 요구사항

이번 과제는 구현뿐 아니라 검증도 중요하므로, 아래 테스트를 반드시 포함한다.

### 16.1 단위 테스트
- tokenizer가 키워드를 올바르게 분리하는지
- parser가 지원 문법을 정확히 해석하는지
- authorization 계산 함수가 맞는지
- datetime 파싱이 맞는지
- CSV append/read가 맞는지
- binary append/read가 맞는지

### 16.2 기능 테스트
- 학생 INSERT 후 SELECT ALL
- 학생 INSERT 후 SELECT WHERE id
- 권한 있는 학생의 ENTRY_LOG_BIN INSERT 성공
- 권한 없는 학생의 ENTRY_LOG_BIN INSERT 실패
- 존재하지 않는 학생의 ENTRY_LOG_BIN INSERT 실패
- ENTRY_LOG_BIN SELECT WHERE id
- 한 SQL 파일 안의 여러 문장 순차 실행

### 16.3 엣지 케이스 테스트
- 빈 CSV / 빈 binary 파일
- 중복 학생 ID
- 잘못된 datetime
- 세미콜론 누락
- 지원하지 않는 테이블명
- 값 개수 불일치
- `SELECT` 결과 없음

---

## 17. README에 반드시 포함할 내용

발표는 README 기준으로 진행하므로 아래를 포함한다.

- 프로젝트 개요
- 지원 문법
- 테이블 구조
- CSV / Binary 저장 방식
- authorization 규칙
- 빌드 방법
- 실행 방법
- 테스트 방법
- 예시 SQL 파일
- 예시 출력
- 제한사항
- 향후 개선점

---

## 18. 예시 SQL

### 예시 1: 학생 등록
```sql
INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);
INSERT INTO STUDENT_CSV VALUES (303, 'Lee', 303);
INSERT INTO STUDENT_CSV VALUES (100, 'Coach', 100);
```

### 예시 2: 학생 전체 조회
```sql
SELECT * FROM STUDENT_CSV;
```

### 예시 3: 특정 학생 조회
```sql
SELECT * FROM STUDENT_CSV WHERE id = 302;
```

### 예시 4: 입장 기록 저장
```sql
INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);
```

### 예시 5: 특정 학생 입장 기록 조회
```sql
SELECT * FROM ENTRY_LOG_BIN WHERE id = 302;
```

### 예시 6: 한 파일에 여러 문장
```sql
INSERT INTO STUDENT_CSV VALUES (302, 'Kim', 302);
INSERT INTO STUDENT_CSV VALUES (303, 'Lee', 303);
SELECT * FROM STUDENT_CSV;
INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);
SELECT * FROM ENTRY_LOG_BIN WHERE id = 302;
```

---

## 19. 구현 우선순위

Codex는 아래 순서로 구현하는 것이 좋다.

1. CLI + SQL 파일 읽기
2. 여러 문장 분리(`;`)
3. tokenizer
4. parser
5. STUDENT_CSV 저장/조회
6. authorization 자동 계산
7. ENTRY_LOG_BIN 저장/조회
8. datetime 변환
9. ENTRY_LOG_BIN INSERT 시 권한 검사
10. SELECT 출력 형식 정리
11. 테스트 추가
12. README 정리

---

## 20. 최종 목표

최종 결과물은 다음을 만족해야 한다.

- SQL 파일을 입력받아 여러 문장을 순차 실행할 수 있다.
- `STUDENT_CSV` 는 CSV로 저장된다.
- `ENTRY_LOG_BIN` 은 binary로 저장된다.
- 입장 권한 규칙이 적용된다.
- `SELECT` 결과가 올바르게 출력된다.
- 테스트가 존재하고 통과한다.
- README만 보고 데모와 설명이 가능하다.
