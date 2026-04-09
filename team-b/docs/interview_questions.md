# Tiny File-Based SQL Processor 인터뷰 질문 정리

## 1. 프로젝트 전체 이해 질문

### Q1. 이 프로젝트를 한 줄로 설명해보세요.

핵심 답변 포인트:

- C99로 만든 작은 파일 기반 SQL 프로세서
- 범용 DBMS가 아니라 명세에 있는 5가지 SQL만 지원
- tokenizer, parser, executor, storage를 분리한 구조

### Q2. 이 프로젝트에서 실제로 만들고자 한 테이블은 무엇인가요?

핵심 답변 포인트:

- `STUDENT_CSV`와 `ENTRY_LOG_BIN` 두 테이블을 코드로 구현한 과제
- `STUDENT_CSV`는 학생 기준 정보 테이블
- `ENTRY_LOG_BIN`은 학생 출입 이벤트 로그 테이블
- 두 테이블은 `id`로 연결됨

### Q3. 왜 `STUDENT_CSV`는 CSV이고 `ENTRY_LOG_BIN`은 binary인가요?

핵심 답변 포인트:

- `STUDENT_CSV`는 사람이 읽고 확인하기 쉬운 마스터 데이터라 CSV가 어울림
- `ENTRY_LOG_BIN`은 append-only 로그 데이터라 fixed-size binary가 어울림
- 한 프로젝트 안에서 CSV와 binary 두 가지 storage를 모두 다루는 것이 과제의 핵심 학습 포인트였음

### Q4. 왜 범용 SQL 엔진으로 만들지 않았나요?

핵심 답변 포인트:

- 과제 요구사항이 명확하게 제한되어 있었음
- 최소 구현과 설명 가능성이 더 중요했음
- 일반화를 하면 코드가 길어지고 디버깅과 테스트가 어려워짐

### Q5. 최종 파이프라인을 함수 호출 순서로 설명해보세요.

핵심 답변 포인트:

- `main`
- `read_text_file`
- `split_sql_statements`
- `tokenize_sql`
- `parse_statement`
- `execute_statement`
- `student_storage` 또는 `entry_log_storage`
- stdout/stderr 출력

### Q6. parser, executor, storage 책임은 각각 무엇인가요?

핵심 답변 포인트:

- parser는 SQL을 AST로 바꾸는 역할
- executor는 AST를 실제 동작으로 연결하는 역할
- storage는 파일 포맷에 맞게 읽고 쓰는 역할

### Q7. 왜 storage가 SQL 문법을 몰라야 하나요?

핵심 답변 포인트:

- storage는 파일 I/O만 담당해야 함
- SQL 문법을 알면 parser/executor와 책임이 섞임
- 테스트와 재사용성이 나빠짐

## 2. splitter / tokenizer / parser 질문

### Q8. splitter는 왜 줄 단위가 아니라 세미콜론 단위인가요?

핵심 답변 포인트:

- SQL 문장은 줄 수와 무관함
- 한 줄에 여러 문장이 있을 수도 있고, 한 문장이 여러 줄에 걸칠 수도 있음
- 문장 경계는 `;` 로 판단하는 것이 맞음

### Q9. tokenizer가 왜 모든 SQL을 일반적으로 처리하지 않나요?

핵심 답변 포인트:

- 과제 범위가 5가지 문법으로 제한되어 있음
- 최소 구현이 목표
- 지원 범위 밖 문법은 parser나 tokenizer 단계에서 실패하도록 설계

### Q10. `Token.text`에 길이 필드가 없는데 어떻게 문자열을 읽나요?

핵심 답변 포인트:

- 널 종료 문자열로 저장함
- `length + 1` 만큼 메모리 할당
- 마지막에 `'\0'` 을 붙여서 `strlen`, `strcmp`, `strtol` 가능

### Q11. `TokenList`의 `count`는 무슨 의미인가요?

핵심 답변 포인트:

- 현재 실제로 들어 있는 토큰 개수
- parser가 끝까지 읽었는지 확인할 때 사용

### Q12. parser의 `current_index` 개념을 설명해보세요.

핵심 답변 포인트:

- 현재 읽고 있는 토큰 위치
- `current_token()` 은 현재 토큰을 봄
- `advance_parser()` 는 한 칸 이동
- `consume_token()` 은 기대한 토큰인지 확인 후 이동

### Q13. 왜 `SELECT ALL`과 `SELECT WHERE`를 다른 statement type으로 나눴나요?

핵심 답변 포인트:

- executor 입장에서 필요한 데이터가 다름
- `SELECT ALL`은 추가 데이터가 필요 없음
- `SELECT BY ID`는 `id` 값이 필요함
- 작은 프로젝트에서는 타입을 분리하는 편이 더 단순함

### Q14. parser가 문장 끝까지 다 읽었는지 왜 확인하나요?

핵심 답변 포인트:

