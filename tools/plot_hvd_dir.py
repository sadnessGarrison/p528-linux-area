#!/usr/bin/env python3
"""Plot Equivalent Height H2 vs Distance from one or more p528-hvd output directories."""

import argparse
import sys
import csv
import os
import glob
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
POWER_COLORS = {
    27: "tab:blue",
    30: "tab:orange",
    37: "tab:green",
    40: "tab:red",
}


def get_transmit_power_label(directory):
    """Return the top-level output directory name (for example, 27dBm)."""
    directory_name = os.path.basename(os.path.abspath(directory))
    parent_name = os.path.basename(os.path.dirname(os.path.abspath(directory)))

    if directory_name.startswith("equivalent_") and parent_name:
        return parent_name

    return directory_name


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


def format_transmit_power_label(label):
    """Insert a space between the numeric power and dBm."""
    if label.endswith("dBm") and not label.endswith(" dBm"):
        return f"{label[:-3]} dBm"

    return label


def parse_transmit_power_dbm(label):
    """Return the numeric dBm value when the label is of the form 27dBm or 27 dBm."""
    normalized_label = label.replace(" ", "")
    if normalized_label.endswith("dBm"):
        try:
            return int(round(float(normalized_label[:-3])))
        except ValueError:
            return None

    return None


def parse_frequency_mhz(value):
    """Return a numeric frequency for sorting when possible."""
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def format_frequency_label(freq, freq_mhz):
    """Return a frequency label without decimal places when numeric."""
    return f"{format_whole_number(freq) if freq_mhz is not None else freq} MHz"


def is_in_old_subdir(path):
    """Return True when the path contains an 'old' directory component."""
    return "old" in os.path.normpath(path).split(os.sep)


def build_argument_parser():
    """Create the command-line parser."""
    parser = argparse.ArgumentParser(
        description="Plot Equivalent Height H2 vs Distance from one or more p528-hvd directories."
    )
    parser.add_argument("directories", nargs="+", help="One or more directories containing p528-hvd CSV files.")
    parser.add_argument("--output", dest="output_path", help="Output PNG path.")
    parser.add_argument("--frequency", dest="frequency_mhz", type=float, help="Only plot curves at this frequency.")
    return parser


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


def main():
    args = build_argument_parser().parse_args()
    directories = args.directories

    if args.output_path:
        output_path = args.output_path
    elif len(directories) == 1:
        directory_name = os.path.basename(os.path.abspath(directories[0]))
        output_path = os.path.join(directories[0], f"{directory_name}_hvd.png")
    else:
        aggregate_name = os.path.basename(os.path.abspath(directories[0]))
        if args.frequency_mhz is not None:
            aggregate_name = f"{aggregate_name}_{format_whole_number(args.frequency_mhz)}MHz"
        output_path = os.path.join("apps/output", f"{aggregate_name}_aggregate_hvd.png")

    fig, ax = plt.subplots(figsize=(10, 6))
    title_params = None
    series = []

    for directory in directories:
        paths = [
            path
            for path in sorted(glob.glob(os.path.join(directory, "**", "*.csv"), recursive=True))
            if not is_in_old_subdir(path)
        ]
        if not paths:
            print(f"No CSV files found in {directory}", file=sys.stderr)
            sys.exit(1)

        transmit_power_label = format_transmit_power_label(get_transmit_power_label(directory))
        transmit_power_dbm = parse_transmit_power_dbm(transmit_power_label)

        for path in paths:
            params, distances, heights = read_hvd_csv(path)
            if not distances:
                print(f"Warning: no data in {path}", file=sys.stderr)
                continue

            if title_params is None:
                title_params = params

            freq = params.get("f__mhz", "?")
            freq_mhz = parse_frequency_mhz(freq)

            if args.frequency_mhz is not None and freq_mhz != args.frequency_mhz:
                continue

            distances_mi = [d * 0.621371 for d in distances]
            heights_ft   = [h * 3.28084 for h in heights]
            series.append((freq_mhz, freq, transmit_power_dbm, transmit_power_label, distances_mi, heights_ft))

    if title_params is None or not series:
        print("No plottable data found", file=sys.stderr)
        sys.exit(1)

    if len(directories) == 1 and args.frequency_mhz is None:
        def series_sort_key(item):
            freq_mhz = item[0]
            if freq_mhz in FREQUENCY_ORDER_MHZ:
                return (0, FREQUENCY_ORDER_MHZ.index(freq_mhz))
            if freq_mhz is not None:
                return (1, freq_mhz)
            return (2, str(item[1]))

        for freq_mhz, freq, _, _, distances_mi, heights_ft in sorted(series, key=series_sort_key):
            label = format_frequency_label(freq, freq_mhz)
            color = FREQUENCY_COLORS.get(freq_mhz)
            ax.plot(distances_mi, heights_ft, label=label, color=color)
    else:
        def series_sort_key(item):
            transmit_power_dbm = item[2]
            if transmit_power_dbm is not None:
                return (0, transmit_power_dbm)
            return (1, str(item[3]))

        for _, _, transmit_power_dbm, transmit_power_label, distances_mi, heights_ft in sorted(series, key=series_sort_key):
            color = POWER_COLORS.get(transmit_power_dbm)
            ax.plot(distances_mi, heights_ft, label=transmit_power_label, color=color)

    h1 = format_height_feet(title_params.get("h_1__meter", "?"))
    p = format_whole_number(title_params.get("p", "?"))
    target = format_whole_number(title_params.get("target_A__db", "?"))

    ax.set_xlabel("Distance (mi)")
    ax.set_ylabel("Equivalent Height H₂ (ft)")
    if len(directories) == 1 and args.frequency_mhz is None:
        transmit_power = format_transmit_power_label(get_transmit_power_label(directories[0]))
        ax.set_title(
            "Equivalent Height Required to Receive at Minimum Power\n"
            f"ITU P.528 Model, Transmitter at {h1} ft, Probability = {p}%, "
            f"Target Loss = {target} dB, Transmit Power = {transmit_power}"
        )
    else:
        frequency_label = format_frequency_label(args.frequency_mhz, args.frequency_mhz)
        ax.set_title(
            "Equivalent Height Required to Receive at Minimum Power\n"
            f"ITU P.528 Model, Transmitter at {h1} ft, Probability = {p}%, "
            f"Target Loss = {target} dB, Frequency = {frequency_label}"
        )
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    fig.savefig(output_path, dpi=150)
    print(f"Saved plot to {output_path}")


if __name__ == "__main__":
    main()
