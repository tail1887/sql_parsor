const express = require("express");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const app = express();
const PORT = 4010;
const repoRoot = path.resolve(__dirname, "..");
const dataDir = path.join(repoRoot, "data");

app.use(express.json({ limit: "1mb" }));
app.use(express.static(path.join(__dirname, "public")));

function traceExecutableCandidates(root) {
  const win = process.platform === "win32";
  const names = win ? ["sql_processor_trace.exe"] : ["sql_processor_trace"];
  const out = [];
  const baseDirs = ["build-gcc", "build-ninja", "build"];
  for (const dir of baseDirs) {
    for (const name of names) {
      out.push(path.join(root, dir, name));
    }
    for (const cfg of ["Release", "Debug", "RelWithDebInfo"]) {
      for (const name of names) {
        out.push(path.join(root, dir, cfg, name));
      }
    }
  }
  return out;
}

function findTraceExecutable(root) {
  for (const p of traceExecutableCandidates(root)) {
    if (fs.existsSync(p)) {
      return p;
    }
  }
  return null;
}

function ensureDataDir() {
  if (!fs.existsSync(dataDir)) {
    fs.mkdirSync(dataDir, { recursive: true });
  }
}

function readCsvSnapshot() {
  ensureDataDir();
  const out = {};
  const names = fs.readdirSync(dataDir).filter((n) => n.toLowerCase().endsWith(".csv"));
  for (const name of names) {
    out[name] = fs.readFileSync(path.join(dataDir, name), "utf8");
  }
  return out;
}

function parseJsonLines(content) {
  const events = [];
  const lines = content.split(/\r?\n/).filter((x) => x.trim().length > 0);
  for (const line of lines) {
    try {
      events.push(JSON.parse(line));
    } catch (e) {
      events.push({ step: "trace_parse_error", raw: line });
    }
  }
  return events;
}

const EXAMPLES_WEEK6 = [
  {
    id: "insert_select_all",
    label: "INSERT + SELECT * (id 자동)",
    sql: "INSERT INTO users VALUES ('pipeline_demo', 'pipeline@example.com');\nSELECT * FROM users;\n"
  },
  {
    id: "select_columns",
    label: "SELECT 컬럼 지정",
    sql: "SELECT id, email FROM users;\n"
  },
  {
    id: "parse_error",
    label: "파싱 에러 예시",
    sql: "SELEC * FROM users;\n"
  }
];

const EXAMPLES_WEEK7 = [
  {
    id: "auto_pk_short_insert",
    label: "자동 PK (id 제외, name·email만)",
    sql: "INSERT INTO users VALUES ('week7_demo', 'week7@example.com');\nSELECT * FROM users;\n"
  },
  {
    id: "where_id_indexed",
    label: "WHERE id = (단일 행)",
    sql: "SELECT id, name, email FROM users WHERE id = 1;\n"
  },
  {
    id: "full_scan_compare",
    label: "전체 스캔 SELECT",
    sql: "SELECT id, name FROM users;\n"
  },
  {
    id: "where_parse_error",
    label: "지원하지 않는 WHERE (구문 오류)",
    sql: "SELECT * FROM users WHERE email = 'x@y.com';\n"
  }
];

const WEEK7_LAB_PATH = path.join(__dirname, "public", "week7-lab.json");

function loadWeek7Lab() {
  try {
    const raw = fs.readFileSync(WEEK7_LAB_PATH, "utf8");
    return JSON.parse(raw);
  } catch (e) {
    console.error("week7-lab.json:", e.message);
    return { docHint: "", steps: [] };
  }
}

const WEEK7_LAB = loadWeek7Lab();
const SAFE_COMMAND_PREFIXES = [
  "cmake --build ",
  "ctest --test-dir ",
  ".\\build-ninja\\",
  "./build-ninja/"
];

app.get("/api/week7-lab", (req, res) => {
  return res.json(WEEK7_LAB);
});

app.get("/api/examples", (req, res) => {
  const week = String(req.query.week || "6");
  if (week === "7") {
    return res.json({ examples: EXAMPLES_WEEK7 });
  }
  return res.json({ examples: EXAMPLES_WEEK6 });
});

app.post("/api/run", (req, res) => {
  const sql = typeof req.body?.sql === "string" ? req.body.sql : "";
  if (!sql.trim()) {
    return res.status(400).json({ error: "sql is required" });
  }
  const traceExe = findTraceExecutable(repoRoot);
  if (!traceExe) {
    return res.status(500).json({
      error: "trace executable not found",
      hint: "Build target sql_processor_trace (e.g. cmake --build build-ninja or build-gcc)"
    });
  }

  const before = readCsvSnapshot();
  const tempDir = fs.mkdtempSync(path.join(os.tmpdir(), "sql-parsor-demo-"));
  const sqlPath = path.join(tempDir, "input.sql");
  const tracePath = path.join(tempDir, "trace.jsonl");
  fs.writeFileSync(sqlPath, sql, "utf8");

  const result = spawnSync(traceExe, [sqlPath, tracePath], {
    cwd: repoRoot,
    encoding: "utf8"
  });

  const after = readCsvSnapshot();
  const traceRaw = fs.existsSync(tracePath) ? fs.readFileSync(tracePath, "utf8") : "";
  const events = parseJsonLines(traceRaw);

  return res.json({
    exitCode: typeof result.status === "number" ? result.status : 3,
    stdout: result.stdout || "",
    stderr: result.stderr || "",
    traceEvents: events,
    csvBefore: before,
    csvAfter: after
  });
});

app.post("/api/run-command", (req, res) => {
  const command = typeof req.body?.command === "string" ? req.body.command.trim() : "";
  if (!command) {
    return res.status(400).json({ error: "command is required" });
  }
  const allowed = SAFE_COMMAND_PREFIXES.some((p) => command.startsWith(p));
  if (!allowed) {
    return res.status(400).json({ error: "command not allowed in demo" });
  }
  const result = spawnSync(command, {
    cwd: repoRoot,
    encoding: "utf8",
    shell: true
  });
  return res.json({
    exitCode: typeof result.status === "number" ? result.status : 1,
    stdout: result.stdout || "",
    stderr: result.stderr || ""
  });
});

app.listen(PORT, () => {
  console.log(`demo server: http://localhost:${PORT}`);
});
