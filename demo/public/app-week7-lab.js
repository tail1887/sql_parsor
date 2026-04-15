const FLOW_PRESETS = [
  {
    label: "자동 ID INSERT + 전체 확인",
    sql: "INSERT INTO users VALUES ('week7_demo', 'week7@example.com');\nSELECT * FROM users;\n"
  },
  {
    label: "ID 기반 조회 (인덱스 경로)",
    sql: "SELECT id, name, email FROM users WHERE id = 1;\n"
  },
  {
    label: "비-ID 조회 대체 경로(전체 스캔)",
    sql: "SELECT id, name, email FROM users;\n"
  }
];

const flowPresetRow = document.getElementById("flowPresetRow");
const flowSqlInput = document.getElementById("flowSqlInput");
const flowRunBtn = document.getElementById("flowRunBtn");
const flowStdout = document.getElementById("flowStdout");
const flowStderr = document.getElementById("flowStderr");
const flowExitCode = document.getElementById("flowExitCode");
const flowTraceSummary = document.getElementById("flowTraceSummary");
const flowDiff = document.getElementById("flowDiff");

const benchNInput = document.getElementById("benchNInput");
const benchKInput = document.getElementById("benchKInput");
const benchBuildBtn = document.getElementById("benchBuildBtn");
const benchRunBtn = document.getElementById("benchRunBtn");
const benchCmdView = document.getElementById("benchCmdView");
const benchOutput = document.getElementById("benchOutput");
const benchSummary = document.getElementById("benchSummary");

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

function summarizeTrace(events) {
  if (!Array.isArray(events) || !events.length) {
    return "(trace 없음)";
  }
  const stepCounts = new Map();
  for (const ev of events) {
    const k = ev && ev.step ? ev.step : "unknown";
    stepCounts.set(k, (stepCounts.get(k) || 0) + 1);
  }
  return Array.from(stepCounts.entries())
    .map(([k, v]) => `${k}: ${v}`)
    .join("\n");
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
  return data;
}

function parseBenchNumbers(stdout) {
  const grab = (key) => {
    const m = stdout.match(new RegExp(`${key}\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)`, "i"));
    return m ? Number(m[1]) : null;
  };
  return {
    insertSec: grab("insert_sec"),
    indexSec: grab("index_lookup_sec"),
    linearSec: grab("linear_scan_sec")
  };
}

function renderBenchSummary(stdout) {
  const { insertSec, indexSec, linearSec } = parseBenchNumbers(stdout);
  if (indexSec == null || linearSec == null) {
    return [
      "지표 파싱 실패: stdout에서 index_lookup_sec / linear_scan_sec를 찾지 못했습니다.",
      "bench 출력 원문을 확인하세요."
    ].join("\n");
  }
  const ratio = linearSec > 0 ? indexSec / linearSec : null;
  const speedup = indexSec > 0 ? linearSec / indexSec : null;
  return [
    `insert_sec: ${insertSec ?? "(N/A)"}`,
    `index_lookup_sec: ${indexSec}`,
    `linear_scan_sec: ${linearSec}`,
    "",
    ratio == null ? "index/linear 비율 계산 불가" : `index/linear 비율: ${ratio.toFixed(6)}`,
    speedup == null ? "speedup 계산 불가" : `선형 대비 인덱스 speedup: ${speedup.toFixed(2)}x`,
    "",
    "해석:",
    "- ID 기반 SELECT는 B+ 인덱스를 써서 index_lookup_sec로 측정됩니다.",
    "- 비인덱스 필드 조건 SELECT는 선형 탐색에 가까운 경로로 linear_scan_sec에 대응됩니다."
  ].join("\n");
}

function initFlowPresets() {
  flowPresetRow.innerHTML = "";
  for (const p of FLOW_PRESETS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "preset-btn";
    btn.textContent = p.label;
    btn.addEventListener("click", () => {
      flowSqlInput.value = p.sql;
    });
    flowPresetRow.appendChild(btn);
  }
  flowSqlInput.value = FLOW_PRESETS[0].sql;
}

