#!/usr/bin/env python3
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
import seaborn as sns


SCENARIO_ORDER = ["normal", "burst", "saturation"]
SCENARIO_LABEL = {"normal": "Normal", "burst": "Burst", "saturation": "Saturation"}
POLICY_ORDER = ["pool", "per_request"]
POLICY_LABEL = {"pool": "A: pool", "per_request": "B: per_request"}
POLICY_COLOR = {"pool": "#4C78A8", "per_request": "#F58518"}
METRICS = [
    ("throughput_rps", "Throughput (req/s)", "throughput_02.png", False),
    ("p95_ms", "p95 Latency (ms)", "p95_02.png", True),
    ("error_503_ratio", "503 Ratio", "error503_02.png", True),
]


def load_csv(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    required = {
        "scenario",
        "policy",
        "run",
        "requests",
        "concurrency",
        "throughput_rps",
        "p95_ms",
        "error_503_ratio",
    }
    missing = required - set(df.columns)
    if missing:
        raise ValueError(f"missing columns: {sorted(missing)}")
    df = df.copy()
    df = df.dropna(subset=["scenario", "policy"])
    for col in ["throughput_rps", "p95_ms", "error_503_ratio"]:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna(subset=["throughput_rps", "p95_ms", "error_503_ratio"])
    df["scenario"] = pd.Categorical(df["scenario"], categories=SCENARIO_ORDER, ordered=True)
    df["scenario_label"] = df["scenario"].map(SCENARIO_LABEL)
    df["policy"] = pd.Categorical(df["policy"], categories=POLICY_ORDER, ordered=True)
    return df.sort_values(["scenario", "policy", "run"])


def aggregate_mean_std(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    agg = (
        df.groupby(["scenario", "policy"], observed=False)[metric]
        .agg(["mean", "std"])
        .reset_index()
    )
    agg["std"] = agg["std"].fillna(0.0)
    return agg.sort_values(["scenario", "policy"])


def format_value(metric: str, value: float) -> str:
    if metric == "error_503_ratio":
        return f"{value * 100:.2f}%"
    if metric == "throughput_rps":
        return f"{value:.0f}"
    return f"{value:.2f}"


def annotate_bars(ax, agg: pd.DataFrame, metric: str):
    patches = ax.patches
    for patch, (_, row) in zip(patches, agg.iterrows()):
        height = patch.get_height()
        ax.annotate(
            format_value(metric, height),
            (patch.get_x() + patch.get_width() / 2.0, height),
            ha="center",
            va="bottom",
            fontsize=9,
            xytext=(0, 4),
            textcoords="offset points",
            fontweight="bold",
        )


def add_delta_notes(ax, agg: pd.DataFrame, metric: str):
    for scenario in SCENARIO_ORDER:
        rows = agg[agg["scenario"] == scenario]
        if len(rows) != 2:
            continue
        v_a = float(rows[rows["policy"] == "pool"]["mean"].iloc[0])
        v_b = float(rows[rows["policy"] == "per_request"]["mean"].iloc[0])
        if abs(v_a) < 1e-9:
            delta_pct = 0.0
        else:
            delta_pct = (v_b - v_a) / v_a * 100.0
        idx = SCENARIO_ORDER.index(scenario)
        y_base = max(v_a, v_b)
        label = f"Δ(B-A): {delta_pct:+.1f}%"
        ax.text(idx, y_base * 1.05 if y_base > 0 else 0.01, label, ha="center", va="bottom", fontsize=9)


def plot_metric(df: pd.DataFrame, metric: str, ylabel: str, out_path: Path, lower_better: bool):
    agg = aggregate_mean_std(df, metric)
    agg["scenario_label"] = agg["scenario"].map(SCENARIO_LABEL)

    plt.figure(figsize=(9, 5.5))
    ax = sns.barplot(
        data=agg,
        x="scenario_label",
        y="mean",
        hue="policy",
        palette=POLICY_COLOR,
        errorbar=None,
    )
    ax.set_title(f"02 Benchmark — {ylabel}", fontsize=14, fontweight="bold")
    ax.set_xlabel("Scenario", fontsize=11)
    ax.set_ylabel(ylabel, fontsize=11)
    ax.grid(axis="y", linestyle="--", alpha=0.35)
    ax.legend(title="Policy", labels=[POLICY_LABEL["pool"], POLICY_LABEL["per_request"]], frameon=True)

    annotate_bars(ax, agg, metric)
    add_delta_notes(ax, agg, metric)

    note = "lower is better" if lower_better else "higher is better"
    plt.figtext(
        0.01,
        0.01,
        f"Setup: same machine/build, 3 runs per scenario, GET /health workload ({note})",
        ha="left",
        fontsize=8,
    )
    plt.tight_layout(rect=(0, 0.03, 1, 1))
    plt.savefig(out_path, dpi=180)
    plt.close()


def plot_dashboard(df: pd.DataFrame, out_path: Path):
    fig, axes = plt.subplots(1, 3, figsize=(17, 5.8))
    fig.suptitle("02 Benchmark Dashboard (A vs B)", fontsize=16, fontweight="bold")
    for ax, (metric, ylabel, _, _) in zip(axes, METRICS):
        agg = aggregate_mean_std(df, metric)
        agg["scenario_label"] = agg["scenario"].map(SCENARIO_LABEL)
        sns.barplot(
            data=agg,
            x="scenario_label",
            y="mean",
            hue="policy",
            palette=POLICY_COLOR,
            errorbar=None,
            ax=ax,
        )
        ax.set_title(ylabel, fontsize=12, fontweight="bold")
        ax.set_xlabel("Scenario")
        ax.set_ylabel(ylabel)
        ax.grid(axis="y", linestyle="--", alpha=0.3)
        annotate_bars(ax, agg, metric)
        add_delta_notes(ax, agg, metric)
        ax.get_legend().remove()

    handles, labels = axes[0].get_legend_handles_labels()
    fig.legend(handles, [POLICY_LABEL["pool"], POLICY_LABEL["per_request"]], loc="upper center", ncol=2, frameon=True)
    fig.text(
        0.01,
        0.01,
        "Setup: same machine/build, 3 runs per scenario, GET /health workload",
        ha="left",
        fontsize=9,
    )
    plt.tight_layout(rect=(0, 0.03, 1, 0.90))
    plt.savefig(out_path, dpi=180)
    plt.close()


def write_summary(df: pd.DataFrame, out_path: Path):
    agg = (
        df.groupby(["scenario", "policy"], as_index=False, observed=False)
        .agg(
            throughput_rps_mean=("throughput_rps", "mean"),
            throughput_rps_std=("throughput_rps", "std"),
            p95_ms_mean=("p95_ms", "mean"),
            p95_ms_std=("p95_ms", "std"),
            error_503_ratio_mean=("error_503_ratio", "mean"),
            error_503_ratio_std=("error_503_ratio", "std"),
        )
        .sort_values(["scenario", "policy"])
    )
    for col in ["throughput_rps_std", "p95_ms_std", "error_503_ratio_std"]:
        agg[col] = agg[col].fillna(0.0)

    lines = [
        "# Bench 02 Summary",
        "",
        "| scenario | policy | throughput_mean | throughput_std | p95_mean | p95_std | error503_mean | error503_std |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for _, row in agg.iterrows():
        lines.append(
            f"| {row['scenario']} | {row['policy']} | {row['throughput_rps_mean']:.2f} | "
            f"{row['throughput_rps_std']:.2f} | {row['p95_ms_mean']:.2f} | {row['p95_ms_std']:.2f} | "
            f"{row['error_503_ratio_mean']:.4f} | {row['error_503_ratio_std']:.4f} |"
        )
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    root = Path(__file__).resolve().parents[2]
    in_csv = root / "artifacts/week8/bench_02/benchmark_results_02.csv"
    out_dir = root / "artifacts/week8/bench_02"
    out_dir.mkdir(parents=True, exist_ok=True)

    df = load_csv(in_csv)
    sns.set_theme(style="whitegrid", context="talk")
    for metric, ylabel, filename, lower_better in METRICS:
        plot_metric(df, metric, ylabel, out_dir / filename, lower_better)
    plot_dashboard(df, out_dir / "dashboard_02.png")
    write_summary(df, out_dir / "summary_02.md")
    print(f"plots generated in {out_dir}")


if __name__ == "__main__":
    main()
