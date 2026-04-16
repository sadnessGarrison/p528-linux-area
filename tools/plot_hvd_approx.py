#!/usr/bin/env python3
"""
Generate Equivalent Receive Height vs Distance plots from the P.528 HVD
power-law approximation — no CSV files required.

GENERALIZED MODEL (derived from curve-fitting all 32 p528-hvd output datasets):

  h₂(d) = exp(log_a) · d^b            [h₂ meters, d km]

  log(a) = +15.8269
           − 1.7417 · ln(h₁)
           + 1.4520 · ln(f)
           − 0.1908 · A_target

  b      = −0.5474
           + 0.2891 · ln(h₁)
           − 0.2608 · ln(f)
           + 0.0301 · A_target

  Parameters: h₁ [m], f [MHz], A_target [dB]
  Overall R² = 0.982 across 23,509 data points.

PER-CONFIG FIT TABLE  (R² ≥ 0.997 each; use --exact for these):
  Columns: h₁(m)  f(MHz)  Tx(dBm)  a          b
  2   150  27  0.00871  2.589        2   150  30  0.00556  2.631
  2   150  37  0.00327  2.630        2   150  40  0.00245  2.653
  2   450  27  0.04879  2.243        2   450  30  0.03002  2.308
  2   450  37  0.00800  2.508        2   450  40  0.00539  2.556
  2   700  27  0.03997  2.294        2   700  30  0.03219  2.298
  2   700  37  0.00876  2.502        2   700  40  0.00558  2.565
  2   850  27  0.03306  2.340        2   850  30  0.02999  2.316
  2   850  37  0.00973  2.483        2   850  40  0.00556  2.572
  20  150  27  0.00022  3.111        20  150  30  0.00007  3.319
  20  150  37  0.00000  3.850        20  150  40  0.00000  4.084
  20  450  27  0.00121  2.822        20  450  30  0.00061  2.938
  20  450  37  0.00015  3.184        20  450  40  0.00006  3.356
  20  700  27  0.00166  2.772        20  700  30  0.00101  2.848
  20  700  37  0.00034  3.021        20  700  40  0.00017  3.156
  20  850  27  0.00185  2.756        20  850  30  0.00121  2.817
  20  850  37  0.00037  3.009        20  850  40  0.00023  3.098

Usage examples:
  # Per-power plot — all frequencies at 27 dBm, h₁=2 m
  python3 tools/plot_hvd_approx.py --power 27 --h1 2

  # Aggregate plot — all powers at 450 MHz, h₁=20 m
  python3 tools/plot_hvd_approx.py --frequency 450 --h1 20

  # All combos — two per-power PNGs (2m and 20m) plus two aggregate PNGs
  python3 tools/plot_hvd_approx.py

  # Use exact per-config fit instead of generalized equation
  python3 tools/plot_hvd_approx.py --power 27 --h1 2 --exact
"""

import argparse
import math
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

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

# Generalized model coefficients
# log(a) = C0 + C1*ln(h1) + C2*ln(f) + C3*A_target
COEF_LOG_A = (15.8269, -1.7417, 1.4520, -0.1908)
# b = D0 + D1*ln(h1) + D2*ln(f) + D3*A_target
COEF_B     = (-0.5474,  0.2891, -0.2608,  0.0301)

