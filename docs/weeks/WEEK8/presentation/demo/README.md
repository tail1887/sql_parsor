# WEEK8 Demo Command List (Terminal)

WEEK8 시연을 터미널 기준으로 실행하는 순서입니다.

## 1) 프로젝트 루트 이동

```bash
cd "/Users/tail1/Desktop/krafton-jungle/3. week_mini/6week_mini/mini_DBMS/sql_parsor"
```

## 2) 서버 시작 (빌드 + 데이터 준비 + 기동)

```bash
bash docs/weeks/WEEK8/presentation/demo/00_start_server.command
```

기대 결과:
- `server started: pid=...`
- `curl http://127.0.0.1:8080/health` 응답 확인
- 보수 설정 적용: `workers=2`, `queue=16`, `fixed timeout=3ms`

## 3) 정상 요청 시연

```bash
bash docs/weeks/WEEK8/presentation/demo/01_normal_query.command
```

기대 결과:
- `HTTP 200`
- 응답 JSON에 `ok:true`

## 4) 과부하(Backpressure) 시연

```bash
bash docs/weeks/WEEK8/presentation/demo/02_overload.command
```

기대 결과:
- 결과에 `error_503_ratio > 0`
- `QUEUE_FULL` 계열 오류 관찰

## 5) Timeout/Cancellation 시연

```bash
bash docs/weeks/WEEK8/presentation/demo/03_timeout.command
```

기대 결과:
- 결과에 `error_504_ratio > 0`
- timeout/cancellation 경로 동작 확인

## 6) 서버 종료

```bash
kill "$(cat /tmp/week8_demo_server.pid)"
```

## 참고: 완전 수동 실행 명령

정상 요청:

```bash
curl -i -s -X POST "http://127.0.0.1:8080/query" \
  -H "Content-Type: application/json" \
  -d '{"sql":"SELECT * FROM week8_bench_small;"}'
```

과부하 테스트:

```bash
python3 scripts/week8/bench_client_02_deep.py \
  --scenario burst \
  --requests 5000 \
  --concurrency 512 \
  --port 8080
```

타임아웃 테스트:

```bash
python3 scripts/week8/bench_client_02_deep.py \
  --scenario saturation \
  --requests 2000 \
  --concurrency 256 \
  --port 8080
```
