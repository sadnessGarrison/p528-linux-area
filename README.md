# p528-linux-area

ITU-R P.528-5 propagation loss tools for a static ground terminal and a mobile
airborne terminal. Two CLI programs compute path loss or minimum receive height
over a range/height grid.

## Tools

| Binary | Purpose |
|--------|---------|
| `apps/p528-area` | Path loss along a trajectory — h₂ ascends with distance |
| `apps/p528-hvd` | Inverse — find minimum h₂ that achieves a target path loss at each distance |

---

## Dependencies

### Build (C++)

| Dependency | Notes |
|------------|-------|
| `g++` ≥ 7 (C++11) | Ubuntu: `sudo apt install build-essential` |
| `libdl` | Dynamic linking — included in `glibc`, no separate install needed |

### Plotting (Python)

| Package | Version tested | Install |
|---------|---------------|---------|
| Python | 3.12 | `sudo apt install python3` |
| `matplotlib` | 3.6 | `pip install matplotlib` |
| `numpy` | 1.26 | `pip install numpy` |
| `scipy` | 1.11 | `pip install scipy` (required by `plot_hvd_approx.py` curve fitting) |

Install all Python dependencies at once:
```bash
pip install matplotlib numpy scipy
```

---

## Build

```bash
make          # builds apps/libp528.so, apps/p528-area, apps/p528-hvd
make clean    # removes build/ and apps/
```

Requires: `g++` (C++11), `libdl`. Run from the repo root — no `LD_LIBRARY_PATH` needed.

---

## p528-area

Computes basic transmission loss along an airborne terminal trajectory.

```
apps/p528-area [Options]

  -h1        Height of low (ground) terminal, in meters
  -h2start   Starting height of high terminal, in meters
  -h2end     Ending height of high terminal, in meters
  -ascent    Ascent rate of high terminal, in m/s
  -startdist Starting distance, in km
  -enddist   Ending distance, in km
  -heightint Height interval between trajectory points, in meters
  -f         Frequency, in MHz
  -p         Time percentage (availability)
  -tpol      Polarization (0=horizontal, 1=vertical)
  -o         Output file name
```

**Example:**
```bash
apps/p528-area -h1 15 -h2start 1000 -h2end 15000 -ascent 10 \
    -startdist 5 -enddist 100 -heightint 500 \
    -f 450 -p 10 -tpol 0 -o trajectory.csv
```

---

## p528-hvd

For each distance step, performs a bisection search over h₂ to find the minimum
receive height at which the P.528 propagation loss does not exceed the target
for the given time percentage.

```
apps/p528-hvd [Options]

  -h1          Height of low (ground) terminal, in meters
  -f           Frequency, in MHz
  -p           Time percentage (availability)
  -tpol        Polarization (0=horizontal, 1=vertical)
  -targetloss  Target basic transmission loss, in dB
  -startdist   Starting distance, in km (must be > 0)
  -enddist     Ending distance, in km
  -distint     Distance interval between points, in km
  -o           Output file name
```

**Example:**
```bash
apps/p528-hvd -h1 2 -f 450 -p 95 -tpol 1 \
    -targetloss 150 -startdist 10 -enddist 100 -distint 0.1 \
    -o equivalent.csv
```

CSV output is written to `apps/output/<stem>_<YYYYMMDD_HHMMSS>_<freq>MHz.csv`.

---

## Plotting Tools (`tools/`)

Three Python scripts visualize the CSV output. All save PNGs at 150 dpi.

### plot_hvd_dir.py — per-power or aggregate plots from directories

```bash
# Per-power plot (all frequencies, one directory)
python3 tools/plot_hvd_dir.py apps/output/27dBm/equivalent_2m

# Aggregate plot (all powers at one frequency)
python3 tools/plot_hvd_dir.py \
    apps/output/27dBm/equivalent_2m apps/output/30dBm/equivalent_2m \
    apps/output/37dBm/equivalent_2m apps/output/40dBm/equivalent_2m \
    --frequency 450 --output apps/output/equivalent_2m_450MHz_aggregate_hvd.png
```

### plot_hvd.py — quick plot from individual CSV files

```bash
python3 tools/plot_hvd.py apps/output/27dBm/equivalent_2m/*.csv --output out.png
```

### plot_hvd_approx.py — plots from the generalized power-law approximation

No CSV files needed — curves are generated from a fitted model (R²=0.982).

```bash
# All defaults (8 per-power PNGs: 2m + 20m × 4 powers, all 4 frequencies)
python3 tools/plot_hvd_approx.py

# Add 450 MHz aggregate plots
python3 tools/plot_hvd_approx.py --aggregate-freq 450

# Use exact per-config fit (R²≥0.997 per dataset)
python3 tools/plot_hvd_approx.py --exact
```