# Per-config exact fit table: (h1_m, f_mhz, tx_dbm) -> (a, b)
EXACT_FITS = {
    (2,  150, 27): (0.00871, 2.589),  (2,  150, 30): (0.00556, 2.631),
    (2,  150, 37): (0.00327, 2.630),  (2,  150, 40): (0.00245, 2.653),
    (2,  450, 27): (0.04879, 2.243),  (2,  450, 30): (0.03002, 2.308),
    (2,  450, 37): (0.00800, 2.508),  (2,  450, 40): (0.00539, 2.556),
    (2,  700, 27): (0.03997, 2.294),  (2,  700, 30): (0.03219, 2.298),
    (2,  700, 37): (0.00876, 2.502),  (2,  700, 40): (0.00558, 2.565),
    (2,  850, 27): (0.03306, 2.340),  (2,  850, 30): (0.02999, 2.316),
    (2,  850, 37): (0.00973, 2.483),  (2,  850, 40): (0.00556, 2.572),
    (20, 150, 27): (0.00022, 3.111),  (20, 150, 30): (0.00007, 3.319),
    (20, 150, 37): (0.00000, 3.850),  (20, 150, 40): (0.00000, 4.084),
    (20, 450, 27): (0.00121, 2.822),  (20, 450, 30): (0.00061, 2.938),
    (20, 450, 37): (0.00015, 3.184),  (20, 450, 40): (0.00006, 3.356),
    (20, 700, 27): (0.00166, 2.772),  (20, 700, 30): (0.00101, 2.848),
    (20, 700, 37): (0.00034, 3.021),  (20, 700, 40): (0.00017, 3.156),
    (20, 850, 27): (0.00185, 2.756),  (20, 850, 30): (0.00121, 2.817),
    (20, 850, 37): (0.00037, 3.009),  (20, 850, 40): (0.00023, 3.098),
}

M_TO_FT  = 3.28084
KM_TO_MI = 0.621371


# ---------------------------------------------------------------------------
# Model
# ---------------------------------------------------------------------------

def a_target_from_tx(tx_dbm, rx_sensitivity_dbm):
    """Derive total allowable path loss from transmit power and Rx sensitivity."""
    return tx_dbm - rx_sensitivity_dbm


def generalized_coefficients(h1_m, f_mhz, a_target_db):
    """Return (a, b) for the power-law h₂ = a · d^b using the generalized model."""
    c0, c1, c2, c3 = COEF_LOG_A
    d0, d1, d2, d3 = COEF_B
    lnh1 = math.log(h1_m)
    lnf  = math.log(f_mhz)
    log_a = c0 + c1 * lnh1 + c2 * lnf + c3 * a_target_db
    b     = d0 + d1 * lnh1 + d2 * lnf + d3 * a_target_db
    return math.exp(log_a), b


def get_coefficients(h1_m, f_mhz, tx_dbm, a_target_db, use_exact):
    """Return (a, b), preferring the exact table when --exact is set."""
    key = (int(round(h1_m)), int(round(f_mhz)), int(round(tx_dbm)))
    if use_exact:
        if key in EXACT_FITS:
            return EXACT_FITS[key]
        print(
            f"Warning: no exact fit for h1={h1_m}m f={f_mhz}MHz tx={tx_dbm}dBm — "
            "falling back to generalized model.",
            file=sys.stderr,
        )
    return generalized_coefficients(h1_m, f_mhz, a_target_db)


def compute_curve(a, b, h1_m, start_km, end_km, step_km):
    """Return (distances_km, heights_m) applying the h₁ floor."""
    n = int(round((end_km - start_km) / step_km)) + 1
    distances, heights = [], []
    for i in range(n):
        d = start_km + i * step_km
        h = max(a * d ** b, h1_m)
        distances.append(d)
        heights.append(h)
    return distances, heights


# ---------------------------------------------------------------------------
# Formatting helpers (mirrors plot_hvd_dir.py)
# ---------------------------------------------------------------------------

def format_bold_label(text):
    return r"$\bf{" + text.replace(" ", r"\ ") + "}$"


def format_whole_number(value):
    return str(int(round(float(value))))


def format_height_feet(value_m):
    return f"{float(value_m) * M_TO_FT:.1f}"


# ---------------------------------------------------------------------------
# Plotting
# ---------------------------------------------------------------------------