- 앞부분만 맞고 뒤에 쓰레기 토큰이 붙은 문장을 막기 위해서
- 예: `SELECT * FROM STUDENT_CSV; garbage`

## 3. STUDENT_CSV 관련 질문

### Q15. `student.csv` 헤더는 왜 필요한가요?

핵심 답변 포인트:

- 명세가 헤더 포함 CSV를 요구함
- 현재 구현도 첫 줄이 헤더인지 검증함
- 빈 파일 상태를 “정상적인 빈 테이블”로 표현할 수 있음
- 사람이 열었을 때 의미를 바로 알 수 있음

### Q16. `student.csv` row 포맷을 설명해보세요.

핵심 답변 포인트:

- 헤더: `id,name,class,authorization`
- row 예시: `302,Kim,302,T`
- `authorization` 은 `T/F`

### Q17. authorization은 어디서 계산하나요?

핵심 답변 포인트:

- executor에서 계산
- storage가 아니라 실행 규칙에 속하는 로직
- 현재는 `class == 302` 또는 `class == 100`이면 `T`, 아니면 `F`

### Q18. 중복 id 검사는 어떻게 하나요?

핵심 답변 포인트:

- INSERT 전에 파일 전체를 읽음
- `find_student_record_by_id()` 로 같은 `id`가 있는지 검사
- 현재 구현은 선형 탐색
- 작은 프로젝트에서는 가장 단순하고 설명하기 쉬운 방식

### Q19. 중복 검사 로직이 비효율적인데 왜 이렇게 했나요?

핵심 답변 포인트:

- 파일 기반 최소 구현이라 인덱스나 해시를 두지 않음
- 데이터가 크지 않다는 전제
- 구현 단순성과 명확성을 우선

### Q20. `name`에 공백/쉼표/줄바꿈을 금지한 이유는 무엇인가요?

핵심 답변 포인트:

- CSV 파서를 최소 구현으로 유지하기 위해서
- 복잡한 escaping 규칙을 구현하지 않기 위해서
- 과제 명세 범위에 맞춘 제약

### Q21. `SELECT * FROM STUDENT_CSV WHERE id = 302;` 는 어떻게 실행되나요?

핵심 답변 포인트:

- parser가 `STATEMENT_SELECT_STUDENT_BY_ID` 생성
- executor가 `find_student_record_by_id()` 호출
- 내부적으로 CSV 전체를 읽고 메모리에서 `id` 선형 탐색
- 찾으면 헤더와 한 row 출력
- 없으면 `no rows found`

## 4. ENTRY_LOG_BIN 관련 질문

### Q22. `entered_at`은 왜 문자열이 아니라 timestamp로 저장하나요?

핵심 답변 포인트:

- 입력은 사람이 읽기 쉬운 문자열
- 저장은 기계가 다루기 쉬운 고정 길이 정수
- binary 레코드를 일정한 길이로 유지할 수 있음

### Q23. binary 레코드 12바이트 구성을 말해보세요.

핵심 답변 포인트:

- `entered_at`: 8바이트 `int64_t`
- `id`: 4바이트 `int32_t`
- 총 12바이트

### Q24. 구조체 전체를 `fwrite` 하지 않은 이유는 무엇인가요?

핵심 답변 포인트:

- padding 때문에 파일 포맷이 달라질 수 있음
- 컴파일러/환경 차이에 취약함
- 파일 포맷을 정확히 `8 + 4` 바이트로 고정하려고 필드별로 씀

### Q25. datetime 파싱 함수의 실패 조건을 설명해보세요.

핵심 답변 포인트:

- 길이가 19자가 아니면 실패
- `-`, 공백, `:` 위치가 틀리면 실패
- 숫자 자리에 숫자가 아니면 실패
- 월/일/시/분/초 범위가 틀리면 실패
- 실제 달력상 존재하지 않는 날짜면 실패

### Q26. SELECT에서 timestamp를 다시 문자열로 바꾸는 흐름은 무엇인가요?

핵심 답변 포인트:

- storage는 binary에서 정수 timestamp를 읽음
- executor가 출력 직전에 포맷팅 함수로 문자열로 변환
- 화면에는 `YYYY-MM-DD HH:MM:SS` 형태로 출력

### Q27. binary 읽기/쓰기에서 가장 실수하기 쉬운 부분은 무엇인가요?

핵심 답변 포인트:

- 필드 순서를 읽기/쓰기에서 다르게 쓰는 것
- 타입 크기를 잘못 쓰는 것
- EOF와 깨진 레코드를 구분하지 않는 것

## 5. Step 6 권한 검사 질문

### Q28. `ENTRY_LOG_BIN INSERT`에서 학생 존재 여부와 권한 검사는 어디서 하나요?

핵심 답변 포인트:

- executor에서 함
- 학생을 `STUDENT_CSV`에서 조회
- 학생이 없으면 실패
- `authorization != 'T'` 면 실패
- 통과한 경우에만 binary append

