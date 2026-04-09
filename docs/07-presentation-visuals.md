# 07. 발표용 시각화 다이어그램 모음

이 문서는 `docs/06-presentation-script-4min.md` 대본 순서에 맞춘 시각화 자료입니다.  
발표 중에는 섹션 제목과 다이어그램만 보여주고, 설명은 대본으로 진행하면 가장 깔끔합니다.

---

## 1) 협업 방식(A/B 조 비교)

```mermaid
flowchart TB
    start[Same_Goal_SQL_Processor] --> teamA[A_Team]
    start --> teamB[B_Team]

    teamA --> aStyle[BottomUp_Component_Flow]
    aStyle --> aStorage[CSV_Centered]
    aStyle --> aOutput[TSV_Stdout_Focus]

    teamB --> bStyle[UseCase_Centered]
    bStyle --> bStorage[CSV_Plus_Binary]
    bStyle --> bRules[RuleHeavy_Executor]
```



핵심 메시지:

- A조: Bottom-up(구성 요소부터) · 파이프라인 가시성
- B조: Use-case 중심(테이블·시나리오 먼저, 그에 맞는 기능) · 도메인 규칙

---

## 2) 전체 아키텍처(큰 그림)

```mermaid
flowchart LR
    sqlFile[SQL_File] --> cli[CLI_RunLoop]
    cli --> lexer[Lexer]
    lexer --> parser[Parser_AST]
    parser --> exec[Executor]
    exec --> storage[Storage]
    exec --> stdoutOut[Stdout_Result]
    storage --> dataFiles[data_Files]
```



핵심 메시지:

- SQL 입력이 단계적으로 변환되어 실행된다.
- 사용자 출력(`stdout`)과 파일 반영(`data/*`)이 동시에 존재한다.

---

## 3) CLI 계층 동작 (A/B 비교)

### A조 (CLI)

```mermaid
flowchart LR
    a1["ArgCheck"] --> a2["Read & Split"]
    a2 --> a3["Parse"]
    a3 --> a4["Execute"]
    a3 --> a5["Exit 2"]
    a4 --> a6["Exit 3"]
    a4 --> a7["Exit 0"]
```

### B조 (CLI)

```mermaid
flowchart LR
    b1["ArgCheck"] --> b2["Read & Split"]
    b2 --> b3["Tokenize/Parse/Execute"]
    b3 --> b4["Exit 1"]
    b3 --> b5["Exit 0"]
```



핵심 메시지:

- A조는 CLI에서 오류를 `1/2/3`으로 세분화한다.
- B조는 성공/실패 중심으로 단순하게 관리한다.

---

## 4) Lexer 방식 비교 (A/B 모두)

### A조 (Lexer)

```mermaid
flowchart LR
    a1["Statement"] --> a2["LexerState"]
    a2 --> a3["NextToken"]
    a3 --> a4["Parser"]
```

### B조 (Tokenizer)

```mermaid
flowchart LR
    b1["Statement"] --> b2["BuildTokenList"]
    b2 --> b3["Parser"]
```



핵심 메시지:

- A조: 스트리밍 방식(토큰 1개씩)
- B조: 버퍼링 방식(토큰 목록 전체)

---

## 5) Parser 방식 비교 (A/B 모두)

### A조 (Parser)

```mermaid
flowchart LR
    a1["TokenStream"] --> a2["Parser"]
    a2 --> a3["General Insert/Select AST"]
```

### B조 (Parser)

```mermaid
flowchart LR
    b1["TokenList"] --> b2["Parser"]
    b2 --> b3["Fixed 5-Pattern AST"]
```



핵심 메시지:

- A조: 확장 여지를 둔 파서
- B조: 요구 범위를 좁힌 전용 파서

---

## 6) Executor + Storage 역할 (A/B 비교)

### A조 (Executor + Storage)

```mermaid
flowchart LR
    a1["AST"] --> a2["Executor"]
    a2 --> a3["SELECT -> ReadCSV -> stdout"]
    a2 --> a4["INSERT -> AppendCSV -> data"]
```

### B조 (Executor + Storage)

```mermaid
flowchart LR
    b1["AST"] --> b2["Executor"]
    b2 --> b3["SELECT -> Read -> stdout"]
    b2 --> b4["INSERT -> RuleCheck -> Write"]
    b4 --> b5["student.csv + entry_log.bin"]
```



핵심 메시지:

- A조는 실행 경로를 단순하게 유지한다.
- B조는 실행 규칙 검증 로직을 executor에 더 많이 둔다.

---

## 7) 실행 결과(두 경로, A/B 비교)

### A조 (실행 결과)

```mermaid
flowchart LR
    aRun["SQL_실행"] --> aUser["사용자_출력_stdout"]
    aRun --> aData["파일_반영_data"]
    aUser --> aTsv["TSV_조회출력"]
    aData --> aCsv["users_csv"]
```

### B조 (실행 결과)

```mermaid
flowchart LR
    bRun["SQL_실행"] --> bUser["사용자_출력_stdout"]
    bRun --> bData["파일_반영_data"]
    bUser --> bCsvOut["CSV형_조회출력"]
    bData --> bCsv["student_csv"]
    bData --> bBin["entry_log_bin"]
```



핵심 메시지:

- 두 조 모두 화면 출력과 파일 반영이 분리된다.
- A조는 CSV 중심, B조는 CSV+Binary를 함께 다룬다.

