(function () {
    var demo = window.SQL_PROCESSOR_DEMO;

    if (!demo || !Array.isArray(demo.cases) || demo.cases.length === 0) {
        document.body.innerHTML = "<p>데모 데이터가 없습니다. `make demo`를 먼저 실행하세요.</p>";
        return;
    }

    var STUDENT_SCHEMA = [
        { type: "int", field: "id", description: "학생 카드 ID" },
        { type: "string", field: "name", description: "이름" },
        { type: "int", field: "class", description: "반 정보" },
        { type: "boolean", field: "authorization", description: "입장 권한 여부 (T/F)" }
    ];

    var ENTRY_SCHEMA = [
        { type: "datetime", field: "entered_at", description: "입장 날짜 / 시간" },
        { type: "int", field: "id", description: "학생 카드 ID" }
    ];

    var STUDENT_TABLE_INFO = {
        displayName: "크래프톤 정글 교육생 정보 DB",
        logicalName: "STUDENT_CSV",
        storageLabel: "CSV 저장"
    };

    var ENTRY_TABLE_INFO = {
        displayName: "302호 출입 기록 LOG",
        logicalName: "ENTRY_LOG_BIN",
        storageLabel: "Binary 저장"
    };

    var state = {
        caseIndex: 0
    };

    var refs = {
        summaryCards: document.getElementById("summary-cards"),
        overviewLink: document.getElementById("overview-link"),
        samplesLink: document.getElementById("samples-link"),
        caseList: document.getElementById("case-list"),
        caseId: document.getElementById("case-id"),
        caseTitle: document.getElementById("case-title"),
        casePurpose: document.getElementById("case-purpose"),
        caseBadges: document.getElementById("case-badges"),
        metricStrip: document.getElementById("metric-strip"),
        commandBlock: document.getElementById("command-block"),
        sqlBlock: document.getElementById("sql-block"),
        studentDefinitionPanel: document.getElementById("student-definition-panel"),
        entryDefinitionPanel: document.getElementById("entry-definition-panel"),
        studentStatePanel: document.getElementById("student-state-panel"),
        entryStatePanel: document.getElementById("entry-state-panel"),
        stdoutBlock: document.getElementById("stdout-block"),
        stderrBlock: document.getElementById("stderr-block"),
        stdoutFileLink: document.getElementById("stdout-file-link"),
        stderrFileLink: document.getElementById("stderr-file-link"),
        artifactLinks: document.getElementById("artifact-links")
    };

    function createElement(tagName, className, textContent) {
        var element = document.createElement(tagName);

        if (className) {
            element.className = className;
        }

        if (typeof textContent === "string") {
            element.textContent = textContent;
        }

        return element;
    }

    function firstLine(text) {
        if (!text || !text.trim()) {
            return "비어 있음";
        }

        return text.trim().split("\n")[0];
    }

    function caseToneClass(demoCase) {
        return demoCase.exitCode === 0 ? "tone-success" : "tone-error";
    }

    function buildSummaryCards() {
        var successCount = demo.cases.filter(function (demoCase) {
            return demoCase.exitCode === 0;
        }).length;
        var failureCount = demo.cases.length - successCount;
        var cards = [
            { label: "전체 케이스", value: String(demo.cases.length) },
            { label: "성공 케이스", value: String(successCount) },
            { label: "실패 케이스", value: String(failureCount) },
            { label: "발표 시작 파일", value: "web_demo/index.html" }
        ];

        refs.summaryCards.innerHTML = "";

        cards.forEach(function (card) {
            var wrapper = createElement("div", "summary-card");

            wrapper.appendChild(createElement("div", "", card.label));
            wrapper.appendChild(createElement("strong", "", card.value));
            refs.summaryCards.appendChild(wrapper);
        });
    }

    function renderCaseList() {
        refs.caseList.innerHTML = "";

        demo.cases.forEach(function (demoCase, index) {
            var button = createElement("button", "case-card", "");
            var top = createElement("div", "case-card-top");
            var name = createElement("div", "case-card-name", demoCase.shortLabel + " " + demoCase.title);
            var statusDot = createElement("span", "case-status-dot " + caseToneClass(demoCase));
            var purpose = createElement("div", "case-card-purpose", demoCase.purpose);

            button.type = "button";
            if (index === state.caseIndex) {
                button.classList.add("is-active");
            }

            top.appendChild(name);
            top.appendChild(statusDot);
            button.appendChild(top);
            button.appendChild(purpose);

            button.addEventListener("click", function () {
                state.caseIndex = index;
                renderCaseList();
                renderCurrentCase();
            });

            refs.caseList.appendChild(button);
        });
    }

    function appendBadge(text) {
        refs.caseBadges.appendChild(createElement("span", "status-pill", text));
    }

    function renderCaseBadges(demoCase) {
        refs.caseBadges.innerHTML = "";
        appendBadge("종료 코드 " + demoCase.exitCode);
        appendBadge(demoCase.exitCode === 0 ? "실행 성공" : "실행 실패");
        appendBadge("학생 행 " + demoCase.studentCsv.rows.length + "개");
        appendBadge("입장 로그 행 " + demoCase.entryLog.rows.length + "개");
    }

    function renderMetrics(demoCase) {
        var metrics = [
            { label: "표준 출력 첫 줄", value: firstLine(demoCase.stdoutText) },
            { label: "표준 에러 첫 줄", value: firstLine(demoCase.stderrText) },
            { label: "student.csv 상태", value: demoCase.studentCsv.present ? "생성됨" : "없음" },
            { label: "entry_log.bin 상태", value: demoCase.entryLog.present ? demoCase.entryLog.sizeBytes + "바이트" : "없음" }
        ];

        refs.metricStrip.innerHTML = "";

        metrics.forEach(function (metric) {
            var card = createElement("div", "metric-card");

            card.appendChild(createElement("div", "", metric.label));
            card.appendChild(createElement("div", "metric-value", metric.value));
            refs.metricStrip.appendChild(card);
        });
    }

    function buildSchemaTable(schemaRows) {
        var table = createElement("table", "schema-table");
        var thead = document.createElement("thead");
        var headerRow = document.createElement("tr");
        var tbody = document.createElement("tbody");

        ["타입", "필드", "설명"].forEach(function (label) {
            headerRow.appendChild(createElement("th", "", label));
        });

        schemaRows.forEach(function (row) {
            var tr = document.createElement("tr");

            tr.appendChild(createElement("td", "", row.type));
            tr.appendChild(createElement("td", "", row.field));
            tr.appendChild(createElement("td", "", row.description));
            tbody.appendChild(tr);
        });

        thead.appendChild(headerRow);
        table.appendChild(thead);
        table.appendChild(tbody);
        return table;
    }

    function buildDataTable(columnLabels, rowKeys, rows, emptyMessage) {
        var table = createElement("table", "data-table");
        var thead = document.createElement("thead");
        var headerRow = document.createElement("tr");
        var tbody = document.createElement("tbody");

        columnLabels.forEach(function (label) {
            headerRow.appendChild(createElement("th", "", label));
        });

        if (rows.length === 0) {
            var emptyRow = document.createElement("tr");
            var emptyCell = createElement("td", "empty-row", emptyMessage);

            emptyCell.colSpan = columnLabels.length;
            emptyRow.appendChild(emptyCell);
            tbody.appendChild(emptyRow);
        } else {
            rows.forEach(function (row) {
                var tr = document.createElement("tr");

                rowKeys.forEach(function (key) {
                    tr.appendChild(createElement("td", "", String(row[key])));
                });

                tbody.appendChild(tr);
            });
        }

        thead.appendChild(headerRow);
        table.appendChild(thead);
        table.appendChild(tbody);
        return table;
    }

    function buildMetaRow(metaTexts) {
        var metaRow = createElement("div", "table-meta-row");

        metaTexts.forEach(function (text) {
            metaRow.appendChild(createElement("span", "table-meta", text));
        });

        return metaRow;
    }

    function renderTableCard(container, config) {
        var titleBar = createElement("div", "mini-title-bar");
        var body = createElement("div", "table-body");

        container.innerHTML = "";
        titleBar.appendChild(createElement("div", "mini-title-text", config.displayName));
        body.appendChild(buildMetaRow(config.metaTexts));
        body.appendChild(config.tableNode);
        container.appendChild(titleBar);
        container.appendChild(body);
    }

    function renderDefinitions() {
        renderTableCard(
            refs.studentDefinitionPanel,
            {
                displayName: STUDENT_TABLE_INFO.displayName,
                metaTexts: [STUDENT_TABLE_INFO.logicalName, STUDENT_TABLE_INFO.storageLabel],
                tableNode: buildSchemaTable(STUDENT_SCHEMA)
            }
        );

        renderTableCard(
            refs.entryDefinitionPanel,
            {
                displayName: ENTRY_TABLE_INFO.displayName,
                metaTexts: [ENTRY_TABLE_INFO.logicalName, ENTRY_TABLE_INFO.storageLabel],
                tableNode: buildSchemaTable(ENTRY_SCHEMA)
            }
        );
    }

    function renderStates(demoCase) {
        renderTableCard(
            refs.studentStatePanel,
            {
                displayName: STUDENT_TABLE_INFO.displayName,
                metaTexts: [
                    STUDENT_TABLE_INFO.logicalName,
                    demoCase.studentCsv.present ? "student.csv 생성됨" : "student.csv 없음"
                ],
                tableNode: buildDataTable(
                    ["id", "name", "class", "authorization"],
                    ["id", "name", "class", "authorization"],
                    demoCase.studentCsv.rows,
                    demoCase.studentCsv.present ? "저장된 행이 없습니다." : "아직 파일이 생성되지 않았습니다."
                )
            }
        );

        renderTableCard(
            refs.entryStatePanel,
            {
                displayName: ENTRY_TABLE_INFO.displayName,
                metaTexts: [
                    ENTRY_TABLE_INFO.logicalName,
                    demoCase.entryLog.present ? "entry_log.bin 생성됨" : "entry_log.bin 없음"
                ],
                tableNode: buildDataTable(
                    ["entered_at", "id"],
                    ["entered_at", "id"],
                    demoCase.entryLog.rows,
                    demoCase.entryLog.present ? "저장된 행이 없습니다." : "아직 파일이 생성되지 않았습니다."
                )
            }
        );
    }

    function renderArtifacts(demoCase) {
        refs.artifactLinks.innerHTML = "";

        demoCase.artifacts.forEach(function (artifact) {
            var link = createElement("a", "artifact-link", artifact.label);
            link.href = artifact.href;
            refs.artifactLinks.appendChild(link);
        });

        refs.artifactLinks.appendChild(createElement(
            "p",
            "helper-text",
            "표와 표준 출력으로 먼저 보여 주고, 필요할 때만 raw file을 눌러 확인하면 된다."
        ));
    }

    function setRawLink(linkElement, href) {
        if (href) {
            linkElement.hidden = false;
            linkElement.href = href;
            linkElement.textContent = "원본 파일";
        } else {
            linkElement.hidden = true;
            linkElement.removeAttribute("href");
        }
    }

    function renderOutputs(demoCase) {
        refs.stdoutBlock.textContent = demoCase.stdoutText || "비어 있음";
        refs.stderrBlock.textContent = demoCase.stderrText || "비어 있음";
        setRawLink(refs.stdoutFileLink, demoCase.links.stdout);
        setRawLink(refs.stderrFileLink, demoCase.links.stderr);
    }

    function renderCurrentCase() {
        var demoCase = demo.cases[state.caseIndex];

        refs.caseId.textContent = demoCase.shortLabel;
        refs.caseTitle.textContent = demoCase.title;
        refs.casePurpose.textContent = demoCase.purpose;
        refs.commandBlock.textContent = demoCase.command;
        refs.sqlBlock.textContent = demoCase.sqlText || "비어 있음";

        renderCaseBadges(demoCase);
        renderMetrics(demoCase);
        renderStates(demoCase);
        renderOutputs(demoCase);
        renderArtifacts(demoCase);
    }

    function copyText(text) {
        if (navigator.clipboard && navigator.clipboard.writeText) {
            return navigator.clipboard.writeText(text);
        }

        return new Promise(function (resolve, reject) {
            var textarea = document.createElement("textarea");

            textarea.value = text;
            textarea.setAttribute("readonly", "readonly");
            textarea.style.position = "fixed";
            textarea.style.top = "-9999px";
            document.body.appendChild(textarea);
            textarea.select();

            try {
                if (document.execCommand("copy")) {
                    resolve();
                } else {
                    reject(new Error("copy failed"));
                }
            } catch (error) {
                reject(error);
            } finally {
                document.body.removeChild(textarea);
            }
        });
    }

    function wireCopyButtons() {
        Array.prototype.forEach.call(document.querySelectorAll(".copy-button"), function (button) {
            button.addEventListener("click", function () {
                var targetId = button.getAttribute("data-copy-target");
                var target = document.getElementById(targetId);
                var originalText = button.textContent;

                if (!target) {
                    return;
                }

                copyText(target.textContent).then(function () {
                    button.textContent = "복사됨";
                    button.classList.add("is-copied");
                    window.setTimeout(function () {
                        button.textContent = originalText;
                        button.classList.remove("is-copied");
                    }, 1200);
                }).catch(function () {
                    button.textContent = "복사 실패";
                    window.setTimeout(function () {
                        button.textContent = originalText;
                    }, 1200);
                });
            });
        });
    }

    refs.overviewLink.href = demo.links.overview;
    refs.samplesLink.href = demo.links.manualSamplesReadme;

    buildSummaryCards();
    renderCaseList();
    renderDefinitions();
    renderCurrentCase();
    wireCopyButtons();

    document.title = demo.title || "SQL 처리기 발표 데모";
})();
