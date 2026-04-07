# p528-linux-area Guidelines

Area-based ITU-R P.528 propagation loss calculator for a static ground terminal and a mobile airborne terminal ascending along a trajectory.

## Architecture

| Component | Purpose |
|-----------|---------|
| `src/P528LinuxArea.cpp` + `.h` | Main driver: argument parsing, trajectory calculation, CSV output |
| `submodules/p528-linux/` | ITU-R P.528 propagation model, built as `libp528.so` — treat as read-only |
| `libp528.so` | Shared library loaded at runtime via `dlopen`/`dlsym`; must be on the library path |

The airborne terminal rises from `h2_start__meter` to `h2_end__meter` at `ascent_rate__mps`, traversing the horizontal range `start_dist__km` → `end_dist__km` in steps of `distint__km`. Each point gets full P.528 path-loss computation via `LineOfSight()` (always LOS-assumed in this driver).

## Build and Run

```bash
make            # builds libp528.so and p528-area executable
make clean      # removes build artifacts, library, and executable
```

Run (library must be findable):
```bash
LD_LIBRARY_PATH=. ./p528-area <args>
```

The submodule has its own `Makefile` but is **not built independently** in this repo — the root `Makefile` compiles submodule sources directly into `libp528.so`.

## Naming Conventions

Follow the pseudo-LaTeX convention used throughout the codebase:
- Single underscore = subscript: `h_1`, `d_r`, `A_fs`
- Double underscore = units suffix: `h_1__meter`, `d__km`, `f__mhz`, `ascent_rate__mps`
- Match variable names to the ITU-R P.528 Recommendation text wherever possible
- Cite equation numbers from the Recommendation in comments when applicable

## Code Style

- C++11 (`-std=c++11`)
- Use the `/*====...====*/` block comment style for function headers (see existing functions)
- Error and warning codes are `#define` constants in `P528LinuxArea.h`; add new ones there
- `P528Params` members initialize to `NOT_SET` (-1) via in-class initializers; check before use
- `TrajectoryData` pre-reserves vectors with the expected point count for performance
- `SUCCESS` = 0; non-zero return values propagate up to `main` as the process exit code

## Key Pitfalls

- **Runtime library path**: `libp528.so` is placed in the repo root, not a system path. Always use `LD_LIBRARY_PATH=. ./p528-area` or set `rpath` during linking.
- **`dlopen` symbol name**: The exported symbol must be `P528` with C linkage. If refactoring the submodule, ensure `extern "C"` is preserved on the `P528` function in `submodules/p528-linux/src/p528/P528.cpp`.
- **Height limits**: P.528 inputs are capped at 20 000 m for `h_1` and `h_2`; higher values set warning flags but still proceed. The `P528Params` struct allows up to 80 000 m — validate before calling the library.
- **Submodule**: Do not edit files under `submodules/p528-linux/` unless intentionally updating the propagation model.
