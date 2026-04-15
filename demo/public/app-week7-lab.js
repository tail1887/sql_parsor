/**
 * WEEK7 러닝 가이드 실습 페이지
 * 각 단계를 실행 가능한 액션으로 분할해
 * - 버튼 실행
 * - 확인 포인트 제시
 * - 간단 자동 체크
 * 를 한 화면에서 수행한다.
 */
const STEPS = [
  {
    id: 1,
    title: "1단계 — B+ 트리 단독 모듈",
    summary: "SQL/CSV 의존 없이 트리 자체 테스트를 통과시키는 단계",
    sequenceGuide: [
      "A(검색): find_leaf로 대상 리프를 찾고 hit/miss를 반환",
      "B(삽입-여유): leaf_insert_sorted로 정렬 유지 삽입",
      "C(삽입-split): leaf_split_insert 후 insert_into_parent로 부모 반영"
    ],
    studyPoints: [
      "split 후에도 정렬/부모 연결 불변식 유지",
      "insert와 insert_or_replace의 중복 키 정책 분리"
    ],
    selfCheck: [
      "test_bplus_tree가 통과하는가?",
      "연속 split 시에도 miss/hit가 정상인가?"
    ],
    offline: true,
    panels: ["result"],
    actions: [
      {
        label: "빌드: test_bplus_tree",
        type: "command",
        command: "cmake --build build-ninja --target test_bplus_tree --clean-first",
        checks: ["exit code 0", "stderr에 컴파일 에러 없음"],
        expected: [{ type: "exitCodeEquals", value: 0 }]
      },
      {
        label: "실행: test_bplus_tree",
        type: "command",
        command: "ctest --test-dir build-ninja -R test_bplus_tree --output-on-failure -V",
        checks: ["exit code 0", "테스트 실행 로그 출력 확인"],
        expected: [
          { type: "exitCodeEquals", value: 0 },
          { type: "stdoutNotContains", value: "FAIL" }
        ]
      }
    ]
  },
  {
    id: 2,
    title: "2단계 — 자동 id + row_ref",
    summary: "ensure/load 및 자동 id, row_ref 규칙 확인",
    sequenceGuide: [
      "A(초기 로드): CSV 전체를 읽어 (id -> row_ref) 인덱스 재구축",
      "B(INSERT 준비): next_id를 첫 컬럼에 채워 실제 append 값 준비",
      "C(성공 후 동기화): row_ref=total-1 계산 후 인덱스 반영"
    ],
    studyPoints: ["row_ref는 헤더 제외 0-based", "CSV와 인덱스 상태 동기화"],
    selfCheck: ["INSERT 후 CSV diff에 새 행 추가", "stdout에서 id 부여 확인"],
    offline: false,
    panels: ["tokens", "ast", "executor", "result"],
    actions: [
      {
        label: "자동 id INSERT + 전체 조회",
        type: "sql",
        sql: "INSERT INTO users VALUES ('s2_user', 's2@example.com');\nSELECT * FROM users;\n",
        checks: ["exit code 0", "users.csv diff가 변경되어야 함"],
        expected: [
          { type: "exitCodeEquals", value: 0 },
          { type: "csvChanged", value: true }
        ]
      }
    ]
  },
  {
    id: 3,
    title: "3단계 — INSERT 경로 인덱스 연동",
    summary: "prepare -> append -> on_append_success 순서 확인",
    sequenceGuide: [
      "A(성공 경로): prepare -> append 성공 -> row_ref 계산 -> on_append_success",
      "B(실패 경로): append 실패 시 인덱스 갱신 없이 즉시 종료",
      "C(직후 조회): 방금 반영한 assigned_id가 같은 row_ref를 가리키는지 확인"
    ],
    studyPoints: ["append 성공 후에만 인덱스 반영", "assigned_id와 row_ref 매핑 유지"],
    selfCheck: ["INSERT 후 WHERE id로 동일 행 조회"],
    offline: false,
    panels: ["executor", "result"],
    actions: [
      {
        label: "INSERT 후 전체 확인",
        type: "sql",
        sql: "INSERT INTO users VALUES ('s3_user', 's3@example.com');\nSELECT * FROM users;\n",
        checks: ["exit code 0", "executor 호출 이벤트 확인"],
        expected: [
          { type: "exitCodeEquals", value: 0 },
          { type: "traceHasStep", value: "executor_call" }
        ]
      },
      {
        label: "WHERE id 단건 조회 (id는 필요시 수정)",
        type: "sql",
        sql: "SELECT * FROM users WHERE id = 5;\n",
        checks: ["exit code 0", "header + 단일 행 형태 확인"],
        expected: [{ type: "exitCodeEquals", value: 0 }]
      }
    ]
  },
  {
    id: 4,
    title: "4단계 — 파서/AST WHERE id = 정수",
    summary: "WHERE 문법 제한과 AST 반영 확인",
    sequenceGuide: [
      "A(WHERE 없음): has_where_id_eq=0으로 기본 SELECT 경로",
      "B(WHERE id=int): has_where_id_eq=1, where_id_value 설정",
      "C(미지원 WHERE): parser 단계에서 parse error 반환"
    ],
    studyPoints: ["허용 패턴: WHERE id = <int>", "미지원 패턴은 parser 단계에서 실패"],
    selfCheck: ["성공/실패 쿼리를 모두 실행해 차이 확인"],
    offline: false,
    panels: ["tokens", "ast", "result"],
    actions: [
      {
        label: "성공: WHERE id = 1",
        type: "sql",
        sql: "SELECT id, name FROM users WHERE id = 1;\n",
        checks: ["exit code 0", "AST에 where 필드 반영"],
        expected: [
          { type: "exitCodeEquals", value: 0 },
          { type: "traceHasStep", value: "parser_result" }
        ]
      },
      {
        label: "실패: WHERE id > 1",
        type: "sql",
        sql: "SELECT * FROM users WHERE id > 1;\n",
        checks: ["비정상 종료 코드", "stderr에 parse 관련 메시지"],
        expected: [{ type: "exitCodeNotEquals", value: 0 }]
      }
    ]
  },
  {
    id: 5,
    title: "5단계 — SELECT 실행 분기",
    summary: "WHERE 경로와 기존 경로 분기 검증",
    sequenceGuide: [
      "A(WHERE hit): lookup_row 성공 후 header + 단건 출력",
      "B(WHERE miss): lookup_row miss면 header only 출력",
      "C(비-WHERE): 기존 csv_storage_read_table 경로 유지"
    ],
    studyPoints: ["WHERE id면 인덱스 경로", "그 외 SELECT는 기존 경로"],
    selfCheck: ["전체 조회와 WHERE 조회의 대상 행 데이터 비교"],
    offline: false,
    panels: ["executor", "result"],
    actions: [
      {
        label: "기존 경로: SELECT 전체",
        type: "sql",
        sql: "SELECT id, name, email FROM users;\n",
        checks: ["exit code 0", "결과 테이블 출력 확인"],
        expected: [{ type: "exitCodeEquals", value: 0 }]
      },
      {
        label: "인덱스 경로: WHERE id",
        type: "sql",
        sql: "SELECT id, name, email FROM users WHERE id = 5;\n",
        checks: ["exit code 0", "header + 단건 형태 확인"],
        expected: [{ type: "exitCodeEquals", value: 0 }]
      }
    ]
  },
  {
    id: 6,
    title: "6단계 — 엣지/에러",
    summary: "miss/parse error 경계와 출력 계약 점검",
    sequenceGuide: [
      "A(miss 정상): 없는 id 조회는 실패가 아니라 정상 결과(헤더만)",
      "B(parse error): 미지원 WHERE는 문법 오류로 분리",
      "C(I/O 실패): 파일 읽기/쓰기 실패는 실행 오류로 분리"
    ],
    studyPoints: ["없는 id는 header only", "parse error와 runtime error 구분"],
    selfCheck: ["miss 쿼리와 parse 실패 쿼리 각각 확인"],
    offline: false,
    panels: ["result"],
    actions: [
      {
        label: "miss: 없는 id",
        type: "sql",
        sql: "SELECT * FROM users WHERE id = 999999;\n",
        checks: ["exit code 0", "header만 출력되는지 확인"],
        expected: [{ type: "exitCodeEquals", value: 0 }]
      },
      {
        label: "parse error: WHERE id > 1",
        type: "sql",
        sql: "SELECT * FROM users WHERE id > 1;\n",
        checks: ["비정상 종료 코드", "stderr 메시지 확인"],
        expected: [{ type: "exitCodeNotEquals", value: 0 }]
      }
    ]
  },
  {
    id: 7,
    title: "7단계 — 대량 벤치 + README",
    summary: "bench_bplus 명령으로 성능 수치 확인",
    sequenceGuide: [
      "통합 시퀀스: build -> compare 실행 -> 지표(index/linear) 수집 -> 비율 해석"
    ],
    studyPoints: ["compare 모드 수치 수집", "README 재현 명령 확인"],
    selfCheck: ["bench 실행 결과를 팀 공유 자료로 정리"],
    offline: true,
    panels: ["result"],
    actions: [
      {
        label: "벤치 바이너리 빌드",
        type: "command",
        command: "cmake --build build-ninja --target bench_bplus",
        checks: ["exit code 0"],
        expected: [{ type: "exitCodeEquals", value: 0 }]
      },
      {
        label: "벤치 실행(compare)",
        type: "command",
        command: ".\\build-ninja\\bench_bplus.exe compare 1000000 10000",
        checks: ["index_lookup_sec / linear_scan_sec 출력 확인"],
        expected: [
          { type: "exitCodeEquals", value: 0 },
          { type: "stdoutContains", value: "index_lookup_sec" }
        ]
      }
    ]
  }
];

