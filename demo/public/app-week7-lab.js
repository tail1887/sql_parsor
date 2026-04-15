/**
 * WEEK7 learning-guide 단계별 실습 (GET /api/week7-lab)
 */
const sqlInput = document.getElementById("sqlInput");
const runBtn = document.getElementById("runBtn");
const stepNav = document.getElementById("stepNav");
const stepDetail = document.getElementById("stepDetail");
const presetRow = document.getElementById("presetRow");
const runHint = document.getElementById("runHint");
const labRunSection = document.getElementById("labRunSection");
const tokenBody = document.querySelector("#tokenTable tbody");
const astView = document.getElementById("astView");
const executorList = document.getElementById("executorList");
const stdoutView = document.getElementById("stdoutView");
const stderrView = document.getElementById("stderrView");
const exitCode = document.getElementById("exitCode");
const diffView = document.getElementById("diffView");

let lab = { steps: [] };
let activeStepId = null;

function esc(s) {
  return String(s)
    .replace(/&/g, "&amp;")
    .replace(/</g, "&lt;")
    .replace(/>/g, "&gt;");
}

function getUsersCsv(snapshot) {
  return snapshot["users.csv"] || "";
}

function simpleLineDiff(before, after) {
  const a = before.split(/\r?\n/);
  const b = after.split(/\r?\n/);
  const lines = [];
  const max = Math.max(a.length, b.length);
  for (let i = 0; i < max; i++) {
    const va = a[i] ?? "";
    const vb = b[i] ?? "";
    if (va === vb) {
      lines.push(`  ${va}`);
    } else {
      if (va !== "") lines.push(`- ${va}`);
      if (vb !== "") lines.push(`+ ${vb}`);
    }
  }
  return lines.join("\n");
}

