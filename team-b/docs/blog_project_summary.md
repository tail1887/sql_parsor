# Tiny File-Based SQL Processor 구현 정리

## 1. 프로젝트 한 줄 소개

이 프로젝트는 **C99로 만든 아주 작은 파일 기반 SQL 프로세서**다.  
목표는 범용 데이터베이스 엔진을 만드는 것이 아니라, **과제 명세에 나온 5가지 SQL만 정확하게 처리하는 최소 구현**을 만드는 것이다.

지원하는 SQL은 아래 다섯 가지뿐이다.

```sql
INSERT INTO STUDENT_CSV VALUES (id, 'name', class);
INSERT INTO ENTRY_LOG_BIN VALUES ('YYYY-MM-DD HH:MM:SS', id);
SELECT * FROM STUDENT_CSV;
SELECT * FROM STUDENT_CSV WHERE id = <int>;
SELECT * FROM ENTRY_LOG_BIN WHERE id = <int>;
```

지원하지 않는 기능은 의도적으로 구현하지 않았다.

- `CREATE TABLE`
- `UPDATE`
- `DELETE`
- `JOIN`
- `ORDER BY`
- `GROUP BY`
- 서브쿼리
- 일반화된 SQL 엔진

이 프로젝트의 핵심은 “많이 만드는 것”이 아니라 **명세를 정확하게 만족하는 가장 작은 구조**를 만드는 데 있다.

## 2. 이 프로젝트가 실제로 만들고자 한 것

이 프로젝트는 SQL 문법 자체를 구현하는 과제이기도 하지만, 더 정확히 말하면 **두 개의 논리적 테이블을 가진 아주 작은 출입 관리 시스템**을 만드는 과제였다.

중요한 점은 이 프로젝트가 `CREATE TABLE`을 지원하지 않는다는 것이다.  
즉 사용자가 직접 테이블을 정의하는 DBMS는 아니고, **과제에서 미리 정해 둔 두 개의 테이블을 코드로 구현하는 방식**이다.

논리적으로는 아래 두 테이블을 만든다고 볼 수 있다.

### 1. STUDENT_CSV

학생 마스터 데이터 테이블이다.

- `id`: 학생 카드 ID
- `name`: 이름
- `class`: 반 정보
- `authorization`: 입장 권한 여부

즉 “이 학생이 누구인지”와 “입장이 가능한 학생인지”를 저장하는 기준 테이블이다.

### 2. ENTRY_LOG_BIN

출입 기록 테이블이다.

- `entered_at`: 입장 날짜/시간
- `id`: 학생 카드 ID

즉 “누가 언제 들어왔는지”를 기록하는 로그 테이블이다.

두 테이블은 `id`를 통해 연결된다.  
`ENTRY_LOG_BIN`에 입장 기록을 넣으려면, 먼저 `STUDENT_CSV`에 해당 학생이 존재해야 하고, 그 학생의 `authorization`이 `T`여야 한다.

결국 이 과제의 데이터 모델은 아래처럼 이해하면 된다.

- `STUDENT_CSV`: 학생 기준 정보
- `ENTRY_LOG_BIN`: 학생 출입 이벤트 로그
- 연결 기준: `id`

그래서 지원하는 SQL도 이 구조를 최소한으로 다룰 수 있는 형태만 남겨 두었다.

- 학생 등록
- 학생 전체 조회
- 학생 단건 조회
- 출입 기록 등록
- 특정 학생의 출입 기록 조회

즉 “작은 SQL 프로세서”이면서 동시에, **학생 정보와 출입 로그를 다루는 작은 데이터 시스템**이라고 보는 것이 맞다.

## 3. 왜 하나는 CSV이고 하나는 binary인가

처음 보면 `STUDENT_CSV`와 `ENTRY_LOG_BIN`을 둘 다 CSV로 하거나, 둘 다 binary로 맞출 수도 있어 보인다.  
하지만 과제에서는 일부러 **서로 다른 성격의 데이터를 서로 다른 저장 방식으로 다루도록** 설계되어 있다.

이 선택은 단순한 취향이 아니라, 데이터의 성격과 학습 목표를 같이 반영한 것이다.

### STUDENT_CSV를 CSV로 둔 이유

학생 테이블은 “기준 정보”에 가깝다.

- row 수가 많지 않다
- 사람이 직접 열어보고 확인하기 쉽다
- 헤더가 있으면 각 컬럼 의미를 바로 이해할 수 있다
- `authorization` 계산 결과를 눈으로 확인하기 좋다
- 디버깅과 시연에 유리하다