const stepNav = document.getElementById("stepNav");
const stepDetail = document.getElementById("stepDetail");
const terminalPresetRow = document.getElementById("terminalPresetRow");
const terminalCmdInput = document.getElementById("terminalCmdInput");
const terminalRunBtn = document.getElementById("terminalRunBtn");
const terminalOutput = document.getElementById("terminalOutput");
const step1Visualizer = document.getElementById("step1Visualizer");
const step2Visualizer = document.getElementById("step2Visualizer");
const step3Visualizer = document.getElementById("step3Visualizer");
const step4Visualizer = document.getElementById("step4Visualizer");
const step5Visualizer = document.getElementById("step5Visualizer");
const step6Visualizer = document.getElementById("step6Visualizer");
const step7Visualizer = document.getElementById("step7Visualizer");
const aBeforeTree = document.getElementById("aBeforeTree");
const aAfterResult = document.getElementById("aAfterResult");
const aKeysInput = document.getElementById("aKeysInput");
const aSearchKeyInput = document.getElementById("aSearchKeyInput");
const aRunBtn = document.getElementById("aRunBtn");
const bBeforeTree = document.getElementById("bBeforeTree");
const bAfterTree = document.getElementById("bAfterTree");
const bTrace = document.getElementById("bTrace");
const bKeysInput = document.getElementById("bKeysInput");
const bInsertKeyInput = document.getElementById("bInsertKeyInput");
const bRunBtn = document.getElementById("bRunBtn");
const cBeforeTree = document.getElementById("cBeforeTree");
const cAfterTree = document.getElementById("cAfterTree");
const cTrace = document.getElementById("cTrace");
const cKeysInput = document.getElementById("cKeysInput");
const cInsertKeyInput = document.getElementById("cInsertKeyInput");
const cRunBtn = document.getElementById("cRunBtn");
const s2aIdsInput = document.getElementById("s2aIdsInput");
const s2aRunBtn = document.getElementById("s2aRunBtn");
const s2aBefore = document.getElementById("s2aBefore");
const s2aAfter = document.getElementById("s2aAfter");
const s2bNextIdInput = document.getElementById("s2bNextIdInput");
const s2bValuesInput = document.getElementById("s2bValuesInput");
const s2bRunBtn = document.getElementById("s2bRunBtn");
const s2bBefore = document.getElementById("s2bBefore");
const s2bAfter = document.getElementById("s2bAfter");
const s2cTotalRowsInput = document.getElementById("s2cTotalRowsInput");
const s2cAssignedIdInput = document.getElementById("s2cAssignedIdInput");
const s2cRunBtn = document.getElementById("s2cRunBtn");
const s2cBefore = document.getElementById("s2cBefore");
const s2cAfter = document.getElementById("s2cAfter");
const s3aAssignedIdInput = document.getElementById("s3aAssignedIdInput");
const s3aTotalRowsInput = document.getElementById("s3aTotalRowsInput");
const s3aRunBtn = document.getElementById("s3aRunBtn");
const s3aBefore = document.getElementById("s3aBefore");
const s3aAfter = document.getElementById("s3aAfter");
const s3bReasonInput = document.getElementById("s3bReasonInput");
const s3bRunBtn = document.getElementById("s3bRunBtn");
const s3bBefore = document.getElementById("s3bBefore");
const s3bAfter = document.getElementById("s3bAfter");
const s3cAssignedIdInput = document.getElementById("s3cAssignedIdInput");
const s3cRowRefInput = document.getElementById("s3cRowRefInput");
const s3cRunBtn = document.getElementById("s3cRunBtn");
const s3cBefore = document.getElementById("s3cBefore");
const s3cAfter = document.getElementById("s3cAfter");
const s4aSqlInput = document.getElementById("s4aSqlInput");
const s4aRunBtn = document.getElementById("s4aRunBtn");
const s4aBefore = document.getElementById("s4aBefore");
const s4aAfter = document.getElementById("s4aAfter");
const s4bSqlInput = document.getElementById("s4bSqlInput");
const s4bRunBtn = document.getElementById("s4bRunBtn");
const s4bBefore = document.getElementById("s4bBefore");
const s4bAfter = document.getElementById("s4bAfter");
const s4cSqlInput = document.getElementById("s4cSqlInput");
const s4cRunBtn = document.getElementById("s4cRunBtn");
const s4cBefore = document.getElementById("s4cBefore");
const s4cAfter = document.getElementById("s4cAfter");
const s5aIdInput = document.getElementById("s5aIdInput");
const s5aRowRefInput = document.getElementById("s5aRowRefInput");
const s5aRunBtn = document.getElementById("s5aRunBtn");
const s5aBefore = document.getElementById("s5aBefore");
const s5aAfter = document.getElementById("s5aAfter");
const s5bIdInput = document.getElementById("s5bIdInput");
const s5bRunBtn = document.getElementById("s5bRunBtn");
const s5bBefore = document.getElementById("s5bBefore");
const s5bAfter = document.getElementById("s5bAfter");
const s5cSqlInput = document.getElementById("s5cSqlInput");
const s5cRunBtn = document.getElementById("s5cRunBtn");
const s5cBefore = document.getElementById("s5cBefore");
const s5cAfter = document.getElementById("s5cAfter");
const s6aIdInput = document.getElementById("s6aIdInput");
const s6aRunBtn = document.getElementById("s6aRunBtn");
const s6aBefore = document.getElementById("s6aBefore");
const s6aAfter = document.getElementById("s6aAfter");
const s6bSqlInput = document.getElementById("s6bSqlInput");
const s6bRunBtn = document.getElementById("s6bRunBtn");
const s6bBefore = document.getElementById("s6bBefore");
const s6bAfter = document.getElementById("s6bAfter");
const s6cTableInput = document.getElementById("s6cTableInput");
const s6cRunBtn = document.getElementById("s6cRunBtn");
const s6cBefore = document.getElementById("s6cBefore");
const s6cAfter = document.getElementById("s6cAfter");
const s7nInput = document.getElementById("s7nInput");
const s7kInput = document.getElementById("s7kInput");
const s7RunBtn = document.getElementById("s7RunBtn");
const s7Before = document.getElementById("s7Before");
const s7After = document.getElementById("s7After");
const sqlRunSection = document.getElementById("sqlRunSection");
const terminalRunnerSection = document.getElementById("terminalRunnerSection");
const terminalGuideSection = document.getElementById("terminalGuideSection");
const terminalCmdList = document.getElementById("terminalCmdList");
const presetRow = document.getElementById("presetRow");
const runHint = document.getElementById("runHint");
const sqlInput = document.getElementById("sqlInput");
const runBtn = document.getElementById("runBtn");
const genSqlCmdBtn = document.getElementById("genSqlCmdBtn");
const copySqlCmdBtn = document.getElementById("copySqlCmdBtn");
const sqlCmdBox = document.getElementById("sqlCmdBox");
const sqlCmdView = document.getElementById("sqlCmdView");