function renderTrace(events, result) {
  tokenBody.innerHTML = "";
  astView.textContent = "";
  executorList.innerHTML = "";
  stdoutView.textContent = result.stdout || "";
  stderrView.textContent = result.stderr || "";
  exitCode.textContent = `exit code: ${result.exitCode}`;

  const asts = [];
  for (const ev of events) {
    if (ev.step === "lexer_tokens" && Array.isArray(ev.tokens)) {
      for (const t of ev.tokens) {
        const tr = document.createElement("tr");
        tr.innerHTML = `<td>${esc(t.kind)}</td><td>${esc(t.text)}</td><td>${esc(t.line)}</td><td>${esc(t.column)}</td>`;
        tokenBody.appendChild(tr);
      }
    }
    if (ev.step === "parser_result" && ev.ok && ev.ast) {
      asts.push(ev.ast);
    }
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

function findStep(id) {
  return lab.steps.find((s) => s.id === id);
}

function renderStepDetail(step) {
  const ulStudy = (step.studyPoints || [])
    .map((t) => `<li>${esc(t)}</li>`)
    .join("");
  const ulCheck = (step.selfCheck || [])
    .map((t) => `<li>${esc(t)}</li>`)
    .join("");

  let offlineBlock = "";
  if (step.offline && (step.terminalCommands || []).length) {
    const cmds = step.terminalCommands.map((c) => `<li><code>${esc(c)}</code></li>`).join("");
    offlineBlock = `
      <div class="offline-box">
        <h3>터미널에서 (브라우저 SQL 실행 없음)</h3>
        <p class="muted small">저장소 루트, 빌드 폴더는 환경에 맞게 바꿉니다.</p>
        <ol class="cmd-list">${cmds}</ol>
      </div>`;
  }

  stepDetail.innerHTML = `
    <h2>${esc(step.title)}</h2>
    <p>${esc(step.summary || "")}</p>
    <h3>공부 포인트</h3>
    <ul>${ulStudy || "<li class='muted'>(없음)</li>"}</ul>
    <h3>스스로 점검</h3>
    <ul>${ulCheck || "<li class='muted'>(없음)</li>"}</ul>
    ${offlineBlock}
  `;

  presetRow.innerHTML = "";
  (step.sqlPresets || []).forEach((p) => {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "preset-btn";
    b.textContent = p.label;
    b.addEventListener("click", () => {
      sqlInput.value = p.sql;
    });
    presetRow.appendChild(b);
  });

  if (step.offline) {
    runHint.textContent =
      "이 단계는 위 터미널 명령으로 검증합니다. 아래 SQL 실행은 선택 사항(빈 채로 두거나 다른 단계용 쿼리만 시험).";
    sqlInput.placeholder = "오프라인 단계 — 필요 시에만 SQL 입력";
    sqlInput.value = "";
    labRunSection.classList.add("is-offline");
  } else {
    runHint.textContent = "프리셋을 눌러 채운 뒤 실행하거나, 직접 수정해 실행하세요.";
    sqlInput.placeholder = "SQL 입력…";
    labRunSection.classList.remove("is-offline");
  }
}

function selectStep(id) {
  activeStepId = id;
  for (const btn of stepNav.querySelectorAll("button")) {
    btn.classList.toggle("is-active", Number(btn.dataset.stepId) === id);
  }
  const step = findStep(id);
  if (!step) return;
  renderStepDetail(step);
}

function stepNavLabel(step) {
  const t = step.title || "";
  const parts = t.split(/[—–]/);
  const short = (parts.length > 1 ? parts.slice(1).join("—") : t).trim();
  return short ? `${step.id}. ${short}` : `단계 ${step.id}`;
}

function buildNav() {
  stepNav.innerHTML = "";
  for (const s of lab.steps) {
    const b = document.createElement("button");
    b.type = "button";
    b.className = "step-nav-btn";
    b.dataset.stepId = String(s.id);
    b.textContent = stepNavLabel(s);
    b.addEventListener("click", () => selectStep(s.id));
    stepNav.appendChild(b);
  }
}

async function loadLabJson() {
  const urls = ["week7-lab.json", "/week7-lab.json", "/api/week7-lab"];
  let lastStatus = 0;
  for (const u of urls) {
    try {
      const res = await fetch(u, { cache: "no-store" });
      lastStatus = res.status;
      if (res.ok) {
        return await res.json();
      }
    } catch (e) {
      lastStatus = 0;
    }
  }
  const hint =
    "① demo 폴더에서 npm start 로 접속했는지(HTML을 더블클릭해 file:// 로 열면 정적 서버만 없을 수 있음). " +
    "② demo/public/week7-lab.json 이 같은 폴더에 있는지 확인하세요.";
  throw new Error(`단계 데이터 로드 실패 (마지막 HTTP ${lastStatus}). ${hint}`);
}

async function init() {
  try {
    lab = await loadLabJson();
    if (!lab || !Array.isArray(lab.steps)) {
      throw new Error("week7-lab 형식 오류: steps 배열이 없습니다.");
    }
    buildNav();
    if (lab.steps.length) {
      selectStep(lab.steps[0].id);
    }
  } catch (e) {
    stepDetail.innerHTML = `<p class="lab-load-error">${esc(String(e.message || e))}</p>`;
  }
}

runBtn.addEventListener("click", async () => {
  const step = activeStepId != null ? findStep(activeStepId) : null;
  if (step && step.offline && !sqlInput.value.trim()) {
    stderrView.textContent = "이 단계는 터미널 명령으로 검증합니다. SQL을 실행하려면 입력란에 쿼리를 적으세요.";
    stdoutView.textContent = "";
    exitCode.textContent = "exit code: —";
    return;
  }

  runBtn.disabled = true;
  try {
    const res = await fetch("/api/run", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ sql: sqlInput.value })
    });
    const data = await res.json();
    if (!res.ok) {
      throw new Error(data.error || data.hint || "run failed");
    }
    renderTrace(data.traceEvents || [], data);
  } catch (e) {
    stderrView.textContent = String(e.message || e);
    stdoutView.textContent = "";
    exitCode.textContent = "exit code: —";
  } finally {
    runBtn.disabled = false;
  }
});

init();
