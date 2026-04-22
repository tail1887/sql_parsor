# 02. Architecture

MVP 범위는 `docs/01-product-planning.md`를 따른다.

## 1) 기술 스택 선택 이유


| 영역  | 선택 기술                     | 선택 이유                   | 대안                                   |
| --- | ------------------------- | ----------------------- | ------------------------------------ |
| 언어  | C (C11 권장)                | 과제 요구사항, 저수준 I/O·메모리 이해 | C++ (과제 범위 밖이면 비권장)                  |
| 빌드  | CMake                     | 크로스 플랫폼, CTest 통합       | Makefile만 (Windows 팀원 고려 시 CMake 우선) |
| 테스트 | CTest + 테스트 실행 파일 또는 스크립트 | 회귀·발표 시 검증 재현           | 수동 실행만 (비권장)                         |
| 저장소 | OS 파일시스템 + **CSV**        | 디버깅·발표·diff 용이          | 바이너리(구현 부담↑)                         |


## 2) 시스템 구성

```mermaid
flowchart LR
    SqlFile[SqlFile] --> CLI[CLI_main]
    Client[HTTP_Client] --> Api[API_Server]
    Api --> SockQ[Socket_Queue]
    SockQ --> IoPool[IO_Thread_Pool]
    IoPool --> ReqQ[Parsed_Request_Queue]
    ReqQ --> Pool[Worker_Pool]
    Pool --> Adapter[Engine_Adapter]
    Adapter --> Lock[Table_Fair_RWLock]
    Lock --> Lexer[Lexer]
    CLI --> Lexer[Lexer]
    Lexer --> Parser[Parser]
    Parser --> AST[StmtAST]
    AST --> Exec[Executor]
    Exec --> Store[Storage_CSV]
    Store --> FS[(data_table_csv)]
    Exec --> Idx[Week7_BplusIndex]
    Exec --> Out[Stdout]
    Api --> Json[JSON_Response]
```



설명:

- **CLI**: 인자로 받은 `.sql` 파일을 읽어 **문장 단위**로 파서에 넘긴다.
- **API Server**: `POST /query` 요청을 받아 **socket queue -> I/O thread -> parsed request queue -> worker** 흐름으로 처리한다. worker는 **완성된 요청 객체**만 받아 SQL 실행과 응답 전송을 맡는다.
- **Engine Adapter**: API 서버와 기존 SQL 엔진 사이의 얇은 접점이다. 요청 SQL에서 테이블을 추출해 **테이블 단위 공정 read/write lock** 을 잡는다. `SELECT`는 read lock, `INSERT`와 미분류 요청은 write lock을 사용한다.
- **Lexer**: 문자 스트림을 토큰 스트림으로 변환한다.
- **Parser**: 토큰에서 **INSERT** / **SELECT** 구문 트리를 만든다.
- **Executor**: AST를 해석해 Storage를 호출하고, SELECT 결과를 **구조화 결과 + TSV 렌더링**으로 공용화한다.
- **Storage**: 논리 테이블명을 물리 경로로 매핑해 **fopen / append / read** 한다.
- **Week7_BplusIndex**: CSV 헤더 첫 컬럼이 `id`인 테이블에 한해, 프로세스 메모리 B+ 트리로 PK 룩업·자동 증가 `id`를 지원한다(상세는 `docs/weeks/WEEK7/sequences.md`).

## 3) 레이어 구조

권장 디렉터리(구현 시):

```text
include/
├─ api_server.h
├─ engine_adapter.h
├─ lexer.h
├─ parser.h
├─ ast.h
├─ executor.h
├─ http_parser.h
├─ response_builder.h
├─ sql_processor.h
└─ csv_storage.h
src/
├─ api_server.c
├─ engine_adapter.c
├─ main.c
├─ main_api_server.c
├─ lexer.c
├─ parser.c
├─ ast.c
├─ executor.c
├─ http_parser.c
├─ response_builder.c
├─ sql_processor.c
└─ csv_storage.c
tests/
├─ test_lexer.c
├─ test_parser.c
├─ ...
└─ sql/               # 통합용 .sql 픽스처
data/
└─ *.csv              # 사전 준비 테이블 파일(헤더 포함)
```

책임:

- **main.c**: 인자 검증, 파일 읽기, 문장 루프, 종료 코드
- **main_api_server.c**: `sql_api_server [port] [workers]` 인자 처리, 시그널 종료 처리
- **api_server.c**: `accept -> socket queue -> I/O thread pool -> parsed request queue -> worker pool` 관리, HTTP 응답 전송
- **http_parser.c**: HTTP request line / header / `Content-Length` / JSON body 파싱
- **response_builder.c**: 엔진 결과를 JSON body + HTTP 응답 문자열로 변환
- **engine_adapter.c**: 엔진 호출용 테이블 단위 공정 read/write lock 보호 + lock 대기 시간 측정
- **lexer.c**: 토큰화만 (키워드 대소문자 정책은 `docs/03-api-reference.md`)
- **parser.c**: 구문 분석만
- **executor.c**: “무엇을 할지” — INSERT/SELECT 의미
- **sql_processor.c**: CLI용 스크립트 실행 + API용 단일 문장 실행 진입점
- **csv_storage.c**: “파일에 어떻게 쓰고 읽을지” — 경로, 인코딩 가정, 이스케이프

## 4) 데이터 모델

### 논리: 테이블

- 테이블 이름은 SQL 식별자 규칙에 따른다(자세한 규칙은 `docs/03-api-reference.md`).
- 각 테이블은 **하나의 CSV 파일**에 대응한다.

### 물리: 파일 매핑

- **기본 규칙**: `data/<table_name>.csv`
- **작업 디렉터리**: CLI를 실행한 **현재 작업 디렉터리(CWD)** 를 기준으로 상대 경로를 해석한다(문서·README·테스트에서 동일하게 유지).

### CSV 형식(고정)

1. **첫 번째 줄**: 헤더 행 — 컬럼 이름, 콤마 구분. 테이블 스키마는 이 헤더에 의해 정의된다.
2. **둘째 줄 이후**: 데이터 행. **INSERT** 는 **항상 파일 끝에 한 줄**을 추가한다.
3. 필드 구분자: **콤마(`,`)**
4. 문자열 필드: **큰따옴표(`"`)** 로 감싼다. 내부 `"` 는 `""` 로 이스케이프(RFC 4180 스타일 권장).
5. 숫자: 따옴표 없이 저장 가능.
6. NULL 표현: 구현이 채택한 리터럴(예: 빈 필드 또는 `\N`)을 `docs/03-api-reference.md`와 **동일**하게 유지한다.

**INSERT** 시 컬럼 개수는 헤더 컬럼 수와 **일치**해야 한다. 불일치 시 에러.

## 5) 정합성·엣지 규칙

- **파일 없음 + INSERT**: `data/<table>.csv` 가 없으면 **에러**(테이블은 사전 존재 가정). (구현 편의상 자동 생성으로 바꿀 경우 문서·테스트 동시 수정.)
- **파일 없음 + SELECT**: 에러.
- **빈 데이터(헤더만)**: SELECT 는 헤더만 또는 0행 출력 — 동작을 `docs/03-api-reference.md`에 맞출 것.
- **API 요청 단위**: HTTP 연결 하나당 요청 하나만 처리하고 응답 후 연결을 닫는다. `Content-Length`만 지원하고 chunked / keep-alive 는 지원하지 않는다.
- **요청 큐 구조**: accept thread는 socket fd를 **socket queue**에 넣고, I/O thread가 요청을 끝까지 읽고 검증한 뒤 **parsed request queue**에 넣는다. worker는 body 읽기 없이 SQL 실행만 담당한다.
- **동시 실행**: API 서버는 여러 worker를 두고, `SELECT`는 **테이블 단위 read lock** 으로 같은 테이블에 대해 함께 실행할 수 있다. `INSERT`와 미분류 요청은 **테이블 단위 write lock** 으로 직렬화한다. 다른 테이블끼리는 서로 다른 lock entry를 사용하므로 경합이 줄어든다.
- **writer starvation 방지**: 테이블 lock은 writer 대기 중 신규 reader를 막는 **writer-preferred fair rwlock** 정책을 사용한다.
- **WEEK7 인덱스 lazy-load**: `WHERE id = ...` 경로에서 쓰는 프로세스 메모리 인덱스 캐시는 `week7_index.c` 내부 `rwlock` 으로 초기 로딩과 조회를 보호한다.
- **lock order**: `g_table_map_mutex` 로 테이블 lock entry를 찾거나 만든 뒤 해제하고, 그 다음 **table fair rwlock** 을 잡는다. 엔진 실행 중 `week7_index.c` 내부 lock이 추가로 사용될 수 있으나, map mutex를 쥔 채로 table lock을 기다리지는 않는다.
- **큐 포화 시**: socket queue 또는 parsed request queue 가 가득 차면 해당 요청은 즉시 `503 Service Unavailable` 로 거절한다.
- **측정 지표**: 운영 중 `SQL_API_SERVER_METRICS=1` 을 켜면 queue 대기 시간, request read/parse 시간, lock 대기 시간, SQL 실행 시간, 전체 응답 시간을 stderr 로 기록할 수 있다.