### Q29. 왜 이 권한 검사를 storage가 아니라 executor에서 하나요?

핵심 답변 포인트:

- storage는 파일 포맷만 알아야 함
- 권한 검사는 비즈니스 규칙
- 역할을 섞으면 구조가 흐려짐

### Q30. 중간 문장에서 에러가 나면 왜 뒤 문장을 멈추나요?

핵심 답변 포인트:

- 상태 예측 가능성을 높이기 위해서
- 테스트와 시연이 쉬워짐
- 부분 실행 결과를 명확하게 설명할 수 있음

## 6. 테스트와 시연 질문

### Q31. 테스트는 어떤 단위로 나눴나요?

핵심 답변 포인트:

- splitter/tokenizer/parser/storage/utils/end-to-end로 나눔
- 어떤 단계에서 깨졌는지 빨리 찾기 위해서

### Q32. 왜 stdout과 stderr를 분리했나요?

핵심 답변 포인트:

- 정상 결과와 에러를 명확하게 구분하기 위해서
- 테스트에서 더 쉽게 검증하기 위해서

### Q33. 발표에서 어떤 실패 케이스를 보여주면 좋은가요?

핵심 답변 포인트:

- 학생 중복 id
- 지원하지 않는 SQL
- 세미콜론 누락
- 닫히지 않은 문자열
- 권한 없는 학생의 entry log
- 존재하지 않는 학생의 entry log
- 중간 에러 후 이후 문장 중단

### Q34. 웹 데모를 왜 만들었나요?

핵심 답변 포인트:

- CLI 결과를 한눈에 보기 어렵기 때문
- SQL 파일, 실행 명령, stdout, stderr, 저장 상태를 한 화면에서 비교하려고
- 발표에서 설명 효율을 높이기 위해서

## 7. 깊게 들어오면 나올 수 있는 질문

### Q35. `Token`에 `length` 필드를 추가하면 뭐가 좋아지나요?

핵심 답변 포인트:

- 매번 `strlen()` 안 해도 됨
- slice 구조로 확장하기 쉬움
- 널 문자에 덜 의존할 수 있음
- 하지만 현재 과제에서는 단순성이 더 중요했음

### Q36. `TokenList`에 `capacity`를 넣지 않은 이유는 무엇인가요?

핵심 답변 포인트:

- 현재는 tokenizer 내부에서만 동적 배열을 관리하면 충분했음
- 컨테이너 일반화보다 단순한 구현을 우선

### Q37. `SELECT BY ID`도 파일 전체를 읽는 이유는 무엇인가요?

핵심 답변 포인트:

- 인덱스를 만들지 않았기 때문
- 파일 기반 최소 구현에서는 선형 탐색이 가장 단순함
- 데이터가 작다는 전제에서 허용 가능한 선택

### Q38. CSV storage와 binary storage를 한 프로젝트에서 같이 다룬 의미는 무엇인가요?

핵심 답변 포인트:

- 기준 테이블과 이벤트 로그의 성격 차이를 드러낼 수 있었음
- 텍스트 기반 저장과 binary 직렬화를 모두 연습할 수 있었음
- 서로 다른 storage format을 같은 executor 파이프라인에 연결하는 연습이 되었음

## 8. 빠르게 자기 점검할 체크리스트

아래 질문에 막힘 없이 답할 수 있으면 준비가 잘 된 상태다.

- 지원하는 SQL 5가지를 정확히 말할 수 있는가
- 이 프로젝트가 실제로 다루는 두 테이블의 역할을 설명할 수 있는가
- 왜 학생은 CSV이고 로그는 binary인지 설명할 수 있는가
- 전체 파이프라인을 함수 순서대로 설명할 수 있는가
- parser / executor / storage 책임을 구분해서 말할 수 있는가
- `student.csv` 헤더와 row 포맷을 말할 수 있는가
- authorization 계산 위치와 규칙을 말할 수 있는가
- 중복 id 검사가 파일 전체 스캔이라는 점을 설명할 수 있는가
- binary 레코드 12바이트 구성을 말할 수 있는가
- 구조체 전체 `fwrite`를 왜 피했는지 설명할 수 있는가
- Step 6 권한 검사 흐름을 설명할 수 있는가
- 실패 시 이후 문장 실행을 멈추는 이유를 말할 수 있는가

## 9. 마지막 정리

면접이나 발표에서 가장 좋은 답변은 “코드를 길게 읊는 것”이 아니라 아래 3가지를 분명하게 말하는 것이다.

1. 이 프로젝트는 범용 SQL 엔진이 아니라, 명세에 맞춘 최소 구현이다.
2. tokenizer, parser, executor, storage를 분리해서 책임을 명확히 했다.
3. CSV / binary / datetime / authorization / 에러 처리까지 테스트 가능한 구조로 끝까지 연결했다.

이 세 문장만 정확하게 전달해도 전체 설계 의도는 충분히 드러난다.
