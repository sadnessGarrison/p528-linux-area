# p528-linux-area Guidelines

Two CLI tools implementing ITU-R P.528 propagation loss over a range/height grid:

- `p528-area` — path loss along a trajectory (airborne terminal ascending over a range grid)
- `p528-hvd` — inverse: find minimum h₂ that achieves a target path loss at each distance

## Architecture

| Component | Purpose |
|-----------|---------|
| `src/P528LinuxArea.cpp/.h` | p528-area: argument parsing, trajectory loop, CSV output |
| `src/P528LinuxHvD.cpp/.h` | p528-hvd: argument parsing, bisection search, CSV output |
| `submodules/p528-linux/` | ITU-R P.528-5 reference implementation — read-only (see [p528-submodule.instructions.md](instructions/p528-submodule.instructions.md)) |
| `apps/libp528.so` | Shared library compiled from submodule sources at build time |

The P.528 library is loaded at runtime via `dlopen`/`dlsym` (symbol `P528`, `extern "C"` linkage). The `dlopen` candidates list includes `apps/libp528.so`, so no `LD_LIBRARY_PATH` is needed when running from the repo root.

## Build and Run

```bash
make            # builds apps/libp528.so, apps/p528-area, apps/p528-hvd
make clean      # removes build/ and apps/
```

Run from repo root — no `LD_LIBRARY_PATH` needed:
```bash
apps/p528-area <args>
apps/p528-hvd  <args>
```

CSV output is written to `<exe-dir>/output/` (resolved via `/proc/self/exe`).
Filename format: `<stem>_<YYYYMMDD_HHMMSS>_<freq>MHz.csv`

## Naming Conventions

Follow the pseudo-LaTeX convention used throughout the codebase:
- Single underscore = subscript: `h_1`, `d_r`, `A_fs`
- Double underscore = units suffix: `h_1__meter`, `d__km`, `f__mhz`, `ascent_rate__mps`
- Match variable names to the ITU-R P.528 Recommendation text wherever possible

## Code Style

- C++11 (`-std=c++11`)
- `/*====...====*/` block comment style for function headers
- Error/warning codes as `#define` constants in the corresponding `.h` file:
  - `P528ERR__*` (1xxx range) in `P528LinuxArea.h`
  - `HVDERR__*` (2xxx range), `HVDWARN__*` in `P528LinuxHvD.h`
- `NOT_SET = -1` sentinel for uninitialized struct members; check before use
- `SUCCESS = 0`; non-zero return values propagate to `main` as the process exit code
- Pre-reserve output vectors with the expected point count for performance

## Key Pitfalls

- **`H2_MIN` constraint** (`p528-hvd`): `H2_MIN = max(1.5, h_1__meter)` — P.528 requires h₂ ≥ h₁. Bisection converges incorrectly if this floor is set below h₁.
- **Bisection range**: h₂ ∈ [H2_MIN, 20 000 m], tolerance 0.1 m. Returns `NAN` + `HVDWARN__TARGET_NOT_ACHIEVABLE` if target is unachievable.
- **`dlopen` symbol**: Must be `P528` with C linkage. Do not remove `extern "C"` from `submodules/p528-linux/src/p528/P528.cpp`.
- **Height limits**: P.528 caps h₁/h₂ at 20 000 m; higher values set warning flags but proceed.
- **Output path**: Uses `/proc/self/exe` to resolve `output/` directory — Linux only.
- **Always call via `libP528()`**: Do not call submodule internals (e.g. `LineOfSight()`) directly from driver code.
