# Study — Graceful Shutdown 정리

이 문서는 서버 종료 시 자주 나오는 개념인 `graceful shutdown`을 현재 프로젝트 맥락에 맞춰 정리한 학습용 문서다.

목표는 단순히 "종료를 지원한다"라고 말하는 것이 아니라, 다음을 설명할 수 있게 만드는 것이다.

- graceful shutdown 이 정확히 무엇인가
- 왜 필요한가
- grace period 는 왜 필요한가
- shutdown 시 queue 를 어디까지 비워야 하는가
- 현재 프로젝트에서는 어디까지 구현되어 있는가
- 현재 선택의 장단점과 개선 방향은 무엇인가

이 문서는 **학습용 보조 자료**이며, 현재 정본 스펙을 직접 바꾸지 않는다.

관련 문서:

- `docs/01-product-planning.md`
- `docs/02-architecture.md`
- `docs/04-development-guide.md`
- `docs/weeks/WEEK8/1. planning.md`
- `docs/weeks/WEEK8/2. architecture.md`
- `docs/weeks/WEEK8/issues/W8-01.md`
- `docs/weeks/WEEK8/issues/W8-06.md`

## 1) 한 줄 요약

`graceful shutdown`은 서버를 끌 때 **그냥 즉시 죽이는 것이 아니라**,  
**새 요청은 막고, 이미 처리 중인 요청은 가능한 범위 안에서 마무리하고, 자원을 정리한 뒤 종료하는 방식**이다.

현재 프로젝트는 이 방향의 **기본 뼈대는 구현되어 있지만**,  
`grace period`, `drain 정책`, `queued request 응답 정책`까지 갖춘 완전한 운영형 shutdown 은 아니다.

## 2) graceful shutdown 이란 무엇인가?

서버 종료 방식은 크게 두 가지로 나눠 생각할 수 있다.

### 2.1 abrupt shutdown

갑자기 종료하는 방식이다.

- 프로세스를 바로 끈다
- 연결이 중간에 끊길 수 있다
- 응답이 반쯤 나가다 멈출 수 있다
- 처리 중 작업의 상태가 애매해질 수 있다

### 2.2 graceful shutdown

정리 단계를 거쳐 종료하는 방식이다.

보통 흐름은 다음과 같다.

1. 종료 신호를 받는다
2. 새 요청 수락을 중단한다
3. 이미 처리 중인 요청은 가능한 한 마무리한다
4. 워커, 큐, 소켓, 내부 자원을 정리한다
5. 프로세스를 종료한다

즉 graceful shutdown 의 본질은  
**종료 중에도 시스템 상태를 예측 가능하게 유지하는 것**이다.

## 3) 왜 필요한가?

종료 경로는 생각보다 자주 발생한다.

- 로컬 개발 중 `Ctrl+C`
- 배포 시 재시작
- 서버 교체
- 장애 대응 중 수동 재기동

이때 graceful shutdown 이 없으면 다음 문제가 생긴다.

- 처리 중 요청이 중간에 끊긴다
- 클라이언트는 네트워크 에러만 보고 실제 반영 여부를 모른다
- 스레드/소켓 정리가 깔끔하지 않다
- 테스트와 디버깅이 어려워진다

즉 graceful shutdown 은 "멋진 추가 기능"이 아니라,  
**운영 가능한 서버를 위한 기본 안전장치**에 가깝다.

## 4) grace period 는 무엇인가?

`grace period`는 shutdown 을 시작한 뒤 서버가 **정리할 시간을 제한적으로 더 받는 구간**이다.

예를 들어:

- 종료 신호를 받았다
- 새 요청은 더 이상 안 받는다
- 하지만 이미 처리 중인 요청은 5초 동안은 마무리 기회를 준다
- 5초가 지나도 안 끝나면 남은 요청은 포기하고 종료한다

즉 grace period 는 다음 두 문제 사이의 균형 장치다.

- 너무 빨리 끄면: 진행 중 요청이 잘린다
- 무한정 기다리면: 종료가 끝나지 않는다

