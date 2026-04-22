#!/usr/bin/env python3
from __future__ import annotations

import re
from pathlib import Path

import pandas as pd
import plotly.graph_objects as go


ROOT = Path(__file__).resolve().parents[2]
DOC_PATH = ROOT / "docs" / "weeks" / "WEEK8" / "presentation" / "presentation_visuals.md"
OUT_DIR = ROOT / "artifacts" / "week8" / "presentation_plotly"

SCENARIO_ORDER = ["normal", "burst", "saturation"]
SCENARIO_LABELS = {"normal": "Normal", "burst": "Burst", "saturation": "Saturation"}

DATASET_META = {
    "02": {
        "section": "4",
        "title": "Week 8 Comparison 02",
        "subtitle": "Throughput only: fixed thread pool + bounded queue vs request-per-thread",
        "policy_order": ["pool", "per_request"],
        "policy_labels": {"pool": "Pool", "per_request": "Per-request"},
        "colors": {"pool": "#2563EB", "per_request": "#F97316"},
    },
    "06": {
        "section": "5",
        "title": "Week 8 Comparison 06",
        "subtitle": "Throughput only: dynamic timeout vs fixed timeout",
        "policy_order": ["dynamic", "fixed"],
        "policy_labels": {"dynamic": "Dynamic timeout", "fixed": "Fixed timeout"},
        "colors": {"dynamic": "#0F9D8A", "fixed": "#E25D45"},
    },
}


def extract_table(section_number: str) -> pd.DataFrame:
    text = DOC_PATH.read_text(encoding="utf-8")
    pattern = rf"## {section_number}\. .*?\n\n\| scenario \| policy .*?\n((?:\|.*\n)+)"
    match = re.search(pattern, text, flags=re.DOTALL)
    if not match:
        raise ValueError(f"table for section {section_number} not found")

    lines = [line.strip() for line in match.group(1).splitlines() if line.strip().startswith("|")]
    lines = [line for line in lines if "---" not in line]

    rows = []
    for line in lines:
        parts = [part.strip() for part in line.strip("|").split("|")]
        if len(parts) != 8 or parts[0] == "scenario":
            continue
        rows.append(
            {
                "scenario": parts[0],
                "policy": parts[1],
                "throughput_mean": float(parts[2]),
            }
        )

    df = pd.DataFrame(rows)
    df["scenario"] = pd.Categorical(df["scenario"], categories=SCENARIO_ORDER, ordered=True)
    return df.sort_values(["scenario", "policy"]).reset_index(drop=True)


def build_figure(df: pd.DataFrame, dataset_key: str) -> go.Figure:
    meta = DATASET_META[dataset_key]
    fig = go.Figure()

    for policy in meta["policy_order"]:
        part = df[df["policy"] == policy].sort_values("scenario")
        fig.add_trace(
            go.Bar(
                x=[SCENARIO_LABELS[item] for item in part["scenario"]],
                y=part["throughput_mean"],
                name=meta["policy_labels"][policy],
                marker={
                    "color": meta["colors"][policy],
                    "line": {"color": "rgba(255,255,255,0.95)", "width": 1.6},
                },
                text=[f"{value:,.0f}" for value in part["throughput_mean"]],
                textposition="outside",
                textfont={"size": 18, "color": "#334155"},
                cliponaxis=False,
                hovertemplate="%{x}<br>%{fullData.name}<br>Throughput: %{text} req/s<extra></extra>",
            )
        )

    min_val = float(df["throughput_mean"].min())
    max_val = float(df["throughput_mean"].max())
    span = max_val - min_val
    y_min = max(0, min_val - max(200, span * 0.18))
    y_max = max_val + max(150, span * 0.12)

    fig.update_layout(
        barmode="group",
        width=1600,
        height=760,
        paper_bgcolor="#F4F8FC",
        plot_bgcolor="#FFFFFF",
        margin={"l": 70, "r": 50, "t": 165, "b": 70},
        title={
            "text": f"{meta['title']}<br><span style='font-size:22px;color:#64748B'>{meta['subtitle']}</span>",
            "x": 0.03,
            "xanchor": "left",
            "y": 0.96,
            "font": {"size": 36, "color": "#0F172A"},
        },
        legend={
            "orientation": "h",
            "x": 0.05,
            "y": 1.10,
            "bgcolor": "rgba(255,255,255,0.96)",
            "bordercolor": "rgba(148,163,184,0.18)",
            "borderwidth": 1,
            "font": {"size": 16},
        },
        font={"family": "Segoe UI, Arial, sans-serif", "color": "#1E293B"},
    )

    fig.update_xaxes(
        tickfont={"size": 16, "color": "#475569"},
        showgrid=False,
    )
    fig.update_yaxes(
        title_text="Throughput (req/s)",
        tickformat=",",
        tickfont={"size": 14, "color": "#64748B"},
        title_font={"size": 22, "color": "#172033"},
        gridcolor="rgba(148, 163, 184, 0.16)",
        zeroline=False,
        range=[y_min, y_max],
    )

    return fig


def main() -> None:
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for key, meta in DATASET_META.items():
        df = extract_table(meta["section"])
        fig = build_figure(df, key)
        fig.write_image(OUT_DIR / f"week8_plotly_{key}.png", scale=2)
        fig.write_html(OUT_DIR / f"week8_plotly_{key}.html", include_plotlyjs="cdn")
        print(f"wrote week8_plotly_{key}.png")


if __name__ == "__main__":
    main()
