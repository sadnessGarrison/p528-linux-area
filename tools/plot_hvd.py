#!/usr/bin/env python3
"""Plot Equivalent Receive Height H2 vs Distance from one or more p528-hvd CSV output files."""

import argparse
import csv
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

FREQUENCY_ORDER_MHZ = [150.0, 450.0, 700.0, 850.0]
FREQUENCY_COLORS = {
    150.0: "tab:blue",
    450.0: "tab:orange",
    700.0: "tab:green",
    850.0: "tab:red",
}


def read_hvd_csv(path):
    """Return (params dict, distances list, heights list) from a p528-hvd CSV."""
    params = {}
    distances = []
    heights = []
    in_results = False

    with open(path, newline="") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            if row[0] == "Results":
                in_results = True
                continue
            if in_results:
                if row[0] == "Distance (km)":
                    continue
                try:
                    d = float(row[0])
                    h = float(row[1])
                except (ValueError, IndexError):
                    continue
                if h != h:   # NaN — stop here
                    break
                distances.append(d)
                heights.append(h)
            else:
                if len(row) >= 2:
                    params[row[0]] = row[1]

    return params, distances, heights


def format_whole_number(value):
    """Format numeric text without decimal places."""
    try:
        return str(int(round(float(value))))
    except (TypeError, ValueError):
        return value


def format_height_feet(value):
    """Format a meter value as feet with one decimal place."""
    try:
        return f"{float(value) * 3.28084:.1f}"
    except (TypeError, ValueError):
        return value


def format_bold_label(text):
    """Return a mathtext string that renders the given text in bold."""
    return r"$\bf{" + text.replace(" ", r"\ ") + "}$"


def infer_transmit_power_label(csv_path):
    """Try to read a dBm label from the parent directory tree (e.g. 27dBm)."""
    path = os.path.abspath(csv_path)
    for part in reversed(os.path.normpath(path).split(os.sep)):
        normalized = part.replace(" ", "")
        if normalized.endswith("dBm"):
            try:
                int(float(normalized[:-3]))
                # Insert space: "27dBm" → "27 dBm"
                return f"{normalized[:-3]} dBm"
            except ValueError:
                pass
    return None


def series_sort_key(freq_mhz):
    """Sort key that puts known frequencies in preferred order."""
    if freq_mhz in FREQUENCY_ORDER_MHZ:
        return (0, FREQUENCY_ORDER_MHZ.index(freq_mhz))
    if freq_mhz is not None:
        return (1, freq_mhz)
    return (2, 0)


def build_argument_parser():
    parser = argparse.ArgumentParser(
        description="Plot Equivalent Receive Height H2 vs Distance from p528-hvd CSV files."
    )
    parser.add_argument("csv_files", nargs="+", help="One or more p528-hvd output CSV files.")
    parser.add_argument("--output", dest="output_path", help="Output PNG path.")
    return parser


def main():
    args = build_argument_parser().parse_args()

    # Collect all series data first so we can sort by frequency
    series = []
    title_params = None

    for path in args.csv_files:
        params, distances, heights = read_hvd_csv(path)
        if not distances:
            print(f"Warning: no data in {path}", file=sys.stderr)
            continue

        if title_params is None:
            title_params = params

        freq_raw = params.get("f__mhz", "?")
        try:
            freq_mhz = float(freq_raw)
        except (TypeError, ValueError):
            freq_mhz = None

        distances_mi = [d * 0.621371 for d in distances]
        heights_ft   = [h * 3.28084 for h in heights]
        series.append((freq_mhz, freq_raw, distances_mi, heights_ft, path))

    if not series:
        print("No plottable data found.", file=sys.stderr)
        sys.exit(1)

    # Determine output path
    if args.output_path:
        output_path = args.output_path
    else:
        first_dir = os.path.dirname(os.path.abspath(args.csv_files[0]))
        output_path = os.path.join(first_dir, "hvd.png")

    fig, ax = plt.subplots(figsize=(10, 6))

    for freq_mhz, freq_raw, distances_mi, heights_ft, _ in sorted(series, key=lambda x: series_sort_key(x[0])):
        label = f"{format_whole_number(freq_raw)} MHz" if freq_mhz is not None else f"{freq_raw} MHz"
        color = FREQUENCY_COLORS.get(freq_mhz)
        ax.plot(distances_mi, heights_ft, label=label, color=color)

    # Metadata from first file
    h1     = format_height_feet(title_params.get("h_1__meter", "?"))
    p      = format_whole_number(title_params.get("p", "?"))
    target = format_whole_number(title_params.get("target_A__db", "?"))
    tx_label = infer_transmit_power_label(args.csv_files[0])

    metadata_lines = [format_bold_label("ITU P.528 Model")]
    metadata_lines.append(f"{format_bold_label('Transmitter:')} {h1} ft")
    metadata_lines.append(f"{format_bold_label('Rx Probability:')} {p}%")
    metadata_lines.append(f"{format_bold_label('Target Loss:')} {target} dB")
    if tx_label:
        metadata_lines.append(f"{format_bold_label('Transmit Power:')} {tx_label}")

    if tx_label:
        title = f"Equivalent Height at Same Receive Power (Tx: {tx_label}, {h1} ft)"
    else:
        title = f"Equivalent Height at Same Receive Power ({h1} ft)"

    ax.set_xlabel("Distance (mi)")
    ax.set_ylabel("Equivalent Receive Height H₂ (ft)")
    ax.set_title(title, fontsize=16, pad=14)
    ax.text(
        0.02, 0.98,
        "\n".join(metadata_lines),
        transform=ax.transAxes,
        va="top", ha="left", fontsize=10,
        bbox={"boxstyle": "round", "facecolor": "white", "alpha": 0.9, "edgecolor": "0.7"},
    )
    ax.legend(loc="lower right")
    ax.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"Saved plot to {output_path}")


if __name__ == "__main__":
    main()
