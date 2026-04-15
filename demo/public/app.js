const demoWeek = document.body.getAttribute("data-demo-week") || "6";

const sqlInput = document.getElementById("sqlInput");
const runBtn = document.getElementById("runBtn");
const tokenBody = document.querySelector("#tokenTable tbody");
const astView = document.getElementById("astView");
const executorList = document.getElementById("executorList");
const stdoutView = document.getElementById("stdoutView");
const stderrView = document.getElementById("stderrView");
const exitCode = document.getElementById("exitCode");
const diffView = document.getElementById("diffView");
const examplesWrap = document.getElementById("examples");

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

function render(events, result) {
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

async function loadExamples() {
  const res = await fetch(`/api/examples?week=${encodeURIComponent(demoWeek)}`);
  const data = await res.json();

  for (const ex of data.examples || []) {
    const btn = document.createElement("button");
    btn.type = "button";
    btn.textContent = ex.label;
    btn.addEventListener("click", () => {
      sqlInput.value = ex.sql;
    });
    examplesWrap.appendChild(btn);
  }
}

runBtn.addEventListener("click", async () => {
  runBtn.disabled = true;
  try {
    const res = await fetch("/api/run", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ sql: sqlInput.value }),
    });
    const data = await res.json();
    if (!res.ok) {
      throw new Error(data.error || "실행에 실패했습니다");
    }
    render(data.traceEvents || [], data);
  } catch (e) {
    stderrView.textContent = String(e.message || e);
  } finally {
    runBtn.disabled = false;
  }
});

loadExamples();