const tokenBody = document.querySelector("#tokenTable tbody");
const astView = document.getElementById("astView");
const executorList = document.getElementById("executorList");
const stdoutView = document.getElementById("stdoutView");
const stderrView = document.getElementById("stderrView");
const exitCode = document.getElementById("exitCode");
const diffView = document.getElementById("diffView");

const panelEls = {
  tokens: document.getElementById("panelTokens"),
  ast: document.getElementById("panelAst"),
  executor: document.getElementById("panelExecutor"),
  result: document.getElementById("panelResult")
};

let activeStep = null;
let selectedTerminalAction = null;

function esc(s) {
  return String(s).replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
}

function getUsersCsv(snapshot) {
  return snapshot["users.csv"] || "";
}

function simpleLineDiff(before, after) {
  const a = before.split(/\r?\n/);
  const b = after.split(/\r?\n/);
  const out = [];
  const max = Math.max(a.length, b.length);
  for (let i = 0; i < max; i++) {
    const va = a[i] ?? "";
    const vb = b[i] ?? "";
    if (va === vb) out.push(`  ${va}`);
    else {
      if (va !== "") out.push(`- ${va}`);
      if (vb !== "") out.push(`+ ${vb}`);
    }
  }
  return out.join("\n");
}

function resetOutput() {
  tokenBody.innerHTML = "";
  astView.textContent = "(AST 없음)";
  executorList.innerHTML = "";
  stdoutView.textContent = "";
  stderrView.textContent = "";
  exitCode.textContent = "exit code: —";
  diffView.textContent = "";
}