See `.github/instructions/plotting-tools.instructions.md` for full option
reference and the standard replot command set.

---

## Model Assumptions

See `apps/output/P528_MODEL_ASSUMPTIONS.txt` for a full description of ITU-R
P.528 model assumptions (smooth Earth, propagation modes, atmosphere,
statistics) and the run parameters used to generate the included CSV datasets.

---

## Run Parameters Used in `apps/output/`

| Parameter | Value |
|-----------|-------|
| Polarization | Vertical (T_pol = 1) |
| Rx probability | 95% |
| Low terminal height h₁ | 2 m or 20 m |
| Frequencies | 150, 450, 700, 850 MHz |
| Distance range | 10 – 100 km at 0.1 km steps |
| Transmit powers | 27, 30, 37, 40 dBm |
| Target path loss | 140, 143, 150, 153 dB (respectively) |

Target loss is derived from a link budget: `A_target = Tx_dBm − Rx_sensitivity`,
where Rx sensitivity = −113 dBm.

---

## Error Codes

### p528-area (1xxx)

| Code | Constant |
|------|----------|
| 1001 | `P528ERR__UNKNOWN` |
| 1002 | `P528ERR__LIBRARY_LOADING` |
| 1004 | `P528ERR__INVALID_OPTION` |
| 1005 | `P528ERR__GETP528_FUNC_LOADING` |
| 1010 | `P528ERR__PARSE_H1_HEIGHT` |
| 1011 | `P528ERR__PARSE_H2_HEIGHT` |
| 1012 | `P528ERR__PARSE_F_FREQUENCY` |
| 1013 | `P528ERR__PARSE_D_DISTANCE` |
| 1014 | `P528ERR__PARSE_P_PERCENTAGE` |
| 1016 | `P528ERR__PARSE_TPOL_POLARIZATION` |
| 1101 | `P528ERR__VALIDATION_F` |
| 1102 | `P528ERR__VALIDATION_P` |
| 1103 | `P528ERR__VALIDATION_D` |
| 1104 | `P528ERR__VALIDATION_H1` |
| 1105 | `P528ERR__VALIDATION_H2` |
| 1106 | `P528ERR__VALIDATION_OUT_FILE` |
| 1107 | `P528ERR__MEMORY_ALLOCATION` |
| 1108 | `P528ERR__INVALID_DISTANCE_RANGE` |
| 1109 | `P528ERR__INVALID_HEIGHT_RANGE` |
| 1110 | `P528ERR__INVALID_ASCENT_RATE` |

### p528-hvd (2xxx)

| Code | Constant |
|------|----------|
| 2001 | `HVDERR__UNKNOWN` |
| 2002 | `HVDERR__LIBRARY_LOADING` |
| 2003 | `HVDERR__INVALID_OPTION` |
| 2004 | `HVDERR__GETP528_FUNC_LOADING` |
| 2010 | `HVDERR__PARSE_H1_HEIGHT` |
| 2011 | `HVDERR__PARSE_F_FREQUENCY` |
| 2012 | `HVDERR__PARSE_P_PERCENTAGE` |
| 2013 | `HVDERR__PARSE_D_DISTANCE` |
| 2014 | `HVDERR__PARSE_TPOL_POLARIZATION` |
| 2015 | `HVDERR__PARSE_TARGET_LOSS` |
| 2101 | `HVDERR__VALIDATION_F` |
| 2102 | `HVDERR__VALIDATION_P` |
| 2103 | `HVDERR__VALIDATION_D` |
| 2104 | `HVDERR__VALIDATION_H1` |
| 2105 | `HVDERR__VALIDATION_OUT_FILE` |
| 2106 | `HVDERR__VALIDATION_TARGET_LOSS` |
| 2107 | `HVDERR__INVALID_DISTANCE_RANGE` |
| 0x10 | `HVDWARN__TARGET_NOT_ACHIEVABLE` — h₂ > 20 000 m required; NaN written |

---

## Repository Structure

```
p528-linux-area/
├── src/
│   ├── P528LinuxArea.cpp/.h   # p528-area driver
│   └── P528LinuxHvD.cpp/.h    # p528-hvd driver
├── submodules/
│   └── p528-linux/            # ITU-R P.528-5 reference implementation (read-only)
├── tools/
│   ├── plot_hvd.py            # Plot individual CSV files
│   ├── plot_hvd_dir.py        # Plot from output directories
│   └── plot_hvd_approx.py     # Plot from power-law approximation model
├── apps/                      # Build output (binaries, library, CSV output, PNGs)
└── Makefile
```