### 4.1 grace period 가 없으면 어떤 문제가 생기나?

grace period 가 없으면 보통 둘 중 하나가 된다.

#### A. 즉시 종료

- 요청이 중간에 끊길 수 있다
- 응답이 반쯤 나갈 수 있다
- 내부 상태가 더 불명확해질 수 있다

#### B. 무기한 대기

- 처리 중인 요청이 길어지면 서버가 안 죽는다
- 배포나 재기동이 지연된다
- 결국 운영자가 강제 종료를 하게 된다

즉 grace period 는  
**"예쁘게 끝낼 기회는 주되, 영원히 기다리지는 않는다"**는 원칙을 구현하는 장치다.

## 5) shutdown 시 queue 는 어디까지 처리해야 하나?

이건 항상 "전부 처리"가 정답은 아니다.

### 5.1 in-flight request 와 queued request 는 다르다

- `in-flight request`: 이미 worker 가 잡아서 처리 중인 요청
- `queued request`: 아직 worker 가 가져가지 않은 대기 요청

실무적으로는 이 둘을 다르게 다루는 경우가 많다.

### 5.2 흔한 shutdown 정책

#### 정책 A. 실행 중 요청만 마무리

- 새 요청은 차단
- 이미 처리 중인 요청만 grace period 동안 마무리
- queue 에 대기 중인 요청은 포기

장점:

- shutdown 시간이 예측 가능하다
- 구현이 단순하다

단점:

- queue 에 있던 일부 요청은 처리되지 않는다

#### 정책 B. queue 도 가능한 만큼 drain

- 새 요청은 차단
- in-flight 뿐 아니라 queued 요청도 grace period 내에서 처리 시도
- 시간 초과 시 남은 요청 포기

장점:

- 종료 직전 처리량을 더 확보할 수 있다

단점:

- shutdown 시간이 길어질 수 있다
- queue 가 길면 종료가 느려진다

#### 정책 C. job queue 는 재큐잉

이건 HTTP request queue 보다 background job queue 에 가깝다.

- 현재 worker 는 작업을 소유하지 않고
- 종료 시 미완료 작업을 다른 worker 가 다시 가져가게 함

장점:

- 유실을 줄일 수 있다

단점:

- 지금 프로젝트처럼 메모리 내부 queue 인 경우 바로 적용하기 어렵다

### 5.3 결론

HTTP 서버 내부 queue 기준으로는  
shutdown 때 **무조건 다 처리하는 것보다**,  
**어디까지 처리하고 어디서 포기할지 정책을 정하는 것**이 더 중요하다.

## 6) 현재 프로젝트에서는 어떻게 구현되어 있나?

현재 WEEK8 API 서버는 기본적으로 `pool` 모드를 기본값으로 둔다.

종료 흐름은 대략 다음과 같다.

1. `SIGINT`/`SIGTERM`을 받는다
2. `week8_api_server_request_stop()`를 호출한다
3. `stop_requested = 1`로 설정한다
4. 대기 중인 worker 와 watcher 를 깨운다
5. listen socket 을 `shutdown/close` 해서 새 연결 수락을 막는다
6. `accept` 루프가 빠져나온다
7. worker 들은 현재 처리 중인 요청을 끝낸 뒤 루프를 빠져나간다
8. `destroy_worker_pool()`에서 watcher 와 worker 를 `join` 하며 정리한다

즉 현재 구현의 의도는 다음과 같다.

- 새 요청은 더 이상 받지 않는다
- 이미 실행 중인 작업은 가능한 한 끝낼 기회를 준다
- 스레드와 소켓을 정리한 뒤 종료한다

## 7) 현재 구현이 graceful 한 부분

현재 프로젝트에는 다음 요소가 들어가 있다.

### 7.1 새 요청 수락 중단

listen socket 을 닫아 `accept` 루프를 멈춘다.  
즉 shutdown 이 시작되면 새 연결은 더 이상 정상 수락되지 않는다.

