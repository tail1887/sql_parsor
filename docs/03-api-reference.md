# 03. CLI 및 SQL 계약 (API Reference)

이 문서는 HTTP API가 아니라 **CLI 인자·종료 코드·지원 SQL 문법·에러 출력** 에 대한 **구현 계약**이다. 동작을 바꿀 때는 본 문서와 테스트를 함께 갱신한다.

## 1) 공통 규칙

- **입력**: 첫 번째 인자로 **단일 SQL 스크립트 파일 경로**를 받는다.
- **stdin**: MVP 에서 **사용하지 않는다**(향후 확장 여지만 문서에 남김).
- **출력**
  - **stdout**: SELECT 결과 및 정상 메시지(필요 시).
  - **stderr**: 진단·에러 메시지(한 줄 또는 짧은 여러 줄).
- **인코딩**: **UTF-8** 입력 파일을 가정한다(Windows 콘솔 표시는 환경 의존 — 발표 시 터미널 UTF-8 설정 권장).
- **식별자**: 테이블·컬럼 이름은 **ASCII 문자로 시작**, 이후 문자·숫자·밑줄(`_`) 허용. 키워드는 대소문자 **무시**(case-insensitive)로 파싱하는 것을 권장.

## 2) 종료 코드 (exit code)

| Code | 의미 |
| --- | --- |
| `0` | 스크립트 전체 성공(모든 문장 처리 완료) |
| `1` | 잘못된 CLI 사용(인자 개수 등) |
| `2` | 구문 오류(SQL parse error) |
| `3` | 실행 오류(테이블 없음, 컬럼 수 불일치, I/O 실패 등) |

구현체는 위 구분을 유지하는 한, 세부 숫자는 조정 가능하나 **문서·테스트와 함께** 변경한다.

## 3) CLI 사용법

```text
sql_processor <path.sql>
```

- `<path.sql>`: UTF-8 텍스트 파일. 여러 SQL 문을 **세미콜론(`;`)** 으로 구분한다.
- 마지막 문장 뒤 세미콜론은 선택 사항으로 허용할 수 있다(구현 일관성 유지).

**잘못된 사용 예**

```text
sql_processor
sql_processor a.sql b.sql
```

→ stderr 에 사용법 메시지, 종료 코드 `1`.

## 4) 지원 SQL 문법 (MVP)

### 4.1 INSERT

```sql
INSERT INTO <table> VALUES ( <value_list> );
```

- `<table>`: 식별자. 물리 파일 `data/<table>.csv` 에 매핑.
- `<value_list>`: 콤마로 구분된 값. 개수는 **헤더 컬럼 수와 동일**해야 한다.
- **리터럴**
  - **정수**: `[+-]?[0-9]+`
  - **실수**: 구현 선택(지원 시 문서에 정규식 명시)
  - **문자열**: 작은따옴표 `'...'` 로 감싼다. 내부 `'` 는 `''` 로 이스케이프.
  - **NULL**: 키워드 `NULL` (대소문자 무시)

INSERT 성공 시: 대상 CSV **마지막에 데이터 행 한 줄** 추가(RFC 4180 스타일 따옴표·이스케이프로 직렬화).

- 파서(INSERT만): `parser_parse_insert` → `InsertStmt` (`include/parser.h`, `include/ast.h`). 문장 끝은 `;` 또는 입력 끝(EOF) 허용.

### 4.2 SELECT

```sql
SELECT * FROM <table> ;
SELECT <column> [, <column> ...] FROM <table> ;
```

- `*`: 모든 컬럼을 헤더 순서대로 출력.
- 컬럼 목록: 헤더에 존재하는 이름만 허용. 순서는 **쿼리에 적힌 순서**.
- **Stretch**: `WHERE <column> = <literal>` 형태는 `docs/01-product-planning.md` 의 Optional 에 따른다. MVP 에 포함하지 않으면 **파싱 시 구문 오류**로 거절해도 된다.

### 4.3 출력 포맷 (stdout)

- **기본(MVP)**: **TSV**(탭 구분). 첫 줄에 컬럼 헤더, 이후 각 데이터 행.
- 구현체가 CSV로 출력하도록 바꿀 경우, 본 절과 README 의 예시를 **동시에** 수정한다.

## 5) 문장 구분·공백

- 문장은 `;` 로 끝난다.
- 공백·개행은 토큰 사이에 자유롭게 허용.
- `--` 로 시작하는 줄은 **행 주석**으로 무시한다(MVP 권장).
- 문자열 리터럴은 §4.1과 같이 **작은따옴표** (`'...'`, 내부 `''`) 기준으로 토큰화한다.
- Lexer 구현체: `include/lexer.h`, `src/lexer.c` — 토큰 종류는 `TokenKind` 열거형에 맞춘다(MVP 키워드: `INSERT`, `INTO`, `VALUES`, `SELECT`, `FROM`, `NULL`).

## 6) 에러 메시지 (stderr)

- 사람이 읽을 수 있는 **짧은 영문 또는 한글** 메시지(팀 통일).
- 가능하면 **파일 경로·라인 번호·문맥** 을 포함한다.

예시(형식 자유, 일관성 권장):

```text
parse error: line 3: unexpected token near 'FORM'
exec error: data/users.csv: column count mismatch (expected 3, got 2)
io error: failed to open data/missing.csv
```

## 7) 파일·테이블 엣지 케이스

| 상황 | 기대 동작 |
| --- | --- |
| `data/<table>.csv` 없음 + INSERT | 종료 코드 `3`, stderr (테이블/파일 없음) |
| `data/<table>.csv` 없음 + SELECT | 종료 코드 `3` |
| 헤더만 있는 CSV + SELECT | 헤더만 출력 후 행 없음 |
| INSERT 값 개수 ≠ 컬럼 수 | 종료 코드 `3`, stderr |
| 따옴표·콤마 포함 문자열 | 파서·CSV 직렬화가 RFC 4180 스타일과 호환되게 처리 |

## 8) 오픈 이슈

- [ ] 실수 리터럴 지원 여부
- [ ] SELECT 출력을 TSV 고정 vs CSV 고정
- [ ] Stretch WHERE 채택 시 비교 연산·타입 규칙