## 6) 핵심 시퀀스

**6.1·6.2**는 6주차까지의 **MVP 실행 흐름**이다(기존과 동일).  
**WEEK7(B+ 인덱스 연계)** 용 시퀀스는 학습·설계 분리를 위해 `[docs/weeks/WEEK7/sequences.md](weeks/WEEK7/sequences.md)`에만 상세 다이어그램을 둔다. 코드에서는 `executor` 등에 문서 경로를 주석으로만 짚어도 된다.

### 6.1 MVP — INSERT 한 문장

```mermaid
sequenceDiagram
    participant M as main
    participant P as parser
    participant E as executor
    participant S as csv_storage

    M->>P: parse_sql(text)
    P-->>M: InsertStmt
    M->>E: execute(insert)
    E->>S: append_row(path, values)
    S-->>E: ok_or_error
    E-->>M: status
```



### 6.2 MVP — SELECT 한 문장

```mermaid
sequenceDiagram
    participant M as main
    participant P as parser
    participant E as executor
    participant S as csv_storage

    M->>P: parse_sql(text)
    P-->>M: SelectStmt
    M->>E: execute(select)
    E->>S: read_all(path)
    S-->>E: rows
    E-->>M: print_stdout
```



### 6.3 WEEK7 연계 — 시퀀스 다이어그램 위치

B+ 트리·자동 `id`·`WHERE id = …` 가 붙은 뒤의 **INSERT / SELECT(인덱스 vs 풀스캔)** 시퀀스는 파일이 길어질 수 있으므로, **이 저장소에서는 주차 문서로만** 관리한다.

- **파일**: `[docs/weeks/WEEK7/sequences.md](weeks/WEEK7/sequences.md)`
- **갱신 시점**: 인덱스 삽입 시점, 룩업 실패 시 정책, `read_all` 대신 부분 읽기 API 등이 코드에 반영될 때마다 위 파일과 필요 시 `docs/03-api-reference.md`를 함께 맞춘다.

### 6.4 WEEK8 — API 서버 요청

```mermaid
sequenceDiagram
    participant C as Client
    participant A as accept_thread
    participant SQ as socket_queue
    participant I as io_worker
    participant RQ as parsed_request_queue
    participant W as worker
    participant D as engine_adapter
    participant E as sql_processor

    C->>A: POST /query
    A->>SQ: enqueue(socket fd)
    SQ-->>I: dequeue(socket fd)
    I->>I: read + parse + validate
    I->>RQ: enqueue(parsed request)
    RQ-->>W: dequeue(parsed request)
    W->>D: execute_sql(sql)
    D->>E: sql_processor_run_text(sql)
    E-->>D: SqlExecutionResult
    D-->>W: result
    W-->>C: HTTP/1.1 + JSON
```

## 7) 운영·실행 메모

- **빌드 산출물**: CMake 기본 빌드 트리 `build/` (gitignore 권장).
- **실행 파일**: `sql_processor`, `sql_processor_trace`, `sql_api_server`
- **Windows**: Visual Studio 생성기 사용 시 실행 파일이 `build/Debug/sql_processor.exe` 또는 `build/Release/sql_processor.exe` 일 수 있다. README에 **두 경우**를 명시한다.
- **API 서버 기본값**: 포트 `8080`, worker `4`, bounded queue(기본 64)
- **환경 변수**: MVP 에서 필수 아님.
- **로깅**: stderr 에 human-readable 메시지로 충분.


## 8) Data Integrity Rules

- CSV rows that contain only whitespace are ignored consistently by csv_storage_read_table, csv_storage_read_table_row, and csv_storage_data_row_count.
- For WEEK7 tables whose first header column is id, each stored id must be a strict integer string. Extra trailing characters or whitespace inside the stored field are treated as corrupted data.
- Duplicate stored id values in a WEEK7 id PK table are treated as corrupted data. Index loading fails instead of silently replacing an earlier row reference.
