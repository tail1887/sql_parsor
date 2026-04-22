#!/usr/bin/env python3
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd
import seaborn as sns


SCENARIO_ORDER = ["normal", "burst", "saturation"]
POLICY_ORDER = ["pool", "per_request"]
POLICY_LABEL = {"pool": "A: pool", "per_request": "B: per_request"}
POLICY_COLOR = {"pool": "#3B82F6", "per_request": "#F97316"}

METRICS = [
    ("throughput_rps", "Throughput (req/s)", False, "throughput_02_deep.png"),
    ("p95_ms", "p95 Latency (ms)", True, "p95_02_deep.png"),
    ("p99_ms", "p99 Latency (ms)", True, "p99_02_deep.png"),
    ("error_503_ratio", "503 Ratio", True, "error503_02_deep.png"),
    ("error_504_ratio", "504 Ratio", True, "error504_02_deep.png"),
    ("success_ratio", "Success Ratio", False, "success_02_deep.png"),
]


def load_csv(path: Path) -> pd.DataFrame:
    df = pd.read_csv(path)
    for col in ["throughput_rps", "p95_ms", "p99_ms", "error_503_ratio", "error_504_ratio", "success_ratio"]:
        df[col] = pd.to_numeric(df[col], errors="coerce")
    df = df.dropna()
    df["scenario"] = pd.Categorical(df["scenario"], categories=SCENARIO_ORDER, ordered=True)
    df["policy"] = pd.Categorical(df["policy"], categories=POLICY_ORDER, ordered=True)
    return df.sort_values(["scenario", "policy", "run"])


def aggregate(df: pd.DataFrame, metric: str) -> pd.DataFrame:
    out = (
        df.groupby(["scenario", "policy"], observed=False)[metric]
        .agg(["mean", "std"])
        .reset_index()
        .sort_values(["scenario", "policy"])
    )
    out["std"] = out["std"].fillna(0.0)
    return out


def annotate(ax, agg: pd.DataFrame, metric: str):
    offsets = {"pool": -0.04, "per_request": 0.04}
    for _, row in agg.iterrows():
        x = SCENARIO_ORDER.index(str(row["scenario"])) + offsets.get(str(row["policy"]), 0.0)
        y = float(row["mean"])
        if metric.endswith("_ratio"):
            label = f"{y * 100:.2f}%"
        elif metric == "throughput_rps":
            label = f"{y:.0f}"
        else:
            label = f"{y:.2f}"
        ax.annotate(
            label,
            (x, y),
            ha="center",
            va="bottom",
            fontsize=8,
            xytext=(0, 4),
            textcoords="offset points",
        )


def add_delta(ax, agg: pd.DataFrame):
    for i, scenario in enumerate(SCENARIO_ORDER):
        rows = agg[agg["scenario"] == scenario]
        if len(rows) != 2:
            continue
        a = float(rows[rows["policy"] == "pool"]["mean"].iloc[0])
        b = float(rows[rows["policy"] == "per_request"]["mean"].iloc[0])
        delta = 0.0 if abs(a) < 1e-9 else (b - a) / a * 100.0
        y = max(a, b)
        ax.text(i, y * 1.04 if y > 0 else 0.01, f"Δ {delta:+.1f}%", ha="center", va="bottom", fontsize=8)


def plot_one(df: pd.DataFrame, metric: str, title: str, filename: str):
    agg = aggregate(df, metric)
    plt.figure(figsize=(9, 5.2))
    ax = sns.lineplot(
        data=agg,
        x="scenario",
        y="mean",
        hue="policy",
        style="policy",
        markers=True,
        dashes=False,
        linewidth=2.4,
        markersize=8,
        palette=POLICY_COLOR,
        errorbar=None,
    )
    ax.set_title(f"02 Deep Benchmark — {title}", fontsize=13, fontweight="bold")
    ax.set_xlabel("Scenario")
    ax.set_ylabel(title)
    ax.set_xticks(range(len(SCENARIO_ORDER)))
    ax.set_xticklabels(["Normal", "Burst", "Saturation"])
    ax.grid(axis="y", linestyle="--", alpha=0.3)
    handles, _ = ax.get_legend_handles_labels()
    ax.legend(handles=handles, labels=[POLICY_LABEL["pool"], POLICY_LABEL["per_request"]], title="Policy")
    annotate(ax, agg, metric)
    add_delta(ax, agg)
    plt.figtext(0.01, 0.01, "Workload: /health + /query mix, 10 runs, deep stress profile", fontsize=8)
    plt.tight_layout(rect=(0, 0.03, 1, 1))
    return plt.gcf(), filename