즉 학생 정보는 자주 append 되는 이벤트 로그라기보다, **사람이 읽고 확인해야 하는 마스터 데이터**에 가깝다.  
그래서 텍스트 기반 CSV가 잘 어울린다.

### ENTRY_LOG_BIN을 binary로 둔 이유

출입 기록은 “이벤트 로그”에 가깝다.

- 같은 구조의 레코드가 계속 뒤에 붙는다
- `entered_at`과 `id`만 있으면 된다
- 고정 길이 레코드로 저장하기 좋다
- timestamp를 숫자로 저장하면 포맷이 단순해진다
- binary 읽기/쓰기와 직렬화 개념을 연습할 수 있다

즉 출입 로그는 사람이 직접 편집하는 데이터라기보다, **프로그램이 계속 append 하는 기록 데이터**에 가깝다.  
그래서 fixed-size binary 레코드가 잘 맞는다.

### 이 분리가 주는 학습 효과

이 구조 덕분에 한 프로젝트 안에서 아래를 동시에 다룰 수 있다.

- 텍스트 기반 CSV 읽기/쓰기
- binary 레코드 직렬화/역직렬화
- 서로 다른 storage format을 같은 executor 파이프라인에 연결하기
- “기준 테이블”과 “로그 테이블”의 역할 차이 이해하기

즉 `STUDENT_CSV = CSV`, `ENTRY_LOG_BIN = binary`는 그냥 파일 형식을 다르게 둔 것이 아니라,  
**데이터 성격이 다른 두 테이블을 서로 다른 저장 방식으로 구현해 보는 과제의 핵심 장치**라고 볼 수 있다.

## 4. 과제 요구사항 요약

과제에서 중요하게 본 포인트는 아래와 같다.

- 언어는 `C99`
- 빌드는 `make`
- 테스트는 `make test`
- tokenizer, parser, executor, storage를 분리할 것
- `STUDENT_CSV`는 CSV 파일로 저장할 것
- `ENTRY_LOG_BIN`은 binary 파일로 저장할 것
- binary 파일에 구조체 전체를 한 번에 `fwrite` 하지 말 것
- 새 동작을 추가하면 테스트도 같이 추가할 것
- 설명하기 쉬운 최소 구현을 유지할 것
- stdout/stderr 결과는 테스트하기 쉽게 결정적으로 만들 것

즉 이 프로젝트는 “기능이 많아 보이는 코드”보다 **역할이 분리되고, 테스트 가능하고, 초보자도 설명할 수 있는 코드**가 더 중요하다.

## 5. 최종 구현 범위

현재 구현은 Step 6까지 연결되어 있다.

- Step 1: SQL 파일 읽기 + `;` 기준 문장 분리
- Step 2: 최소 tokenizer 구현
- Step 3: 5가지 SQL 문법만 AST로 바꾸는 parser 구현
- Step 4: `STUDENT_CSV` storage + executor 연결
- Step 5: datetime 파싱/포맷 + `ENTRY_LOG_BIN` binary storage 구현
- Step 6: 학생 존재 여부 검사 + authorization 검사 + 전체 파이프라인 연결

즉 지금은 **파일 읽기 -> 문장 분리 -> 토큰화 -> 파싱 -> 실행 -> 저장/출력**까지 끝까지 동작한다.

## 6. 전체 파이프라인

프로그램의 큰 흐름은 아래와 같다.

```text
main
-> read_text_file
-> split_sql_statements
-> tokenize_sql
-> parse_statement
-> execute_statement
-> student_storage / entry_log_storage
-> stdout / stderr
```

핵심은 parser 이전과 이후의 책임이 완전히 다르다는 점이다.

- tokenizer/parser: SQL 문장을 “이해”하는 단계
- executor: 해석된 문장을 실제 동작으로 연결하는 단계
- storage: 파일 포맷에 맞게 읽고 쓰는 단계

이렇게 분리해 두면 코드가 훨씬 설명하기 쉬워지고, 테스트도 단계별로 나눠서 작성할 수 있다.

## 7. 왜 범용 SQL 엔진으로 가지 않았는가

처음부터 중요한 원칙은 **과제 범위를 넘어서 일반화하지 않는 것**이었다.

예를 들어 parser도 “어떤 SELECT든 받을 수 있는 구조”로 만들지 않고, 아래처럼 과제에서 필요한 패턴만 허용한다.

- `SELECT * FROM STUDENT_CSV;`
- `SELECT * FROM STUDENT_CSV WHERE id = <int>;`
- `SELECT * FROM ENTRY_LOG_BIN WHERE id = <int>;`

이 선택의 장점은 명확하다.

- 코드가 짧다
- 디버깅이 쉽다
- 설명이 쉽다
- 테스트 범위가 분명하다