### 7.2 대기 중인 worker/watcher 깨우기

조건변수를 broadcast 해서 sleep 중이던 스레드들이 종료 플래그를 볼 수 있게 한다.

### 7.3 실행 중 worker 정리

worker thread 는 현재 잡은 `handle_client()` 호출을 끝낸 뒤 루프를 빠져나간다.  
그 후 `join`으로 정리한다.

### 7.4 종료 경로가 테스트 가능함

테스트에서도 `week8_api_server_request_stop()`를 호출해 종료 경로를 검증한다.  
즉 종료 동작이 설계상 존재만 하는 것이 아니라 실제 코드 경로로 포함돼 있다.

## 8) 현재 구현이 아직 완전하지 않은 부분

현재 구현은 "graceful shutdown 의 1차 버전"으로 보는 것이 정확하다.

### 8.1 grace period 가 없다

가장 큰 한계다.

- 현재는 worker 를 `join` 하며 기다리는데
- 얼마나 오래 기다릴지 제한이 없다

즉 shutdown 이  
**너무 빠르게 끊기지는 않지만, 반대로 너무 오래 붙잡힐 수도 있다.**

### 8.2 queued request 를 끝까지 drain 하지 않는다

종료 시 `client_queue` 에 남은 요청은 최종 정리 단계에서 소켓을 닫아버린다.

즉:

- in-flight 요청은 어느 정도 마무리 기회를 받지만
- queue 에만 있던 요청은 처리되지 않고 종료될 수 있다

### 8.3 queued request 에 정교한 종료 응답이 없다

남은 queue 항목에 대해:

- 명시적인 `503 shutting down`
- 또는 재시도 힌트

같은 응답을 일관되게 보내지 않고, 연결 종료 중심으로 처리된다.

### 8.4 per-request 모드는 더 덜 graceful 하다

`per_request` 모드에서는 요청별 스레드를 `detach` 해서 관리한다.  
즉 pool 모드처럼 명시적으로 `join` 하며 수명 관리를 하지 않는다.

따라서 현재 graceful shutdown 논의는 **기본 pool 모드 기준**으로 이해하는 것이 맞다.

## 9) graceful shutdown 이 있을 때와 없을 때 차이

### 있을 때

- 새 요청 유입을 멈출 수 있다
- 실행 중 요청을 어느 정도 마무리할 수 있다
- 워커/소켓/조건변수를 정리할 수 있다
- 종료 경로를 테스트하고 설명할 수 있다

### 없을 때

- 종료 시점에 연결이 갑자기 끊길 수 있다
- 처리 중 응답이 반쯤 나가다 멈출 수 있다
- 스레드 정리가 불명확해진다
- 종료 중 장애가 생겨도 디버깅이 어렵다

즉 graceful shutdown 의 가치는  
**정상 경로만이 아니라 종료 경로도 설계했다**는 데 있다.

## 10) queue 크기는 실무적으로 얼마나 잡나?

정답은 없다. 중요한 것은 "크게 잡기"가 아니라  
**허용 가능한 대기시간과 처리시간을 기준으로 bounded 하게 잡는 것**이다.

### 10.1 queue 가 너무 크면 왜 문제인가?

- 메모리 점유가 커진다
- 대기 시간이 길어진다
- tail latency 가 악화된다
- shutdown 시 drain 시간이 길어진다

즉 queue 가 크다고 좋은 게 아니라,  
**문제를 뒤로 밀어놓는 효과**만 생길 수 있다.

### 10.2 queue 가 너무 작으면 왜 문제인가?

- 짧은 burst 에도 바로 `QUEUE_FULL`이 난다
- 성공률이 떨어질 수 있다

즉 queue 는  
**대기 허용량과 실패 정책 사이의 균형값**이다.

### 10.3 감으로 잡는 기준

실무에서는 보통 수십에서 수백 정도의 bounded queue 가 흔하다.  
하지만 더 중요한 건 절대 숫자보다 다음 식이다.