function buildTraceCommand(sql) {
  const safeSql = String(sql || "").replace(/'/g, "''");
  return [
    `$sql = @'`,
    safeSql.trimEnd(),
    `'@`,
    `$sqlPath = ".\\week7_action.sql"`,
    `$tracePath = ".\\week7_action_trace.jsonl"`,
    `Set-Content -Path $sqlPath -Value $sql -Encoding UTF8`,
    `.\\build-gcc\\sql_processor_trace.exe $sqlPath $tracePath`,
    `Get-Content $tracePath`
  ].join("\n");
}

function applyPanelVisibility(step) {
  const visible = new Set(step.panels || ["tokens", "ast", "executor", "result"]);
  Object.entries(panelEls).forEach(([key, el]) => {
    if (!el) return;
    el.hidden = !visible.has(key);
  });
}

function renderStepDetail(step) {
  const seqHtml = (step.sequenceGuide && step.sequenceGuide.length)
    ? `
      <h3>시퀀스 요약</h3>
      <ul class="seq-guide-list">
        ${step.sequenceGuide.map((s) => `<li>${esc(s)}</li>`).join("")}
      </ul>
    `
    : "";
  stepDetail.innerHTML = `
    <h2>${esc(step.title)}</h2>
    <p>${esc(step.summary || "")}</p>
    ${seqHtml}
  `;

  if (step.offline) {
    sqlRunSection.hidden = true;
    terminalGuideSection.hidden = false;
    terminalCmdList.innerHTML = (step.terminalCommands || [])
      .map((cmd) => `<li><code>${esc(cmd)}</code></li>`)
      .join("");
  } else {
    sqlRunSection.hidden = false;
    terminalGuideSection.hidden = true;
    runHint.textContent = "프리셋을 고르면 SQL이 자동 입력됩니다. 실행하거나, 아래 명령어 생성으로 터미널에서 바로 돌릴 수 있습니다.";
    presetRow.innerHTML = "";
    (step.sqlPresets || []).forEach((p) => {
      const b = document.createElement("button");
      b.type = "button";
      b.className = "preset-btn";
      b.textContent = p.label;
      b.addEventListener("click", () => {
        sqlInput.value = p.sql;
        sqlCmdBox.hidden = true;
      });
      presetRow.appendChild(b);
    });
    sqlInput.value = step.sqlPresets && step.sqlPresets.length ? step.sqlPresets[0].sql : "";
  }

  if (Number(step.id) === 1) {
    renderStep1ABC();
    step1Visualizer.hidden = false;
    step2Visualizer.hidden = true;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else if (Number(step.id) === 2) {
    renderStep2ABC();
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = false;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else if (Number(step.id) === 3) {
    renderStep3ABC();
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = true;
    step3Visualizer.hidden = false;
    step4Visualizer.hidden = true;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else if (Number(step.id) === 4) {
    renderStep4ABC();
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = true;
    step3Visualizer.hidden = true;
    step4Visualizer.hidden = false;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else if (Number(step.id) === 5) {
    renderStep5ABC();
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = true;
    step3Visualizer.hidden = true;
    step4Visualizer.hidden = true;
    step5Visualizer.hidden = false;
    step6Visualizer.hidden = true;
    step7Visualizer.hidden = true;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else if (Number(step.id) === 6) {
    renderStep6ABC();
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = true;
    step3Visualizer.hidden = true;
    step4Visualizer.hidden = true;
    step5Visualizer.hidden = true;
    step6Visualizer.hidden = false;
    step7Visualizer.hidden = true;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else if (Number(step.id) === 7) {
    renderStep7Unified();
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = true;
    step3Visualizer.hidden = true;
    step4Visualizer.hidden = true;
    step5Visualizer.hidden = true;
    step6Visualizer.hidden = true;
    step7Visualizer.hidden = false;
    sqlRunSection.hidden = true;
    terminalRunnerSection.hidden = true;
    terminalGuideSection.hidden = true;
    Object.values(panelEls).forEach((el) => { if (el) el.hidden = true; });
  } else {
    step1Visualizer.hidden = true;
    step2Visualizer.hidden = true;
    step3Visualizer.hidden = true;
    step4Visualizer.hidden = true;
    step5Visualizer.hidden = true;
    step6Visualizer.hidden = true;
    step7Visualizer.hidden = true;
    terminalRunnerSection.hidden = false;
  }

  renderTerminalPresets(step);
  const visualOnly = Number(step.id) === 1 || Number(step.id) === 2 || Number(step.id) === 3 || Number(step.id) === 4 || Number(step.id) === 5 || Number(step.id) === 6 || Number(step.id) === 7;
  if (!visualOnly) {
    applyPanelVisibility(step);
  }
  if (Number(step.id) !== 2) {
    step2Visualizer.hidden = true;
  }
  if (Number(step.id) !== 3) {
    step3Visualizer.hidden = true;
  }
  if (Number(step.id) !== 4) {
    step4Visualizer.hidden = true;
  }
  if (Number(step.id) !== 5) {
    step5Visualizer.hidden = true;
  }
  if (Number(step.id) !== 6) {
    step6Visualizer.hidden = true;
  }
  if (Number(step.id) !== 7) {
    step7Visualizer.hidden = true;
  }
  resetOutput();
}

function parseKeyList(text) {
  return String(text || "")
    .split(",")
    .map((x) => x.trim())
    .filter((x) => x.length > 0)
    .map((x) => Number(x))
    .filter((x) => Number.isFinite(x))
    .sort((a, b) => a - b);
}

function leafTreeText(keys) {
  const payloads = keys.map((k) => k * 10);
  return `root(leaf): [${keys.join(", ")}]\npayloads : [${payloads.join(", ")}]`;
}

function runA() {
  const keys = parseKeyList(aKeysInput.value);
  const key = Number(aSearchKeyInput.value);
  if (!keys.length || !Number.isFinite(key)) {
    aAfterResult.textContent = "입력 오류: 키 목록과 검색 key를 입력하세요.";
    return;
  }
  aBeforeTree.textContent = leafTreeText(keys);
  const ix = keys.indexOf(key);
  aAfterResult.textContent = [
    `query key = ${key}`,
    `경로: root(leaf)`,
    ix >= 0 ? "결과: HIT" : "결과: MISS",
    ix >= 0 ? `payload = ${key * 10}` : "payload = (none)",
    "",
    "호출 순서:",
    "find_leaf -> bplus_search"
  ].join("\n");
}

function runB() {
  const keys = parseKeyList(bKeysInput.value);
  const key = Number(bInsertKeyInput.value);
  if (!keys.length || !Number.isFinite(key)) {
    bAfterTree.textContent = "입력 오류: 키 목록과 삽입 key를 입력하세요.";
    return;
  }
  const before = [...keys];
  bBeforeTree.textContent = leafTreeText(before);
  if (!before.includes(key)) before.push(key);
  before.sort((a, b) => a - b);
  bAfterTree.textContent = `삽입 key=${key}\n\n${leafTreeText(before)}`;
  bTrace.textContent = "호출 순서: find_leaf -> leaf_insert_sorted -> bplus_insert";
}

function runC() {
  const keys = parseKeyList(cKeysInput.value);
  const key = Number(cInsertKeyInput.value);
  if (!keys.length || !Number.isFinite(key)) {
    cAfterTree.textContent = "입력 오류: 키 목록과 삽입 key를 입력하세요.";
    return;
  }
  const before = [...keys];
  cBeforeTree.textContent = leafTreeText(before);
  if (!before.includes(key)) before.push(key);
  before.sort((a, b) => a - b);
  const n = before.length;
  const split = Math.floor((n + 1) / 2);
  const left = before.slice(0, split);
  const right = before.slice(split);
  const sep = right[0];
  cAfterTree.textContent = [
    `삽입 key=${key}`,
    "",
    `new root(internal): [${sep}]`,
    `  left leaf : [${left.join(", ")}]`,
    `  right leaf: [${right.join(", ")}]`
  ].join("\n");
  cTrace.textContent = [
    `sep = ${sep} (오른쪽 리프 첫 키)`,
    "호출 순서:",
    "find_leaf -> leaf_split_insert -> insert_into_parent",
    "(부모 없음이면 새 루트 생성)"
  ].join("\n");
}

function renderStep1ABC() {
  if (!aKeysInput.value) aKeysInput.value = "10,20,30";
  if (!aSearchKeyInput.value) aSearchKeyInput.value = "20";
  if (!bKeysInput.value) bKeysInput.value = "10,30";
  if (!bInsertKeyInput.value) bInsertKeyInput.value = "20";
  if (!cKeysInput.value) cKeysInput.value = "10,20,30";
  if (!cInsertKeyInput.value) cInsertKeyInput.value = "25";
  aBeforeTree.textContent = "(A 실행 버튼을 누르면 표시)";
  aAfterResult.textContent = "(A 실행 결과가 여기 표시됩니다)";
  bBeforeTree.textContent = "(B 실행 버튼을 누르면 표시)";
  bAfterTree.textContent = "(B 실행 결과가 여기 표시됩니다)";
  bTrace.textContent = "";
  cBeforeTree.textContent = "(C 실행 버튼을 누르면 표시)";
  cAfterTree.textContent = "(C 실행 결과가 여기 표시됩니다)";
  cTrace.textContent = "";
}

function run2A() {
  const ids = parseKeyList(s2aIdsInput.value);
  if (!ids.length) {
    s2aAfter.textContent = "입력 오류: id 목록을 입력하세요.";
    return;
  }
  s2aBefore.textContent = [
    `CSV id 열: [${ids.join(", ")}]`,
    "index: (empty)",
    "next_id: unknown"
  ].join("\n");
  const pairs = [];
  let maxId = 0;
  ids.forEach((id, idx) => {
    if (id > maxId) maxId = id;
    pairs.push(`${id}->${idx}`);
  });
  const lastWinMap = {};
  ids.forEach((id, idx) => { lastWinMap[id] = idx; });
  const compact = Object.entries(lastWinMap)
    .sort((a, b) => Number(a[0]) - Number(b[0]))
    .map(([k, v]) => `${k}->${v}`);
  s2aAfter.textContent = [
    `bplus_insert_or_replace 순서: ${pairs.join(", ")}`,
    `index(last-row-wins): { ${compact.join(", ")} }`,
    `next_id = max(id)+1 = ${maxId + 1}`
  ].join("\n");
}

function run2B() {
  const nextId = Number(s2bNextIdInput.value);
  const raw = String(s2bValuesInput.value || "").trim();
  if (!Number.isFinite(nextId) || !raw) {
    s2bAfter.textContent = "입력 오류: next_id와 입력 값을 채우세요.";
    return;
  }
  s2bBefore.textContent = [
    `next_id: ${nextId}`,
    `원본 값: [${raw}]`
  ].join("\n");
  s2bAfter.textContent = [
    `assigned_id = ${nextId}`,
    `prepared values: [${nextId}, ${raw}]`,
    "호출 흐름: week7_prepare_insert_values -> append 준비 완료"
  ].join("\n");
}

function run2C() {
  const total = Number(s2cTotalRowsInput.value);
  const assignedId = Number(s2cAssignedIdInput.value);
  if (!Number.isFinite(total) || total < 1 || !Number.isFinite(assignedId)) {
    s2cAfter.textContent = "입력 오류: total rows(1 이상), assigned_id를 입력하세요.";
    return;
  }
  const rowRef = total - 1;
  s2cBefore.textContent = [
    `append 성공 직후 total rows: ${total}`,
    `assigned_id: ${assignedId}`,
    "index 반영 전"
  ].join("\n");
  s2cAfter.textContent = [
    `row_ref = total-1 = ${rowRef}`,
    `index update: ${assignedId} -> ${rowRef}`,
    "호출 흐름: data_row_count -> week7_on_append_success"
  ].join("\n");
}

function renderStep2ABC() {
  if (!s2aIdsInput.value) s2aIdsInput.value = "1,2,4,4";
  if (!s2bNextIdInput.value) s2bNextIdInput.value = "5";
  if (!s2bValuesInput.value) s2bValuesInput.value = "'new_user','new@example.com'";
  if (!s2cTotalRowsInput.value) s2cTotalRowsInput.value = "6";
  if (!s2cAssignedIdInput.value) s2cAssignedIdInput.value = "5";
  s2aBefore.textContent = "(A 실행 버튼을 누르면 표시)";
  s2aAfter.textContent = "(A 실행 결과가 여기 표시됩니다)";
  s2bBefore.textContent = "(B 실행 버튼을 누르면 표시)";
  s2bAfter.textContent = "(B 실행 결과가 여기 표시됩니다)";
  s2cBefore.textContent = "(C 실행 버튼을 누르면 표시)";
  s2cAfter.textContent = "(C 실행 결과가 여기 표시됩니다)";
}

function run3A() {
  const id = Number(s3aAssignedIdInput.value);
  const total = Number(s3aTotalRowsInput.value);
  if (!Number.isFinite(id) || !Number.isFinite(total) || total < 1) {
    s3aAfter.textContent = "입력 오류: assigned_id와 total rows(1 이상)를 입력하세요.";
    return;
  }
  const rowRef = total - 1;
  s3aBefore.textContent = [
    "prepare 완료",
    `assigned_id = ${id}`,
    "append 실행 전"
  ].join("\n");
  s3aAfter.textContent = [
    "append 성공",
    `row_ref = total-1 = ${rowRef}`,
    `on_append_success(${id}, ${rowRef}) 호출`,
    `index 반영: ${id} -> ${rowRef}`
  ].join("\n");
}

function run3B() {
  const reason = String(s3bReasonInput.value || "").trim() || "I/O error";
  s3bBefore.textContent = [
    "prepare 완료",
    "append 시도"
  ].join("\n");
  s3bAfter.textContent = [
    `append 실패: ${reason}`,
    "on_append_success 호출 안 함",
    "즉시 오류 반환 (인덱스 갱신 없음)"
  ].join("\n");
}

function run3C() {
  const id = Number(s3cAssignedIdInput.value);
  const rowRef = Number(s3cRowRefInput.value);
  if (!Number.isFinite(id) || !Number.isFinite(rowRef) || rowRef < 0) {
    s3cAfter.textContent = "입력 오류: assigned_id와 row_ref(0 이상)를 입력하세요.";
    return;
  }
  s3cBefore.textContent = [
    `INSERT 반영: ${id} -> ${rowRef}`,
    "WHERE id 조회 전"
  ].join("\n");
  s3cAfter.textContent = [
    `week7_lookup_row(id=${id}) -> row_ref=${rowRef}`,
    "SELECT 출력 행 = INSERT 직후 반영한 동일 행",
    "결론: assigned_id와 row_ref 매핑 일치"
  ].join("\n");
}

function renderStep3ABC() {
  if (!s3aAssignedIdInput.value) s3aAssignedIdInput.value = "7";
  if (!s3aTotalRowsInput.value) s3aTotalRowsInput.value = "8";
  if (!s3bReasonInput.value) s3bReasonInput.value = "I/O error";
  if (!s3cAssignedIdInput.value) s3cAssignedIdInput.value = "7";
  if (!s3cRowRefInput.value) s3cRowRefInput.value = "7";
  s3aBefore.textContent = "(A 실행 버튼을 누르면 표시)";
  s3aAfter.textContent = "(A 실행 결과가 여기 표시됩니다)";
  s3bBefore.textContent = "(B 실행 버튼을 누르면 표시)";
  s3bAfter.textContent = "(B 실행 결과가 여기 표시됩니다)";
  s3cBefore.textContent = "(C 실행 버튼을 누르면 표시)";
  s3cAfter.textContent = "(C 실행 결과가 여기 표시됩니다)";
}

function run4A() {
  const sql = String(s4aSqlInput.value || "").trim();
  if (!sql) {
    s4aAfter.textContent = "입력 오류: SQL을 입력하세요.";
    return;
  }
  s4aBefore.textContent = `SQL: ${sql}`;
  s4aAfter.textContent = [
    "WHERE 없음 경로",
    "AST:",
    "{",
    "  has_where_id_eq: 0",
    "  where_id_value: (unset)",
    "}"
  ].join("\n");
}

function run4B() {
  const sql = String(s4bSqlInput.value || "").trim();
  if (!sql) {
    s4bAfter.textContent = "입력 오류: SQL을 입력하세요.";
    return;
  }
  const m = sql.match(/where\s+id\s*=\s*([0-9]+)/i);
  s4bBefore.textContent = `SQL: ${sql}`;
  if (!m) {
    s4bAfter.textContent = "패턴 불일치: WHERE id = <정수> 형태가 아닙니다.";
    return;
  }
  s4bAfter.textContent = [
    "WHERE id = 정수 성공 경로",
    "AST:",
    "{",
    "  has_where_id_eq: 1",
    `  where_id_value: ${m[1]}`,
    "}"
  ].join("\n");
}

function run4C() {
  const sql = String(s4cSqlInput.value || "").trim();
  if (!sql) {
    s4cAfter.textContent = "입력 오류: SQL을 입력하세요.";
    return;
  }
  s4cBefore.textContent = `SQL: ${sql}`;
  s4cAfter.textContent = [
    "미지원 WHERE 실패 경로",
    "결과: parse error",
    "사유: WHERE id = <정수> 외 패턴"
  ].join("\n");
}

function renderStep4ABC() {
  if (!s4aSqlInput.value) s4aSqlInput.value = "SELECT id, name FROM users";
  if (!s4bSqlInput.value) s4bSqlInput.value = "SELECT * FROM users WHERE id = 7";
  if (!s4cSqlInput.value) s4cSqlInput.value = "SELECT * FROM users WHERE id > 1";
  s4aBefore.textContent = "(A 실행 버튼을 누르면 표시)";
  s4aAfter.textContent = "(A 실행 결과가 여기 표시됩니다)";
  s4bBefore.textContent = "(B 실행 버튼을 누르면 표시)";
  s4bAfter.textContent = "(B 실행 결과가 여기 표시됩니다)";
  s4cBefore.textContent = "(C 실행 버튼을 누르면 표시)";
  s4cAfter.textContent = "(C 실행 결과가 여기 표시됩니다)";
}

function run5A() {
  const id = Number(s5aIdInput.value);
  const rowRef = Number(s5aRowRefInput.value);
  if (!Number.isFinite(id) || !Number.isFinite(rowRef) || rowRef < 0) {
    s5aAfter.textContent = "입력 오류: id와 row_ref(0 이상)를 입력하세요.";
    return;
  }
  s5aBefore.textContent = [
    `WHERE id = ${id}`,
    "lookup 전"
  ].join("\n");
  s5aAfter.textContent = [
    "ensure_loaded -> table_has_id_pk(true)",
    `lookup_row(${id}) -> ${rowRef} (hit)`,
    "출력: header + one row"
  ].join("\n");
}

function run5B() {
  const id = Number(s5bIdInput.value);
  if (!Number.isFinite(id)) {
    s5bAfter.textContent = "입력 오류: id를 입력하세요.";
    return;
  }
  s5bBefore.textContent = [
    `WHERE id = ${id}`,
    "lookup 전"
  ].join("\n");
  s5bAfter.textContent = [
    "ensure_loaded -> table_has_id_pk(true)",
    `lookup_row(${id}) -> miss`,
    "출력: header only"
  ].join("\n");
}

function run5C() {
  const sql = String(s5cSqlInput.value || "").trim();
  if (!sql) {
    s5cAfter.textContent = "입력 오류: SQL을 입력하세요.";
    return;
  }
  s5cBefore.textContent = `SQL: ${sql}`;
  s5cAfter.textContent = [
    "WHERE 없음 경로",
    "csv_storage_read_table 호출",
    "기존 select_all/컬럼 프로젝션 로직 출력"
  ].join("\n");
}

function renderStep5ABC() {
  if (!s5aIdInput.value) s5aIdInput.value = "5";
  if (!s5aRowRefInput.value) s5aRowRefInput.value = "4";
  if (!s5bIdInput.value) s5bIdInput.value = "999999";
  if (!s5cSqlInput.value) s5cSqlInput.value = "SELECT id, name FROM users";
  s5aBefore.textContent = "(A 실행 버튼을 누르면 표시)";
  s5aAfter.textContent = "(A 실행 결과가 여기 표시됩니다)";
  s5bBefore.textContent = "(B 실행 버튼을 누르면 표시)";
  s5bAfter.textContent = "(B 실행 결과가 여기 표시됩니다)";
  s5cBefore.textContent = "(C 실행 버튼을 누르면 표시)";
  s5cAfter.textContent = "(C 실행 결과가 여기 표시됩니다)";
}

function run6A() {
  const id = Number(s6aIdInput.value);
  if (!Number.isFinite(id)) {
    s6aAfter.textContent = "입력 오류: id를 입력하세요.";
    return;
  }
  s6aBefore.textContent = `SELECT * FROM users WHERE id = ${id}`;
  s6aAfter.textContent = [
    "lookup_row -> miss(-1)",
    "출력: header only",
    "exit code: 0 (정상)"
  ].join("\n");
}

function run6B() {
  const sql = String(s6bSqlInput.value || "").trim();
  if (!sql) {
    s6bAfter.textContent = "입력 오류: SQL을 입력하세요.";
    return;
  }
  s6bBefore.textContent = `SQL: ${sql}`;
  s6bAfter.textContent = [
    "parser_parse_select: 미지원 WHERE 감지",
    "결과: parse error",
    "exit code: 2 (문법 오류)"
  ].join("\n");
}

function run6C() {
  const table = String(s6cTableInput.value || "").trim();
  if (!table) {
    s6cAfter.textContent = "입력 오류: 테이블 이름을 입력하세요.";
    return;
  }
  s6cBefore.textContent = `SELECT * FROM ${table} WHERE id = 1`;
  s6cAfter.textContent = [
    "csv_storage_read_table/read_table_row: I/O 실패",
    "결과: execution error",
    "exit code: 3 (실행 오류)"
  ].join("\n");
}

function renderStep6ABC() {
  if (!s6aIdInput.value) s6aIdInput.value = "999999";
  if (!s6bSqlInput.value) s6bSqlInput.value = "SELECT * FROM users WHERE id > 1";
  if (!s6cTableInput.value) s6cTableInput.value = "missing_table";
  s6aBefore.textContent = "(A 실행 버튼을 누르면 표시)";
  s6aAfter.textContent = "(A 실행 결과가 여기 표시됩니다)";
  s6bBefore.textContent = "(B 실행 버튼을 누르면 표시)";
  s6bAfter.textContent = "(B 실행 결과가 여기 표시됩니다)";
  s6cBefore.textContent = "(C 실행 버튼을 누르면 표시)";
  s6cAfter.textContent = "(C 실행 결과가 여기 표시됩니다)";
}

function run7Unified() {
  const n = Number(s7nInput.value);
  const k = Number(s7kInput.value);
  if (!Number.isFinite(n) || n < 1 || !Number.isFinite(k) || k < 1) {
    s7After.textContent = "입력 오류: n, k는 1 이상의 정수여야 합니다.";
    return;
  }
  s7Before.textContent = [
    "1) bench_bplus 빌드",
    "cmake --build build-ninja --target bench_bplus",
    "",
    "2) compare 실행",
    `.\\build-ninja\\bench_bplus.exe compare ${n} ${k}`
  ].join("\n");
  s7After.textContent = [
    "예상 출력 지표:",
    "- insert_sec",
    "- index_lookup_sec",
    "- linear_scan_sec",
    "- speedup ratio",
    "",
    "해석 포인트:",
    "- 절대값보다 index vs linear의 비율/경향을 본다.",
    "- compare는 SQL 전체 경로가 아니라 lookup 성능 비교용이다."
  ].join("\n");
}

function renderStep7Unified() {
  if (!s7nInput.value) s7nInput.value = "1000000";
  if (!s7kInput.value) s7kInput.value = "10000";
  s7Before.textContent = "(실행 흐름 생성 버튼을 누르면 표시)";
  s7After.textContent = "(지표 해석이 여기 표시됩니다)";
}

function selectStep(id) {
  activeStep = STEPS.find((s) => s.id === id) || null;
  for (const btn of stepNav.querySelectorAll("button")) {
    btn.classList.toggle("is-active", Number(btn.dataset.stepId) === id);
  }
  if (activeStep) renderStepDetail(activeStep);
}

function buildNav() {
  stepNav.innerHTML = "";
  STEPS.forEach((s) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "step-nav-btn";
    b.dataset.stepId = String(s.id);
    b.textContent = `${s.id}. ${s.title.split("—")[1]?.trim() || s.title}`;
    b.addEventListener("click", () => selectStep(s.id));
    stepNav.appendChild(b);
  });
}

function renderTerminalPresets(step) {
  terminalPresetRow.innerHTML = "";
  terminalOutput.textContent = "";
  selectedTerminalAction = null;
  (step.actions || []).forEach((act) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "preset-btn";
    b.textContent = act.label;
    b.addEventListener("click", () => {
      selectedTerminalAction = act;
      terminalCmdInput.value = act.type === "sql" ? buildTraceCommand(act.sql) : act.command;
    });
    terminalPresetRow.appendChild(b);
  });
  if (step.actions && step.actions.length) {
    selectedTerminalAction = step.actions[0];
    terminalCmdInput.value = selectedTerminalAction.type === "sql"
      ? buildTraceCommand(selectedTerminalAction.sql)
      : selectedTerminalAction.command;
  } else {
    terminalCmdInput.value = "";
  }
}

function renderTrace(result) {
  tokenBody.innerHTML = "";
  astView.textContent = "";
  executorList.innerHTML = "";
  stdoutView.textContent = result.stdout || "";
  stderrView.textContent = result.stderr || "";
  exitCode.textContent = `exit code: ${result.exitCode}`;

  const asts = [];
  for (const ev of result.traceEvents || []) {
    if (ev.step === "lexer_tokens" && Array.isArray(ev.tokens)) {
      ev.tokens.forEach((t) => {
        const tr = document.createElement("tr");
        tr.innerHTML = `<td>${esc(t.kind)}</td><td>${esc(t.text)}</td><td>${esc(t.line)}</td><td>${esc(t.column)}</td>`;
        tokenBody.appendChild(tr);
      });
    }
    if (ev.step === "parser_result" && ev.ok && ev.ast) asts.push(ev.ast);
    if (ev.step === "executor_call") {
      const li = document.createElement("li");
      li.textContent = `statement ${ev.statementNo}: ${ev.executor}`;
      executorList.appendChild(li);
    }
  }
  astView.textContent = asts.length ? JSON.stringify(asts, null, 2) : "(AST 없음)";

  const before = getUsersCsv(result.csvBefore || {});
  const after = getUsersCsv(result.csvAfter || {});
  diffView.textContent = simpleLineDiff(before, after);
}

async function runSql(sql) {
  const res = await fetch("/api/run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ sql })
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || data.hint || "run failed");
  return data;
}

async function runCommand(command) {
  const res = await fetch("/api/run-command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ command })
  });
  const data = await res.json();
  if (!res.ok) throw new Error(data.error || "command run failed");
  return {
    exitCode: data.exitCode,
    stdout: data.stdout || "",
    stderr: data.stderr || "",
    traceEvents: [],
    csvBefore: {},
    csvAfter: {}
  };
}

runBtn.addEventListener("click", async () => {
  if (!activeStep || activeStep.offline) return;
  if (!sqlInput.value.trim()) {
    stderrView.textContent = "SQL을 입력하거나 프리셋을 선택하세요.";
    return;
  }
  runBtn.disabled = true;
  try {
    const data = await runSql(sqlInput.value);
    renderTrace(data);
  } catch (e) {
    stderrView.textContent = String(e.message || e);
    stdoutView.textContent = "";
    exitCode.textContent = "exit code: —";
  } finally {
    runBtn.disabled = false;
  }
});

terminalRunBtn.addEventListener("click", async () => {
  if (!activeStep) return;
  const action = selectedTerminalAction;
  if (!action) {
    terminalOutput.textContent = "선택된 프리셋이 없습니다.";
    return;
  }
  terminalRunBtn.disabled = true;
  terminalOutput.textContent = "실행 중...";
  try {
    let result;
    if (action.type === "sql") {
      result = await runSql(action.sql);
      renderTrace(result);
      terminalOutput.textContent = [
        "> [SQL 프리셋 실행]",
        action.sql.trim(),
        "",
        `exit code: ${result.exitCode}`,
        "[stdout]",
        result.stdout || "(empty)",
        "[stderr]",
        result.stderr || "(empty)"
      ].join("\n");
    } else {
      result = await runCommand(action.command);
      terminalOutput.textContent = [
        `> ${action.command}`,
        "",
        `exit code: ${result.exitCode}`,
        "[stdout]",
        result.stdout || "(empty)",
        "[stderr]",
        result.stderr || "(empty)"
      ].join("\n");
      stdoutView.textContent = result.stdout || "";
      stderrView.textContent = result.stderr || "";
      exitCode.textContent = `exit code: ${result.exitCode}`;
      diffView.textContent = "(command 실행: CSV diff 없음)";
      tokenBody.innerHTML = "";
      astView.textContent = "(AST 없음)";
      executorList.innerHTML = "";
    }
  } catch (e) {
    terminalOutput.textContent = `실행 실패: ${String(e.message || e)}`;
  } finally {
    terminalRunBtn.disabled = false;
  }
});

buildNav();
selectStep(1);

genSqlCmdBtn.addEventListener("click", () => {
  const sql = sqlInput.value || "";
  if (!sql.trim()) {
    stderrView.textContent = "명령어를 만들 SQL이 없습니다. 프리셋을 먼저 선택하세요.";
    return;
  }
  sqlCmdView.textContent = buildTraceCommand(sql);
  sqlCmdBox.hidden = false;
});

copySqlCmdBtn.addEventListener("click", async () => {
  const sql = sqlInput.value || "";
  if (!sql.trim()) {
    stderrView.textContent = "복사할 SQL이 없습니다. 프리셋을 먼저 선택하세요.";
    return;
  }
  const cmd = buildTraceCommand(sql);
  try {
    await navigator.clipboard.writeText(cmd);
    copySqlCmdBtn.textContent = "복사됨";
    setTimeout(() => {
      copySqlCmdBtn.textContent = "명령어 복사";
    }, 1200);
  } catch (_e) {
    sqlCmdView.textContent = cmd;
    sqlCmdBox.hidden = false;
    copySqlCmdBtn.textContent = "복사 실패(아래 수동복사)";
  }
});

aRunBtn.addEventListener("click", runA);
bRunBtn.addEventListener("click", runB);
cRunBtn.addEventListener("click", runC);
s2aRunBtn.addEventListener("click", run2A);
s2bRunBtn.addEventListener("click", run2B);
s2cRunBtn.addEventListener("click", run2C);
s3aRunBtn.addEventListener("click", run3A);
s3bRunBtn.addEventListener("click", run3B);
s3cRunBtn.addEventListener("click", run3C);
s4aRunBtn.addEventListener("click", run4A);
s4bRunBtn.addEventListener("click", run4B);
s4cRunBtn.addEventListener("click", run4C);
s5aRunBtn.addEventListener("click", run5A);
s5bRunBtn.addEventListener("click", run5B);
s5cRunBtn.addEventListener("click", run5C);
s6aRunBtn.addEventListener("click", run6A);
s6bRunBtn.addEventListener("click", run6B);
s6cRunBtn.addEventListener("click", run6C);
s7RunBtn.addEventListener("click", run7Unified);