대신 유연성은 줄어들지만, 이 프로젝트에서는 그게 오히려 장점이다.

## 8. STUDENT_CSV 설계 포인트

`STUDENT_CSV`는 `data/student.csv` 파일에 저장된다.

헤더는 항상 아래와 같다.

```text
id,name,class,authorization
```

row 포맷은 아래와 같다.

```text
302,Kim,302,T
303,Lee,303,F
```

중요한 점은 `authorization`을 사용자가 직접 넣지 않는다는 점이다.  
`INSERT INTO STUDENT_CSV VALUES (id, 'name', class);` 에서는 `id`, `name`, `class`만 들어오고, `authorization`은 executor가 `class`를 보고 자동 계산한다.

현재 규칙은 아래와 같다.

- `class == 302` 이면 `T`
- `class == 100` 이면 `T`
- 나머지는 `F`

이 계산을 storage가 아니라 executor에서 하는 이유는, 이 로직이 **파일 저장 규칙**이 아니라 **실행 규칙**이기 때문이다.

또 하나의 핵심은 중복 `id` 검사다.  
학생 INSERT를 할 때는 append 전에 파일 전체를 읽어서 같은 `id`가 이미 있는지 검사한다.

이 방식은 큰 데이터에는 비효율적이지만, 이번 과제에서는 장점이 더 크다.

- 구현이 단순하다
- 설명이 쉽다
- 동작이 명확하다
- 테스트하기 쉽다

즉 “빠른 방법”보다 **가장 작은 정답 구현**을 선택한 셈이다.

## 9. ENTRY_LOG_BIN 설계 포인트

`ENTRY_LOG_BIN`은 `data/entry_log.bin` 파일에 저장된다.  
이 파일은 CSV가 아니라 **고정 길이 binary 레코드**를 사용한다.

레코드 1개는 총 12바이트다.

```text
entered_at: 8 bytes (int64_t timestamp)
id:         4 bytes (int32_t)
```

즉 파일 안에는 아래 순서로 저장된다.

```text
[8 bytes entered_at][4 bytes id]
```

여기서 중요한 포인트가 두 개 있다.

### 1. entered_at은 문자열이 아니라 timestamp로 저장한다

입력 SQL에서는 사람이 읽기 쉬운 문자열을 받는다.

```sql
INSERT INTO ENTRY_LOG_BIN VALUES ('2026-04-08 09:00:00', 302);
```

하지만 파일에는 문자열 그대로 저장하지 않고 `int64_t` timestamp로 저장한다.

이유는 아래와 같다.

- 고정 길이 레코드를 만들 수 있다
- 저장 포맷이 단순해진다
- 나중에 비교나 정렬이 쉬워진다
- binary 파일을 안정적으로 읽고 쓸 수 있다

### 2. 구조체 전체를 fwrite 하지 않는다

과제 요구사항에서 특히 강조된 부분이 바로 이것이다.

`fwrite(&record, sizeof(record), 1, file)` 같은 식으로 구조체 전체를 저장하면, 컴파일러 padding 문제 때문에 파일 포맷이 의도와 다르게 깨질 수 있다.

그래서 현재 구현은 필드를 하나씩 직접 쓴다.

- `entered_at` 8바이트 쓰기
- `id` 4바이트 쓰기

이렇게 해야 파일 포맷이 항상 명확하게 유지된다.

## 10. ENTRY_LOG_BIN INSERT에서 권한 검사를 어디서 하는가

`ENTRY_LOG_BIN` INSERT는 단순히 binary append만 하면 되는 작업이 아니다.  
Step 6에서 중요한 요구사항은 **학생 존재 여부와 authorization 검사**였다.

흐름은 아래와 같다.

1. parser가 datetime 문자열과 `id`를 AST로 만든다.
2. executor가 datetime 형식을 검사하고 timestamp로 변환한다.
3. executor가 `STUDENT_CSV`에서 해당 학생을 찾는다.
4. 학생이 없으면 에러다.
5. 학생이 있어도 `authorization != 'T'` 이면 에러다.
6. 두 조건을 통과한 경우에만 binary 파일에 append 한다.

이 권한 검사는 storage가 아니라 executor에 있어야 한다.  
storage는 “파일에 어떻게 저장하는가”만 알아야지, “이 학생이 입장 가능한가” 같은 도메인 규칙까지 알면 안 되기 때문이다.

## 11. parser / executor / storage를 왜 나눴는가

이 프로젝트의 가장 중요한 설계 포인트 중 하나는 역할 분리다.

### parser

- 토큰 배열을 읽는다
- 지원하는 SQL 문법인지 검사한다
- 문장을 AST로 바꾼다
- 파일 I/O는 하지 않는다