def make_per_power_plot(h1_m, tx_dbm, rx_sens_dbm, frequencies, p_pct,
                        start_km, end_km, step_km, use_exact, output_path):
    """One plot: curves for each frequency at a fixed Tx power."""
    a_target = a_target_from_tx(tx_dbm, rx_sens_dbm)
    fig, ax = plt.subplots(figsize=(10, 6))

    for f_mhz in FREQUENCY_ORDER_MHZ:
        if f_mhz not in frequencies:
            continue
        a, b = get_coefficients(h1_m, f_mhz, tx_dbm, a_target, use_exact)
        dist_km, h2_m = compute_curve(a, b, h1_m, start_km, end_km, step_km)
        dist_mi = [d * KM_TO_MI for d in dist_km]
        h2_ft   = [h * M_TO_FT  for h in h2_m]
        ax.plot(dist_mi, h2_ft,
                label=f"{format_whole_number(f_mhz)} MHz",
                color=FREQUENCY_COLORS.get(f_mhz))

    h1_ft   = format_height_feet(h1_m)
    model_tag = "Exact Fit" if use_exact else "Generalized Approx"
    metadata_lines = [
        format_bold_label("P.528 HVD " + model_tag),
        f"{format_bold_label('Transmitter:')} {h1_ft} ft",
        f"{format_bold_label('Rx Probability:')} {format_whole_number(p_pct)}%",
        f"{format_bold_label('Target Loss:')} {format_whole_number(a_target)} dB",
        f"{format_bold_label('Transmit Power:')} {tx_dbm} dBm",
    ]
    title = (
        f"Equivalent Height at Same Receive Power "
        f"(Tx: {tx_dbm} dBm, {h1_ft} ft)"
    )
    _apply_style(ax, title, metadata_lines, legend_loc="lower right")
    _save(fig, output_path)


def make_aggregate_plot(h1_m, powers_dbm, rx_sens_dbm, f_mhz, p_pct,
                        start_km, end_km, step_km, use_exact, output_path):
    """One plot: curves for each Tx power at a fixed frequency."""
    fig, ax = plt.subplots(figsize=(10, 6))

    for tx_dbm in sorted(powers_dbm):
        a_target = a_target_from_tx(tx_dbm, rx_sens_dbm)
        a, b = get_coefficients(h1_m, f_mhz, tx_dbm, a_target, use_exact)
        dist_km, h2_m = compute_curve(a, b, h1_m, start_km, end_km, step_km)
        dist_mi = [d * KM_TO_MI for d in dist_km]
        h2_ft   = [h * M_TO_FT  for h in h2_m]
        ax.plot(dist_mi, h2_ft,
                label=f"{tx_dbm} dBm",
                color=POWER_COLORS.get(tx_dbm))

    h1_ft    = format_height_feet(h1_m)
    freq_lbl = f"{format_whole_number(f_mhz)} MHz"
    model_tag = "Exact Fit" if use_exact else "Generalized Approx"
    metadata_lines = [
        format_bold_label("P.528 HVD " + model_tag),
        f"{format_bold_label('Frequency:')} {freq_lbl}",
        f"{format_bold_label('Transmitter:')} {h1_ft} ft",
        f"{format_bold_label('Rx Probability:')} {format_whole_number(p_pct)}%",
    ]
    title = (
        f"Equivalent Height at Same Frequency "
        f"(Tx: {freq_lbl}, {h1_ft} ft)"
    )
    _apply_style(ax, title, metadata_lines, legend_loc="lower right")
    _save(fig, output_path)


def _apply_style(ax, title, metadata_lines, legend_loc="lower right"):
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
    ax.legend(loc=legend_loc)
    ax.grid(True, linestyle="--", alpha=0.5)
    plt.tight_layout()


