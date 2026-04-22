# API Server Layer Spec

이 문서는 **기존 SQL 엔진을 최대한 보존한 채** C 기반 HTTP API 서버를 붙이는 구현 계약이다.  
CLI/SQL 문법 자체는 `docs/03-api-reference.md`를 따르고, 여기서는 **서버 레이어의 요청/응답/동시성** 만 정의한다.

## 1) 목표

- 기존 `sql_processor` CLI는 유지한다.
- 새 실행 파일 `sql_api_server`를 추가한다.
- 외부 클라이언트는 `POST /query` 로 **단일 SQL 문장 1개**를 보낸다.
- 서버는 기존 엔진을 직접 호출하지 않고 `engine_adapter` 를 통해 호출한다.
- 결과는 JSON으로 반환한다.

## 2) 실행 계약

```text
sql_api_server [port] [workers]
```

- `port` 기본값: `8080`
- `workers` 기본값: `4`
- 연결 하나당 요청 하나만 처리하고 응답 후 소켓을 닫는다.
- `Content-Length` 기반 body만 지원한다.
- chunked transfer / keep-alive / streaming response 는 지원하지 않는다.

## 3) 서버 구조

```text
HTTP client
-> accept thread
-> bounded task queue
-> worker threads
-> engine_adapter
-> sql_processor_run_text
-> JSON response
```

핵심 원칙:

- 기존 parser / executor / CSV / WEEK7 B+ 인덱스는 재작성하지 않는다.
- `engine_adapter` 는 `SELECT`에 **전역 read lock**, `INSERT`와 미분류 요청에 **전역 write lock** 을 적용한다.
- `WHERE id = ...` 경로의 WEEK7 인덱스 lazy-load 는 `week7_index.c` 내부 lock 으로 별도 보호한다.
- worker 는 요청 파싱, 엔진 호출, 응답 작성, 소켓 종료까지 책임진다.
- queue 가 가득 차면 요청은 즉시 `503` 으로 거절한다.

## 4) 엔진 접점

서버는 `sql_processor_run_text(const char *sql, SqlExecutionResult *out, FILE *err)` 만 사용한다.

`SqlExecutionResult` 필드:

- `statement_type`
- `exit_code`
- `message`
- `affected_rows`
- `column_count`
- `columns`
- `row_count`
- `rows`

규칙:

- `sql_processor_run_text` 는 **단일 SQL 문장만 허용**한다.
- 여러 문장 입력 시 `exit_code = 2`, `message = "parse error: exactly one statement required"`
- SELECT 결과는 TSV를 다시 파싱하지 않고 구조화 결과를 직접 채운다.
- JSON `rows` 의 셀 값은 모두 **문자열**이다.

## 5) HTTP 계약

### Endpoint

`POST /query`

### Request

헤더:

```http
Content-Type: application/json
Content-Length: <n>
```

body:

```json
{
  "sql": "SELECT * FROM users;"
}
```

### Success Response

#### SELECT

```json
{
  "status": "ok",
  "statementType": "select",
  "message": "2 rows selected",
  "columns": ["id", "name", "email"],
  "rows": [
    ["1", "alice", "alice@example.com"],
    ["2", "bob", "bob@example.com"]
  ]
}
```

#### INSERT

```json
{
  "status": "ok",
  "statementType": "insert",
  "message": "1 row inserted",
  "affectedRows": 1
}
```

### Engine Error Response

HTTP status 는 `200 OK` 를 유지하고, body 에 엔진 오류를 담는다.

```json
{
  "status": "error",
  "statementType": "select",
  "message": "exec error: statement 1 failed (SELECT)",
  "exitCode": 3
}
```

### HTTP Error Responses

- `400 Bad Request`
  - malformed request line / header
  - missing or invalid `Content-Length`
  - invalid JSON body
  - empty `sql`
- `404 Not Found`
  - unsupported path
- `405 Method Not Allowed`
  - `/query` 에 대한 비-POST 요청
- `503 Service Unavailable`
  - bounded queue 포화

에러 body 형식:

```json
{
  "status": "error",
  "message": "..."
}
```

## 6) 테스트 기준

- `test_sql_processor_api`: 엔진 구조화 결과 / 단일 문장 제약 / exec error 확인
- `test_http_parser`: request line, `Content-Length`, JSON `sql` 추출 확인
- `test_api_server`: SELECT/INSERT/400/404/405/503, 동시 `SELECT WHERE id = ...`, 동시 insert, 혼합 read/write 확인
- 기존 `test_executor`, `test_main_integration`, `test_data_integrity` 는 계속 통과해야 한다
