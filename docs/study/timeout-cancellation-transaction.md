# Study — Timeout, Cancellation, Transaction 정리

이 문서는 현재 프로젝트에서 `timeout`, `request cancellation`, `shutdown`, `transaction/rollback` 이 어떤 의미를 가지는지 정리한 학습용 문서다.

목표는 단순히 "현재는 안 됩니다"라고 말하는 것이 아니라, 다음을 설명할 수 있게 만드는 것이다.

- 현재 구현이 실제로 어디까지 보장하는가
- 왜 이 프로젝트에서는 그런 선택을 했는가
- 그 선택이 어떤 문제를 남기는가
- 이후 어떤 순서로 개선할 수 있는가

이 문서는 **학습용 보조 자료**이며, 현재 정본 스펙을 직접 바꾸지 않는다.

관련 정본 및 주차 문서:

- `docs/01-product-planning.md`
- `docs/02-architecture.md`
- `docs/03-api-reference.md`
- `docs/04-development-guide.md`
- `docs/weeks/WEEK8/1. planning.md`
- `docs/weeks/WEEK8/2. architecture.md`
- `docs/weeks/WEEK8/3. contract.md`
- `docs/weeks/WEEK8/issues/W8-06.md`

## 1) 한 줄 요약

현재 구현은 **트랜잭션 기반 rollback 시스템이 아니라**,  
**테이블 단위 락 + timeout 응답 + best-effort cancellation** 구조에 가깝다.

즉:

- 충돌은 줄이지만
- 원자적 commit/rollback 은 보장하지 않고
- timeout 이 나도 내부 작업이 이미 반영되었을 수 있다

## 2) 먼저 큰 전제

프로젝트 정본 문서에서 **트랜잭션은 MVP 범위 밖**이다.

- `docs/01-product-planning.md` 에서 `트랜잭션·동시성`은 포함하지 않는 항목으로 잡혀 있다.

이 말은 "몰라서 안 했다"보다 다음에 가깝다.

- 이번 프로젝트는 SQL 엔진과 API 서버를 끝까지 완주하는 것이 우선이었다.
- CSV 기반 미니 DB에서 완전한 transaction/rollback 까지 넣으면 구조가 크게 복잡해진다.
- 그래서 현재 단계에서는 **동작 가능성, 설명 가능성, 디버깅 가능성**을 우선하고,  
  복잡한 복구 메커니즘은 다음 단계 개선 포인트로 남겨둔 상태다.

## 3) 현재 구현은 실제로 어떻게 동작하나?

### 3.1 timeout 처리

현재 WEEK8 API 서버는 요청마다 deadline 을 계산하고, watcher thread 가 deadline 을 넘긴 요청에 대해 `cancel_token` 을 켠다.

하지만 중요한 점은:

- 이 token 은 "즉시 강제 종료"가 아니다.
- `/query` 핸들러는 token 을 **엔진 실행 전**과 **엔진 실행 후**에만 확인한다.
- 실제 SQL 실행 함수는 token 을 인자로 받지 않고, 중간중간 체크하지도 않는다.

즉 현재 timeout 의 의미는 보통 다음과 같다.

- 클라이언트는 `504`를 받을 수 있다.
- 하지만 내부 SQL 실행은 이미 계속 진행 중이거나 완료되었을 수 있다.

### 3.2 cancellation 처리

현재 cancellation 은 **cooperative cancellation 의 초기 형태**다.

- cancel token 이 켜진다.
- 그러나 엔진 내부 루프가 그 token 을 계속 확인하지 않는다.
- 결과적으로 "취소 신호를 세웠다" 수준이지, "실행을 안전하게 중단했다" 수준은 아니다.

즉 현재 구조는 **응답 취소에 가깝고, 실행 취소는 아니다.**

### 3.3 INSERT 처리

현재 `INSERT` 실행은 대략 다음 순서다.

1. 필요한 경우 WEEK7 auto id 를 준비한다.
2. CSV 파일 끝에 새 행을 append 한다.
3. 데이터 행 수를 다시 읽는다.
4. WEEK7 인덱스를 갱신한다.

이 순서의 의미:

- CSV append 가 먼저 일어난다.
- 그 뒤에 다른 후속 단계가 실패할 수 있다.
- 실패해도 이미 append 된 파일을 되돌리는 rollback 은 없다.

즉 `INSERT` 는 "한 번에 모두 성공하거나 모두 취소"되는 원자적 transaction 이 아니다.

### 3.4 SELECT 처리

`SELECT` 는 읽기 작업이라 rollback 문제는 직접적으로 적다.  
대신 timeout 이 나도 내부 조회는 끝까지 수행될 수 있다는 점이 더 중요하다.

### 3.5 shutdown 처리