def _save(fig, path):
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"Saved: {path}")


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def build_argument_parser():
    parser = argparse.ArgumentParser(
        description=(
            "Generate Equivalent Receive Height vs Distance plots from the "
            "P.528 HVD power-law approximation."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--h1", type=float, nargs="+", default=[2.0, 20.0],
        metavar="METERS",
        help="Transmitter height(s) in meters (default: 2.0 20.0)",
    )
    parser.add_argument(
        "--power", type=int, nargs="+", default=[27, 30, 37, 40],
        metavar="DBM",
        help="Transmit power(s) in dBm (default: 27 30 37 40)",
    )
    parser.add_argument(
        "--frequency", type=float, nargs="+", default=FREQUENCY_ORDER_MHZ,
        metavar="MHZ",
        help="Frequency(ies) in MHz (default: 150 450 700 850)",
    )
    parser.add_argument(
        "--rx-sensitivity", type=float, default=-113.0,
        metavar="DBM",
        help="Receiver sensitivity in dBm used to compute A_target = Tx − Rx_sens (default: -113)",
    )
    parser.add_argument(
        "--p", type=float, default=95.0,
        metavar="PCT",
        help="Time-availability probability %% — display only (default: 95)",
    )
    parser.add_argument(
        "--start-dist", type=float, default=10.0, metavar="KM",
        help="Start distance in km (default: 10.0)",
    )
    parser.add_argument(
        "--end-dist", type=float, default=100.0, metavar="KM",
        help="End distance in km (default: 100.0)",
    )
    parser.add_argument(
        "--dist-step", type=float, default=0.1, metavar="KM",
        help="Distance step in km (default: 0.1)",
    )
    parser.add_argument(
        "--aggregate-freq", type=float, nargs="+",
        metavar="MHZ",
        help=(
            "Generate aggregate plot(s) at these frequency(ies): one curve per "
            "Tx power, all powers on one figure. Can be combined with per-power plots."
        ),
    )
    parser.add_argument(
        "--output-dir", default="apps/output/approx",
        metavar="DIR",
        help="Directory for output PNGs (default: apps/output/approx)",
    )
    parser.add_argument(
        "--exact", action="store_true",
        help=(
            "Use per-config exact fit table instead of the generalized equation. "
            "Falls back to generalized for configs not in the table."
        ),
    )
    return parser


def default_per_power_path(output_dir, h1_m, tx_dbm):
    h1_tag = f"{int(round(h1_m))}m"
    return os.path.join(output_dir, f"approx_{tx_dbm}dBm_{h1_tag}.png")


def default_aggregate_path(output_dir, h1_m, f_mhz):
    h1_tag = f"{int(round(h1_m))}m"
    f_tag  = f"{int(round(f_mhz))}MHz"
    return os.path.join(output_dir, f"approx_{f_tag}_aggregate_{h1_tag}.png")


def main():
    args = build_argument_parser().parse_args()

    frequencies_set = set(args.frequency)
    any_plot = False

    for h1_m in args.h1:
        # Per-power plots (one per Tx dBm, multiple frequency curves)
        if len(args.power) > 0 and not (
            len(args.power) == 0 and args.aggregate_freq
        ):
            for tx_dbm in args.power:
                out = default_per_power_path(args.output_dir, h1_m, tx_dbm)
                make_per_power_plot(
                    h1_m=h1_m,
                    tx_dbm=tx_dbm,
                    rx_sens_dbm=args.rx_sensitivity,
                    frequencies=frequencies_set,
                    p_pct=args.p,
                    start_km=args.start_dist,
                    end_km=args.end_dist,
                    step_km=args.dist_step,
                    use_exact=args.exact,
                    output_path=out,
                )
                any_plot = True

        # Aggregate plots (one per frequency, multiple power curves)
        if args.aggregate_freq:
            for f_mhz in args.aggregate_freq:
                out = default_aggregate_path(args.output_dir, h1_m, f_mhz)
                make_aggregate_plot(
                    h1_m=h1_m,
                    powers_dbm=args.power,
                    rx_sens_dbm=args.rx_sensitivity,
                    f_mhz=f_mhz,
                    p_pct=args.p,
                    start_km=args.start_dist,
                    end_km=args.end_dist,
                    step_km=args.dist_step,
                    use_exact=args.exact,
                    output_path=out,
                )
                any_plot = True

    if not any_plot:
        print("No plots generated. Provide --power and/or --aggregate-freq.", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