`queue_capacity ≈ worker_count × 허용 가능한 최대 대기시간 / 평균 처리시간`

예:

- worker 4개
- 평균 처리시간 100ms
- 허용 가능한 최대 대기시간 500ms

이면 대략:

- `4 × 0.5 / 0.1 = 20`

정도의 queue 가 출발점이 될 수 있다.

즉 queue 크기는  
**메모리 기준보다 사용자 대기시간 기준으로 잡는 것이 더 실무적**이다.

## 11) 현재 프로젝트에서는 왜 이런 shutdown 구조를 선택했나?

이 질문에는 "한계"와 "의도"를 같이 설명하는 것이 좋다.

### 11.1 구현 우선순위

이번 프로젝트의 우선순위는 다음에 가까웠다.

- tiny HTTP 서버 완성
- Thread Pool 과 bounded queue 연결
- SQL 엔진 재사용
- timeout/backpressure 포함
- 설명 가능한 MVP 완주

여기에 완전한 운영형 graceful shutdown 까지 넣으려면:

- grace period
- drain 정책
- queued request 응답 정책
- in-flight tracking 세분화
- per-request mode 수명 관리

까지 함께 풀어야 한다.

즉 현재 구조는  
**종료 경로를 아예 비워두지 않고, 핵심만 먼저 넣은 1차 버전**이라고 보는 게 맞다.

### 11.2 단순성과 디버깅 가능성

현재 구조의 장점도 분명하다.

- 종료 흐름이 단순하다
- 새 요청 차단과 worker 정리 순서가 명확하다
- 테스트에 넣기 쉽다
- 발표에서 설명하기 쉽다

즉 현재 선택은  
"운영형 완성본"보다는 **작동 가능한 shutdown skeleton**을 우선한 것이다.

## 12) 어떻게 개선할 수 있나?

실무적으로는 한 번에 다 고치기보다 단계적으로 가는 것이 현실적이다.

### 12.1 1단계: shutdown 의미 명확화

먼저 문서와 계약에서 다음을 분명히 해야 한다.

- shutdown 시작 후 새 요청은 받지 않는다
- in-flight 요청은 최대 얼마까지 기다린다
- queued request 는 처리할지 포기할지
- 포기 시 어떤 응답 또는 종료를 보낼지

이 단계는 비용이 낮고 효과가 크다.

### 12.2 2단계: grace period 추가

예:

- shutdown 시작
- 최대 3초 또는 5초 대기
- 그 안에 끝난 in-flight 요청은 정상 종료
- 시간 초과 시 남은 요청은 종료

이 단계가 들어가면 shutdown 시간이 예측 가능해진다.

### 12.3 3단계: in-flight 와 queued request 분리 관리 강화

현재도 어느 정도 구분은 있지만, 정책적으로 더 분명히 할 수 있다.

예:

- in-flight 요청은 grace period 동안 유지
- queued request 는 즉시 `503 server shutting down`으로 응답

이렇게 하면 클라이언트가 재시도 전략을 세우기 쉬워진다.

### 12.4 4단계: queued request 종료 응답 개선

연결을 그냥 닫는 대신:

- JSON error envelope
- `retryable=true`
- `server shutting down`

같은 명시적 응답을 줄 수 있다.

### 12.5 5단계: per-request 모드 종료 개선

`detach` 대신 추적 가능한 thread registry 를 두거나,  
기본 운영 모드를 pool 모드로 고정하는 것이 더 안전하다.

### 12.6 6단계: 장기 요청은 비동기 job 으로 분리

shutdown 이 특히 어려워지는 이유는 "오래 걸리는 동기 요청" 때문이다.

따라서:

- 짧은 요청은 동기
- 긴 작업은 비동기 job

으로 분리하면 graceful shutdown 도 더 단순해진다.

## 13) 현실적인 개선 순서 제안

추천 순서는 다음과 같다.