shutdown 이 들어오면 서버는 새 요청 수락을 멈춘다.

하지만:

- 이미 실행 중인 작업을 transaction rollback 하지는 않는다.
- 큐에 쌓인 일부 요청은 응답 없이 연결이 닫힐 수 있다.
- 진행 중 작업은 끝까지 가거나, 응답 시점에서 timeout/종료로 처리될 수 있다.

즉 shutdown 역시 "복구 중심"이 아니라 "더 이상 받지 않는 보호 동작"에 가깝다.

## 4) 상황별로 보면 무엇이 보장되고, 무엇이 안 되나?

### A. queue full

- 요청은 아예 실행되지 않는다.
- `503 QUEUE_FULL` 로 즉시 거절된다.
- 데이터 변경은 없다.

이 경우는 가장 안전하다.

### B. 잘못된 JSON 또는 parse error

- 엔진 실행 전 단계에서 실패한다.
- 데이터 변경은 없다.

### C. timeout 이 엔진 실행 전에 걸림

- 바로 `504`가 나간다.
- 데이터 변경은 없다.

### D. timeout 이 엔진 실행 중에 걸림

- 클라이언트는 `504`를 받을 수 있다.
- 하지만 엔진은 이미 작업을 수행하고 있을 수 있다.
- INSERT 라면 CSV 반영이 이미 끝났을 가능성도 있다.

이 구간이 현재 구조의 핵심 한계다.

### E. CSV append 성공 후 후속 단계 실패

- CSV 는 이미 수정됐을 수 있다.
- 인덱스 갱신이 실패하면 CSV 와 인덱스 사이에 불일치가 생길 수 있다.
- rollback 은 없다.

### F. shutdown 중 대기/실행 요청

- 새 요청은 더 받지 않는다.
- 실행 중 작업은 강제 rollback 하지 않는다.
- 큐 대기 요청은 연결 종료로 끝날 수 있다.

## 5) 현재 구현이 가진 문제를 어떻게 설명하면 좋나?

현재 구현의 문제는 단순히 "트랜잭션이 없다"가 아니다.  
더 정확히 말하면 다음 세 가지다.

### 5.1 timeout 의미와 실행 의미가 분리되어 있다

- 현재 `504`는 "응답 deadline 을 넘겼다"는 의미에 가깝다.
- 하지만 사용자는 종종 `504`를 "아무 것도 반영되지 않았다"로 이해한다.
- 이 둘이 다르면 운영과 디버깅에서 혼란이 생긴다.

### 5.2 파일 반영과 인덱스 반영이 원자적이지 않다

- CSV append 후 인덱스 갱신 실패가 가능하다.
- 즉 상태가 "모두 성공"과 "모두 실패" 둘 중 하나로만 끝나지 않는다.

### 5.3 cancellation 이 엔진 내부까지 전파되지 않는다

- 현재 cancel token 은 API 레이어 근처에서만 의미가 있다.
- 엔진, executor, storage 가 취소 가능 구조로 설계되지 않았다.

## 6) 그럼에도 왜 이런 선택을 했는가?

이 질문에는 "제약"과 "의도"를 같이 설명하는 게 좋다.

### 6.1 과제 범위와 구현 우선순위

이번 프로젝트의 우선순위는 다음에 가까웠다.

- SQL 엔진 재사용
- HTTP API 서버 완성
- Thread Pool, queue, timeout, backpressure 설명 가능
- 테스트 가능한 MVP 완주

여기에 진짜 transaction/rollback 까지 넣으려면:

- commit/rollback 경계 정의
- 파일 갱신 원자성
- recovery 전략
- timeout 과 commit 관계
- 인덱스와 저장소 동기화

까지 같이 풀어야 한다.  
이는 현재 과제 범위를 크게 넘어선다.

### 6.2 CSV 저장소의 구조적 한계

현재 저장소는 단순 append 기반 CSV 파일이다.

이 구조에서 rollback 을 안전하게 하려면 최소한 다음 중 하나가 필요하다.

- append 전/후 journal
- temp file + atomic rename
- WAL + recovery

즉 "CSV 끝에 한 줄 붙이기"에 비해 설계 난도가 훨씬 올라간다.

### 6.3 디버깅 가능성과 설명 가능성

초기 단계에서는 다음 장점이 컸다.

- 코드 경로가 단순하다
- 어떤 파일이 바뀌는지 눈으로 보기 쉽다
- 실패 지점을 추적하기 쉽다
- 발표에서 구조를 설명하기 쉽다

즉 현재 구조는 완전무결한 구조라기보다,  
**작동하는 시스템을 먼저 완성하기 위한 의도적 단순화**라고 설명하는 것이 맞다.