### executor

- AST를 보고 실제 동작을 결정한다
- authorization 계산을 한다
- 학생 존재 여부를 검사한다
- SELECT 결과와 에러 메시지를 출력한다
- 필요한 storage 함수를 호출한다

### storage

- 파일 포맷만 안다
- CSV/binary를 읽고 쓴다
- SQL 키워드나 문법은 모른다

이 구조 덕분에 코드가 훨씬 명확해진다.  
예를 들어 storage는 `SELECT`, `WHERE`, `VALUES` 같은 SQL 문법을 몰라도 된다.  
그 대신 “학생 row를 append 한다”, “학생 id로 찾는다”, “entry log 레코드를 append 한다” 같은 데이터 작업만 처리한다.

## 12. 에러 처리에서 중요했던 점

이번 구현에서는 에러가 나면 그 뒤 문장을 계속 실행하지 않고 중단한다.  
예를 들어 중간에 권한 없는 학생의 `ENTRY_LOG_BIN` INSERT가 나오면, 그 뒤 SQL은 실행되지 않는다.

이 정책은 테스트와 시연에서 매우 중요하다.

- 어떤 시점까지 저장되었는지 예측 가능하다
- stderr 메시지가 명확해진다
- 부분 실행 상태를 설명하기 쉽다

또한 stdout/stderr를 분리해서 관리하면 테스트 코드에서 성공 출력과 에러 출력을 정확히 검증할 수 있다.

## 13. 테스트 전략

이 프로젝트는 새 동작을 추가할 때마다 테스트를 붙이는 방식으로 확장했다.

현재 테스트는 아래 범위를 다룬다.

- 파일 읽기와 문장 분리
- tokenizer
- parser
- datetime 파싱/포맷
- student storage
- entry log storage
- Step 4 end-to-end
- Step 5 end-to-end
- Step 6 end-to-end
- 데모 웹 생성 스모크 테스트

이 전략의 장점은 “어느 단계에서 깨졌는지”를 빨리 찾을 수 있다는 점이다.  
예를 들어 tokenizer 테스트, parser 테스트, storage 테스트를 따로 두면 문제가 생겼을 때 범위를 바로 줄일 수 있다.

## 14. 발표/시연 포인트

이번 프로젝트는 CLI 프로그램이지만, 발표용으로는 웹 데모도 같이 만들었다.  
샘플 SQL 파일을 실행한 결과를 브라우저에서 한눈에 볼 수 있도록 정리해 두었다.

데모에서 강조하기 좋은 포인트는 아래와 같다.

- 성공 케이스 2개
- 실패 케이스 여러 개
- `stdout`과 `stderr` 분리
- `STUDENT_CSV`와 `ENTRY_LOG_BIN`의 현재 상태 시각화
- SQL 파일 내용, 실행 명령, 실행 결과를 한 화면에서 비교

즉 최종 결과물은 단순히 “코드가 돌아간다”가 아니라, **왜 이렇게 설계했고 어떤 요구사항을 만족하는지 설명 가능한 상태**까지 포함한다.

## 15. 이번 과제에서 배운 점

이번 과제에서 가장 중요했던 배움은 두 가지였다.

첫째, 작은 프로젝트일수록 **과도한 일반화보다 역할 분리와 명확한 제약**이 더 중요하다는 점이다.  
둘째, 파일 기반 저장은 단순해 보이지만, CSV 포맷, binary 레코드 레이아웃, 에러 처리, 테스트 전략까지 생각하면 꽤 많은 설계 판단이 필요하다는 점이다.

결국 이 프로젝트는 “작은 SQL 엔진”을 만든 것이 아니라,  
**명세가 분명한 작은 시스템을 책임별로 나눠 끝까지 구현하고 설명하는 연습**에 더 가깝다.

## 16. 정리

이 프로젝트의 핵심은 아래 한 문장으로 정리할 수 있다.

> 과제에서 요구한 5가지 SQL만 정확하게 처리하는, 설명하기 쉬운 최소 SQL 프로세서를 C로 구현했다.

겉으로는 단순해 보여도, 내부에는 아래 포인트가 모두 들어 있다.

- tokenizer / parser / executor / storage 분리
- CSV storage와 binary storage 동시 처리
- datetime 검증 및 timestamp 변환
- authorization 자동 계산
- 학생 존재 여부 및 권한 검사
- deterministic stdout/stderr
- 단계별 테스트

그래서 이 프로젝트는 “기능 구현” 자체보다도, **작은 요구사항을 정확한 구조로 끝까지 연결하는 법**을 보여주는 과제라고 볼 수 있다.
