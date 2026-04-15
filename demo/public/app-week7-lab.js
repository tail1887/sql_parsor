const FLOW_PRESETS = [
  {
    label: "자동 ID INSERT + 전체 조회",
    sql: "INSERT INTO users VALUES ('week7_demo', 'week7@example.com');\nSELECT * FROM users;\n",
  },
  {
    label: "ID 기반 조회",
    sql: "SELECT id, name, email FROM users WHERE id = 1;\n",
  },
  {
    label: "전체 스캔 SELECT",
    sql: "SELECT id, name, email FROM users;\n",
  },
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
const benchRunWideBtn = document.getElementById("benchRunWideBtn");
const benchRunBothBtn = document.getElementById("benchRunBothBtn");
const benchCmdView = document.getElementById("benchCmdView");
const benchOutput = document.getElementById("benchOutput");
const benchSummary = document.getElementById("benchSummary");

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
    if (va === vb) {
      out.push(`  ${va}`);
      continue;
    }
    if (va !== "") out.push(`- ${va}`);
    if (vb !== "") out.push(`+ ${vb}`);
  }

  return out.join("\n");
}

function summarizeTrace(events) {
  if (!Array.isArray(events) || !events.length) {
    return "(trace 없음)";
  }

  const stepCounts = new Map();
  for (const ev of events) {
    const key = ev && ev.step ? ev.step : "unknown";
    stepCounts.set(key, (stepCounts.get(key) || 0) + 1);
  }

  return Array.from(stepCounts.entries())
    .map(([key, count]) => `${key}: ${count}`)
    .join("\n");
}

async function runSql(sql) {
  const res = await fetch("/api/run", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ sql }),
  });
  const data = await res.json();
  if (!res.ok) {
    throw new Error(data.error || data.hint || "실행에 실패했습니다");
  }
  return data;
}

async function runCommand(command) {
  const res = await fetch("/api/run-command", {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ command }),
  });
  const data = await res.json();
  if (!res.ok) {
    throw new Error(data.error || "명령 실행에 실패했습니다");
  }
  return data;
}

function parseBenchNumbers(stdout) {
  const grab = (key) => {
    const match = stdout.match(new RegExp(`${key}\\s*[:=]\\s*([0-9]+(?:\\.[0-9]+)?)`, "i"));
    return match ? Number(match[1]) : null;
  };

  return {
    insertSec: grab("insert_sec"),
    indexSec: grab("index_lookup_sec"),
    linearSec: grab("linear_scan_sec"),
  };
}

function renderBenchSummary(stdout) {
  const { insertSec, indexSec, linearSec } = parseBenchNumbers(stdout);
  if (indexSec == null || linearSec == null) {
    return [
      "벤치 출력을 해석하지 못했습니다.",
      "stdout에 index_lookup_sec와 linear_scan_sec가 필요합니다.",
    ].join("\n");
  }

  const ratio = linearSec > 0 ? indexSec / linearSec : null;
  const speedup = indexSec > 0 ? linearSec / indexSec : null;

  return [
    `insert_sec: ${insertSec ?? "(N/A)"}`,
    `index_lookup_sec: ${indexSec}`,
    `linear_scan_sec: ${linearSec}`,
    "",
    ratio == null ? "index/linear 비율: N/A" : `index/linear 비율: ${ratio.toFixed(6)}`,
    speedup == null ? "속도 향상: N/A" : `linear 대비 index 속도 향상: ${speedup.toFixed(2)}x`,
  ].join("\n");
}

function compareSummaries(baseStdout, wideStdout) {
  const base = parseBenchNumbers(baseStdout || "");
  const wide = parseBenchNumbers(wideStdout || "");

  if (base.indexSec == null || wide.indexSec == null) {
    return "두 벤치 결과를 비교하지 못했습니다.";
  }

  const indexGain = wide.indexSec > 0 ? base.indexSec / wide.indexSec : null;
  const linearGain =
    base.linearSec != null && wide.linearSec != null && wide.linearSec > 0
      ? base.linearSec / wide.linearSec
      : null;

  return [
    "[BP_MAX_KEYS=3]",
    renderBenchSummary(baseStdout),
    "",
    "[BP_MAX_KEYS=31]",
    renderBenchSummary(wideStdout),
    "",
    "[비교]",
    indexGain == null ? "index lookup 개선: N/A" : `index lookup 개선: ${indexGain.toFixed(2)}x`,
    linearGain == null ? "linear scan 개선: N/A" : `linear scan 개선: ${linearGain.toFixed(2)}x`,
  ].join("\n");
}

function initFlowPresets() {
  flowPresetRow.innerHTML = "";
  for (const preset of FLOW_PRESETS) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.className = "preset-btn";
    btn.textContent = preset.label;
    btn.addEventListener("click", () => {
      flowSqlInput.value = preset.sql;
    });
    flowPresetRow.appendChild(btn);
  }
  flowSqlInput.value = FLOW_PRESETS[0].sql;
}

flowRunBtn.addEventListener("click", async () => {
  const sql = String(flowSqlInput.value || "").trim();
  if (!sql) {
    flowStderr.textContent = "먼저 SQL을 입력해 주세요.";
    return;
  }

  flowRunBtn.disabled = true;
  flowStdout.textContent = "";
  flowStderr.textContent = "실행 중...";
  flowTraceSummary.textContent = "";
  flowDiff.textContent = "";
  flowExitCode.textContent = "exit code: ?";

  try {
    const result = await runSql(sql);
    flowStdout.textContent = result.stdout || "(empty)";
    flowStderr.textContent = result.stderr || "(empty)";
    flowExitCode.textContent = `exit code: ${result.exitCode}`;
    flowTraceSummary.textContent = summarizeTrace(result.traceEvents);
    flowDiff.textContent = simpleLineDiff(
      getUsersCsv(result.csvBefore || {}),
      getUsersCsv(result.csvAfter || {})
    );
  } catch (err) {
    flowStderr.textContent = String(err.message || err);
  } finally {
    flowRunBtn.disabled = false;
  }
});