## 7) 현재 구조에서 인정해야 하는 한계

발표나 Q&A 에서는 한계를 숨기기보다, 범위를 알고 있다는 점을 보여주는 것이 더 좋다.

현재 인정해야 할 한계:

- timeout 은 응답 deadline 중심이지, 강한 의미의 execution cancel 이 아니다
- rollback 이 없어서 partial write 가능성이 있다
- INSERT 와 index update 가 원자적이지 않다
- shutdown 시 queued request 에 대한 정교한 종료 응답이 없다
- retry 시 중복 반영을 막는 idempotency 설계가 없다

## 8) 어떻게 개선할 수 있나?

중요한 것은 "무조건 transaction 넣겠다"가 아니라,  
**현재 구조에서 현실적으로 어떤 순서로 개선할지**를 설명하는 것이다.

### 8.1 1단계: 의미를 먼저 분명히 하기

가장 먼저 할 일은 구현보다 **계약을 명확히 문서화**하는 것이다.

- `504`가 "응답 deadline 초과"인지 명확히 적기
- timeout 후에도 내부 작업이 완료될 수 있음을 문서화하기
- retryable 의미와 주의사항 명시하기

이 단계는 비용이 낮고 효과가 크다.

### 8.2 2단계: cooperative cancellation 을 엔진까지 전파하기

다음 단계는 cancel token 을 API 레이어에서만 쓰지 않고,  
engine bridge -> sql processor -> executor -> storage 까지 전달하는 것이다.

예시 체크포인트:

- statement 실행 시작 전
- CSV append 직전
- 인덱스 갱신 직전
- 긴 SELECT 루프 중간

효과:

- timeout 반응성이 좋아진다
- "이미 너무 늦은 작업"을 끝까지 수행하지 않을 수 있다

한계:

- 이미 파일 쓰기를 시작한 뒤라면 rollback 문제는 여전히 남는다

즉 이 단계는 cancellation 개선이지, transaction 완성은 아니다.

### 8.3 3단계: single-statement pseudo transaction 도입

현재 구조에서 가장 현실적인 개선은 **INSERT 단건에 대한 의사-트랜잭션**이다.

가능한 방식 A: temp file + rename

- 기존 CSV 를 읽는다
- 새 행이 반영된 임시 파일을 만든다
- 검증이 끝나면 기존 파일과 교체한다

장점:

- commit 시점이 명확하다
- 부분 append 문제를 줄일 수 있다

단점:

- 매번 파일 전체를 다시 써야 해서 비용이 크다

가능한 방식 B: journal 파일 추가

- 본 파일에 쓰기 전에 journal 에 intent 를 남긴다
- 본 파일 반영 후 commit marker 를 기록한다
- 서버 시작 시 미완료 journal 을 보고 복구한다

장점:

- 진짜 transaction 구조에 더 가깝다

단점:

- recovery 설계가 필요하다
- 구현 복잡도가 크게 오른다

### 8.4 4단계: 저장소와 인덱스 commit 경계 통합

현재는 CSV 와 index update 가 분리되어 있다.  
이를 개선하려면 둘을 하나의 commit 단위로 묶어야 한다.

예시:

- 준비 단계: id 할당, 검증, cancel check
- 저장 단계: temp file 또는 journal 반영
- commit 단계: 파일 교체 완료 후 index 갱신
- 실패 시: index 갱신 전까지는 외부에 committed 상태로 보지 않음

또는 더 단순하게:

- index 는 부차 캐시로 보고, 서버 재시작 시 CSV 기반으로 rebuild 가능하게 설계

이렇게 하면 index mismatch 위험을 낮출 수 있다.

### 8.5 5단계: retry 와 idempotency 추가

timeout 이후 클라이언트 재시도가 들어오면 중복 INSERT 가 생길 수 있다.  
이를 줄이려면 요청 식별자를 더 잘 써야 한다.

예시:

- `requestId`를 실제 중복 방지 키로 사용
- 동일 `requestId`의 INSERT 는 한 번만 commit
- 결과를 캐시하거나 dedup 기록 유지

이 단계는 timeout + retry 환경에서 매우 중요하다.

### 8.6 6단계: 오래 걸리는 작업은 비동기 job API 로 분리

아주 긴 작업까지 동기 요청 안에서 처리하면 timeout 과 cancellation 이 계속 어렵다.

따라서:

- 짧은 작업은 동기
- 긴 작업은 비동기 job

으로 분리하는 것이 현실적이다.

그러면:

- timeout 을 응답 deadline 이 아니라 작업 상태 관리로 바꿀 수 있고
- commit 이후 결과 조회 구조도 더 명확해진다

## 9) 현실적인 개선 순서 제안