1. shutdown 정책을 문서로 고정한다.
2. grace period 를 도입한다.
3. in-flight / queued request 처리 정책을 분리한다.
4. queued request 에 명시적 종료 응답을 준다.
5. per-request 모드 종료 관리를 강화한다.
6. 장기 요청은 비동기 job 으로 분리한다.

이 순서가 좋은 이유:

- 앞 단계일수록 구현 비용이 낮다
- shutdown 시간 예측 가능성을 빠르게 높일 수 있다
- 발표에서도 "문제를 알고 있고 현실적인 우선순위를 갖고 있다"는 인상을 준다

## 14) 질문이 들어왔을 때 이렇게 답하면 좋다

### Q1. graceful shutdown 이 정확히 뭐예요?

A. 서버를 끌 때 즉시 죽이는 게 아니라, 새 요청은 막고 이미 처리 중인 요청은 가능한 범위에서 마무리한 뒤 자원을 정리하고 종료하는 방식입니다.

### Q2. grace period 는 왜 필요한가요?

A. 종료를 너무 빨리 하면 진행 중 요청이 잘리고, 무기한 기다리면 서버가 안 죽을 수 있습니다. grace period 는 정리할 기회는 주되 영원히 기다리지는 않게 해주는 시간 제한입니다.

### Q3. shutdown 할 때 queue 요청은 원래 다 처리하나요?

A. 항상 그렇지는 않습니다. HTTP 서버 내부 queue 는 보통 in-flight 요청만 우선 마무리하고, queued request 는 정책적으로 포기하거나 제한된 시간 내에서만 처리하는 경우가 많습니다.

### Q4. 현재 프로젝트는 graceful shutdown 이 구현돼 있나요?

A. 기본 pool 모드 기준으로는 새 요청 수락 중단, worker/watcher wake-up, worker join 까지는 구현돼 있습니다. 다만 grace period 가 없고 queued request 를 끝까지 drain 하지 않기 때문에 운영형 완성본이라기보다 1차 버전이라고 보는 것이 맞습니다.

### Q5. 왜 처음부터 완전하게 안 만들었나요?

A. 이번 단계에서는 tiny HTTP 서버, thread pool, timeout/backpressure, SQL 엔진 연동을 끝까지 완주하는 것이 우선이었습니다. graceful shutdown 도 핵심 뼈대는 먼저 넣되, grace period 와 drain 정책 같은 운영형 요소는 다음 단계 개선 포인트로 남겼습니다.

### Q6. 가장 먼저 개선한다면 무엇부터 하시겠어요?

A. shutdown 정책을 문서로 먼저 고정하고, 그다음 grace period 를 넣겠습니다. 이후 in-flight 와 queued request 를 다르게 처리하는 정책을 추가하는 것이 가장 효과적입니다.

## 15) 발표용 짧은 답변 템플릿

다음처럼 답하면 균형이 좋다.

"graceful shutdown 은 서버를 끌 때 새 요청은 막고, 이미 처리 중인 요청은 가능한 범위 안에서 마무리한 뒤 자원을 정리하고 종료하는 방식입니다. 저희 프로젝트는 기본 pool 모드 기준으로 이 흐름의 뼈대는 구현돼 있습니다. 다만 아직 grace period 와 queued request drain 정책은 완성되지 않아서, 운영형 완성본이라기보다 1차 버전으로 보는 게 맞습니다. 다음 단계로는 grace period 를 넣고, in-flight 요청과 queued request 를 분리해 종료 정책을 더 명확히 할 수 있습니다."

## 16) 최종 정리

- graceful shutdown 은 종료 중에도 상태를 예측 가능하게 유지하는 설계다
- grace period 는 너무 빨리 끊는 것과 무기한 대기하는 것 사이의 균형 장치다
- shutdown 시 queue 를 무조건 다 처리하는 것이 정답은 아니다
- 현재 프로젝트는 새 요청 차단과 worker 정리까지는 구현했지만, grace period 와 drain 정책은 아직 없다
- 이것은 한계를 모르는 상태가 아니라, MVP 완주를 우선한 의도적 단순화다