benchBuildBtn.addEventListener("click", async () => {
  const cmd = ".\\demo\\scripts\\build_demo_binaries.cmd";
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
      result.stderr || "(empty)",
    ].join("\n");
    benchSummary.textContent =
      result.exitCode === 0
        ? "벤치 바이너리 빌드가 끝났습니다. 이제 실행 버튼을 눌러 비교할 수 있습니다."
        : "빌드에 실패했습니다. stderr를 확인해 주세요.";
  } catch (err) {
    benchOutput.textContent = `실행 실패: ${String(err.message || err)}`;
  } finally {
    benchBuildBtn.disabled = false;
  }
});

async function runBench(binaryName) {
  const n = Number(benchNInput.value || 1000000);
  const k = Number(benchKInput.value || 10000);

  if (!Number.isFinite(n) || n < 1000000) {
    benchSummary.textContent = "n은 최소 1,000,000 이상이어야 합니다.";
    return null;
  }
  if (!Number.isFinite(k) || k < 1) {
    benchSummary.textContent = "k는 1 이상의 정수여야 합니다.";
    return null;
  }

  const cmd = `.\\build-demo\\${binaryName}.exe compare ${Math.floor(n)} ${Math.floor(k)}`;
  benchCmdView.textContent = cmd;
  return { cmd, n: Math.floor(n), k: Math.floor(k) };
}

benchRunBtn.addEventListener("click", async () => {
  const params = await runBench("bench_bplus");
  if (!params) return;

  benchOutput.textContent = "실행 중...";
  benchSummary.textContent = "";
  benchRunBtn.disabled = true;

  try {
    const result = await runCommand(params.cmd);
    benchOutput.textContent = [
      `[bench_bplus] exit code: ${result.exitCode}`,
      "",
      "[stdout]",
      result.stdout || "(empty)",
      "",
      "[stderr]",
      result.stderr || "(empty)",
    ].join("\n");
    benchSummary.textContent =
      result.exitCode === 0 ? renderBenchSummary(result.stdout || "") : "벤치 실행에 실패했습니다.";
  } catch (err) {
    benchOutput.textContent = `실행 실패: ${String(err.message || err)}`;
  } finally {
    benchRunBtn.disabled = false;
  }
});

benchRunWideBtn.addEventListener("click", async () => {
  const params = await runBench("bench_bplus_wide");
  if (!params) return;

  benchOutput.textContent = "실행 중...";
  benchSummary.textContent = "";
  benchRunWideBtn.disabled = true;

  try {
    const result = await runCommand(params.cmd);
    benchOutput.textContent = [
      `[bench_bplus_wide] exit code: ${result.exitCode}`,
      "",
      "[stdout]",
      result.stdout || "(empty)",
      "",
      "[stderr]",
      result.stderr || "(empty)",
    ].join("\n");
    benchSummary.textContent =
      result.exitCode === 0 ? renderBenchSummary(result.stdout || "") : "벤치 실행에 실패했습니다.";
  } catch (err) {
    benchOutput.textContent = `실행 실패: ${String(err.message || err)}`;
  } finally {
    benchRunWideBtn.disabled = false;
  }
});

benchRunBothBtn.addEventListener("click", async () => {
  const base = await runBench("bench_bplus");
  if (!base) return;

  const wideCmd = `.\\build-demo\\bench_bplus_wide.exe compare ${base.n} ${base.k}`;
  benchCmdView.textContent = [base.cmd, wideCmd].join("\n");
  benchOutput.textContent = "두 벤치를 순서대로 실행 중...";
  benchSummary.textContent = "";
  benchRunBothBtn.disabled = true;

  try {
    const baseResult = await runCommand(base.cmd);
    const wideResult = await runCommand(wideCmd);
    benchOutput.textContent = [
      `[bench_bplus] exit code: ${baseResult.exitCode}`,
      baseResult.stdout || "(empty)",
      "",
      `[bench_bplus_wide] exit code: ${wideResult.exitCode}`,
      wideResult.stdout || "(empty)",
      "",
      "[stderr(base)]",
      baseResult.stderr || "(empty)",
      "",
      "[stderr(wide)]",
      wideResult.stderr || "(empty)",
    ].join("\n");

    if (baseResult.exitCode === 0 && wideResult.exitCode === 0) {
      benchSummary.textContent = compareSummaries(baseResult.stdout || "", wideResult.stdout || "");
    } else {
      benchSummary.textContent = "둘 중 하나 이상의 벤치 실행이 실패했습니다.";
    }
  } catch (err) {
    benchOutput.textContent = `실행 실패: ${String(err.message || err)}`;
  } finally {
    benchRunBothBtn.disabled = false;
  }
});

initFlowPresets();
benchNInput.value = "1000000";
benchKInput.value = "10000";
benchCmdView.textContent = ".\\build-demo\\bench_bplus.exe compare 1000000 10000";
benchOutput.textContent = "(아직 실행하지 않았습니다)";
benchSummary.textContent = "(요약이 여기에 표시됩니다)";