가장 설득력 있는 답은 "한 번에 다 고치겠다"가 아니라 단계적 로드맵이다.

추천 순서:

1. 문서와 계약에 timeout 의미를 명확히 적는다.
2. cancel token 을 엔진 내부까지 전파한다.
3. INSERT 단건에 temp file 기반 pseudo transaction 을 붙인다.
4. index 와 storage commit 경계를 정리한다.
5. requestId 기반 idempotency 를 추가한다.
6. 장기 작업은 비동기 job API 로 분리한다.

이 순서는 다음 이유로 현실적이다.

- 앞 단계일수록 비용이 낮고 효과가 빠르다
- 뒤 단계일수록 구조적 개선이지만 구현 부담이 크다
- 발표에서도 "우선순위를 알고 있다"는 인상을 준다

## 10) 질문이 들어왔을 때 이렇게 답하면 좋다

### Q1. timeout 이 나면 DB 변경도 자동으로 rollback 되나요?

A. 현재 구현에서는 그렇지 않습니다. 지금 timeout 은 응답 deadline 기준에 가깝고, 엔진 내부 실행을 강하게 취소하거나 rollback 하는 구조는 아닙니다. 그래서 클라이언트는 504를 받더라도 내부 작업은 이미 완료됐을 수 있습니다. 이 점은 한계로 인지하고 있고, 다음 단계로는 cancel token 을 엔진까지 전파하고, INSERT 에는 temp file 또는 journal 기반 pseudo transaction 을 도입하는 방향을 생각하고 있습니다.

### Q2. 그러면 현재 구현은 문제가 있는 것 아닌가요?

A. 맞습니다. 특히 timeout 이후 실제 반영 여부가 불명확할 수 있고, INSERT 이후 index 갱신 실패 시 불일치 가능성이 있습니다. 다만 이 프로젝트는 CSV 기반 SQL 엔진과 API 서버를 완주하는 것이 우선이라, transaction 까지 한 번에 넣기보다 구조를 단순하게 유지했습니다. 대신 어떤 문제가 남는지와 어떻게 개선할지는 명확히 인지하고 있습니다.

### Q3. 왜 처음부터 transaction 을 넣지 않았나요?

A. 현재 저장소가 단순 append 기반 CSV 라서, transaction 을 제대로 하려면 temp file 교체, journal, recovery, index commit 경계까지 같이 설계해야 합니다. 이건 구현 난도가 급격히 올라가는 영역이라 이번 범위에서는 의도적으로 제외하고, 대신 락/timeout/backpressure 로 기본 안정성을 확보하는 방향을 선택했습니다.

### Q4. 가장 먼저 개선한다면 무엇부터 하시겠어요?

A. 가장 먼저는 timeout 의미를 문서에 분명히 적고, cancel token 을 엔진 내부까지 전달하겠습니다. 그다음 INSERT 단건부터 pseudo transaction 을 붙여서 partial write 위험을 줄이는 순서가 현실적이라고 봅니다.

### Q5. 지금 구조의 장점은 전혀 없나요?

A. 있습니다. 구조가 단순해서 디버깅과 발표 설명이 쉽고, CSV 기반 동작을 눈으로 확인하기 좋습니다. 초기 단계에서 작동 가능한 엔진과 API 서버를 빠르게 완성하는 데는 적합한 구조였습니다. 다만 운영 안정성을 높이려면 이후 transaction 과 recovery 계층이 추가되어야 합니다.

## 11) 발표용 짧은 답변 템플릿

다음처럼 답하면 균형이 좋다.

"현재 구현은 transaction rollback 보장 구조는 아닙니다. timeout 은 응답 deadline 중심이라, 504가 나더라도 내부 작업이 이미 반영됐을 수 있습니다. 이건 저희도 한계로 인지하고 있습니다. 다만 이번 단계에서는 CSV 기반 SQL 엔진과 API 서버를 완성하는 것이 우선이었고, transaction 을 제대로 넣으려면 journal, temp file rename, recovery, index commit 경계까지 같이 풀어야 해서 범위를 넘는다고 판단했습니다. 다음 단계로는 cancel token 을 엔진까지 전파하고, INSERT 단건부터 pseudo transaction 을 붙이는 방향으로 개선할 수 있습니다."

## 12) 최종 정리

- 현재 구현은 rollback 중심 구조가 아니다.
- timeout 은 응답 실패와 내부 실행 완료가 분리될 수 있다.
- 이 한계는 인지하고 있으며, 이유 없는 누락이 아니라 범위와 구조를 고려한 의도적 단순화다.
- 개선 방향은 이미 그려져 있고, 문서화 -> cancellation 전파 -> pseudo transaction -> idempotency -> async job 순으로 확장하는 것이 현실적이다.