def plot_dashboard(df: pd.DataFrame, out_path: Path):
    metrics = ["throughput_rps", "p95_ms", "p99_ms", "error_503_ratio", "error_504_ratio", "success_ratio"]
    titles = ["Throughput", "p95", "p99", "503 Ratio", "504 Ratio", "Success Ratio"]
    fig, axes = plt.subplots(2, 3, figsize=(17, 9))
    fig.suptitle("02 Deep Benchmark Dashboard (A vs B)", fontsize=16, fontweight="bold")
    for ax, metric, title in zip(axes.flat, metrics, titles):
        agg = aggregate(df, metric)
        sns.lineplot(
            data=agg,
            x="scenario",
            y="mean",
            hue="policy",
            style="policy",
            markers=True,
            dashes=False,
            linewidth=2.0,
            markersize=7,
            palette=POLICY_COLOR,
            errorbar=None,
            ax=ax,
        )
        ax.set_title(title, fontsize=11, fontweight="bold")
        ax.set_xlabel("Scenario")
        ax.set_xticks(range(len(SCENARIO_ORDER)))
        ax.set_xticklabels(["Normal", "Burst", "Saturation"], fontsize=9)
        ax.grid(axis="y", linestyle="--", alpha=0.25)
        if ax.get_legend() is not None:
            ax.get_legend().remove()
        annotate(ax, agg, metric)
        add_delta(ax, agg)
    handles, _ = axes.flat[0].get_legend_handles_labels()
    fig.legend(handles, [POLICY_LABEL["pool"], POLICY_LABEL["per_request"]], loc="upper center", ncol=2, frameon=True)
    fig.text(0.01, 0.01, "Deep workload: mixed read/write with stress scenarios, 10 runs", fontsize=9)
    plt.tight_layout(rect=(0, 0.03, 1, 0.93))
    fig.savefig(out_path, dpi=180)
    plt.close(fig)


def write_summary(df: pd.DataFrame, out_path: Path):
    agg = (
        df.groupby(["scenario", "policy"], observed=False)
        .agg(
            throughput_mean=("throughput_rps", "mean"),
            throughput_std=("throughput_rps", "std"),
            p95_mean=("p95_ms", "mean"),
            p99_mean=("p99_ms", "mean"),
            e503_mean=("error_503_ratio", "mean"),
            e504_mean=("error_504_ratio", "mean"),
            success_mean=("success_ratio", "mean"),
        )
        .reset_index()
        .sort_values(["scenario", "policy"])
    )
    lines = [
        "# Bench 02 Deep Summary",
        "",
        "| scenario | policy | throughput_mean | throughput_std | p95_mean | p99_mean | 503_mean | 504_mean | success_mean |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for _, r in agg.iterrows():
        lines.append(
            f"| {r['scenario']} | {r['policy']} | {r['throughput_mean']:.2f} | {r['throughput_std']:.2f} | "
            f"{r['p95_mean']:.2f} | {r['p99_mean']:.2f} | {r['e503_mean']:.4f} | {r['e504_mean']:.4f} | {r['success_mean']:.4f} |"
        )
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main():
    root = Path(__file__).resolve().parents[2]
    out_dir = root / "artifacts/week8/bench_02_deep"
    in_csv = out_dir / "benchmark_results_02_deep.csv"
    out_dir.mkdir(parents=True, exist_ok=True)
    df = load_csv(in_csv)
    sns.set_theme(style="whitegrid", context="talk")

    for metric, title, _lower_better, filename in METRICS:
        fig, fname = plot_one(df, metric, title, filename)
        fig.savefig(out_dir / fname, dpi=180)
        plt.close(fig)

    plot_dashboard(df, out_dir / "dashboard_02_deep.png")
    write_summary(df, out_dir / "summary_02_deep.md")
    print(f"deep plots generated in {out_dir}")


if __name__ == "__main__":
    main()
