const express = require("express");
const fs = require("fs");
const os = require("os");
const path = require("path");
const { spawnSync } = require("child_process");

const app = express();
const PORT = 4010;
const repoRoot = path.resolve(__dirname, "..");
const dataDir = path.join(repoRoot, "data");
const traceExe = path.join(repoRoot, "build-gcc", "sql_processor_trace.exe");

app.use(express.json({ limit: "1mb" }));
app.use(express.static(path.join(__dirname, "public")));

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

app.get("/api/examples", (_req, res) => {
  res.json({
    examples: [
      {
        id: "insert_select_all",
        label: "INSERT + SELECT *",
        sql: "INSERT INTO users VALUES (3, 'charlie', 'charlie@example.com');\nSELECT * FROM users;\n"
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
    ]
  });
});

app.post("/api/run", (req, res) => {
  const sql = typeof req.body?.sql === "string" ? req.body.sql : "";
  if (!sql.trim()) {
    return res.status(400).json({ error: "sql is required" });
  }
  if (!fs.existsSync(traceExe)) {
    return res.status(500).json({
      error: "trace executable not found",
      hint: "Run: cmake --build build-gcc"
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

app.listen(PORT, () => {
  console.log(`demo server: http://localhost:${PORT}`);
});