flowRunBtn.addEventListener("click", async () => {
  const sql = String(flowSqlInput.value || "").trim();
  if (!sql) {
    flowStderr.textContent = "SQL을 입력하세요.";
    return;
  }
  flowRunBtn.disabled = true;
  flowStdout.textContent = "";
  flowStderr.textContent = "실행 중...";
  flowTraceSummary.textContent = "";
  flowDiff.textContent = "";
  flowExitCode.textContent = "exit code: —";
  try {
    const result = await runSql(sql);
    flowStdout.textContent = result.stdout || "(empty)";
    flowStderr.textContent = result.stderr || "(empty)";
    flowExitCode.textContent = `exit code: ${result.exitCode}`;
    flowTraceSummary.textContent = summarizeTrace(result.traceEvents);
    flowDiff.textContent = simpleLineDiff(getUsersCsv(result.csvBefore || {}), getUsersCsv(result.csvAfter || {}));
  } catch (e) {
    flowStderr.textContent = String(e.message || e);
  } finally {
    flowRunBtn.disabled = false;
  }
});

benchBuildBtn.addEventListener("click", async () => {
  const cmd = "cmake --build build-ninja --target bench_bplus";
  benchCmdView.textContent = cmd;
  benchOutput.textContent = "실행 중...";
  benchSummary.textContent = "";
  benchBuildBtn.disabled = true;
  try {
    const result = await runCommand(cmd);
    benchOutput.textContent = [
      `exit code: ${result.exitCode}`,
      "",
      "[stdout]",
      result.stdout || "(empty)",
      "",
      "[stderr]",
      result.stderr || "(empty)"
    ].join("\n");
    benchSummary.textContent = result.exitCode === 0
      ? "bench_bplus 빌드 성공. 이제 compare 실행 버튼을 누르세요."
      : "빌드 실패. stderr를 먼저 확인하세요.";
  } catch (e) {
    benchOutput.textContent = `실행 실패: ${String(e.message || e)}`;
  } finally {
    benchBuildBtn.disabled = false;
  }
});

benchRunBtn.addEventListener("click", async () => {
  const n = Number(benchNInput.value || 1000000);
  const k = Number(benchKInput.value || 10000);
  if (!Number.isFinite(n) || n < 1000000) {
    benchSummary.textContent = "n은 최소 1,000,000 이상이어야 합니다.";
    return;
  }
  if (!Number.isFinite(k) || k < 1) {
    benchSummary.textContent = "k는 1 이상의 정수여야 합니다.";
    return;
  }

  const cmd = `.\\build-ninja\\bench_bplus.exe compare ${Math.floor(n)} ${Math.floor(k)}`;
  benchCmdView.textContent = cmd;
  benchOutput.textContent = "실행 중...";
  benchSummary.textContent = "";
  benchRunBtn.disabled = true;
  try {
    const result = await runCommand(cmd);
    benchOutput.textContent = [
      `exit code: ${result.exitCode}`,
      "",
      "[stdout]",
      result.stdout || "(empty)",
      "",
      "[stderr]",
      result.stderr || "(empty)"
    ].join("\n");
    benchSummary.textContent = result.exitCode === 0
      ? renderBenchSummary(result.stdout || "")
      : "bench 실행 실패. stderr를 확인하세요.";
  } catch (e) {
    benchOutput.textContent = `실행 실패: ${String(e.message || e)}`;
  } finally {
    benchRunBtn.disabled = false;
  }
});

initFlowPresets();
benchNInput.value = "1000000";
benchKInput.value = "10000";
benchCmdView.textContent = ".\\build-ninja\\bench_bplus.exe compare 1000000 10000";
benchOutput.textContent = "(아직 실행하지 않았습니다)";
benchSummary.textContent = "(결과 해석이 여기에 표시됩니다)";
