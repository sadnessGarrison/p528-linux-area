#!/usr/bin/env python3
"""Plot Equivalent Height H2 vs Distance from one or more p528-hvd CSV output files."""

import sys
import csv
import os
import matplotlib.pyplot as plt


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
                # Skip the column header row
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
                # Collect input parameters from the header section
                if len(row) >= 2:
                    params[row[0]] = row[1]

    return params, distances, heights


def main():
    paths = sys.argv[1:]
    if not paths:
        print(f"Usage: {os.path.basename(sys.argv[0])} <csv_file> [<csv_file> ...]")
        sys.exit(1)

    fig, ax = plt.subplots(figsize=(10, 6))

    for path in paths:
        params, distances, heights = read_hvd_csv(path)
        if not distances:
            print(f"Warning: no data in {path}", file=sys.stderr)
            continue

        freq = params.get("f__mhz", "?")
        h1   = params.get("h_1__meter", "?")
        p    = params.get("p", "?")
        target = params.get("target_A__db", "?")
        label = f"{freq} MHz  h₁={h1} m  p={p}%  target={target} dB"

        distances_mi = [d * 0.621371 for d in distances]
        heights_ft   = [h * 3.28084 for h in heights]
        ax.plot(distances_mi, heights_ft, label=label)

    ax.set_xlabel("Distance (mi)")
    ax.set_ylabel("Equivalent Height H₂ (ft)")
    ax.set_title("p528-hvd: Minimum Required Height vs Distance")
    ax.legend()
    ax.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
