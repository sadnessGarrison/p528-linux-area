---
applyTo: "submodules/**"
---

The files in `submodules/p528-linux/` are a read-only upstream dependency (ITU-R P.528 reference implementation). Do not edit them unless the user explicitly states they are updating the propagation model.

- To change propagation behavior, update the calling code in `src/P528LinuxArea.cpp` or `src/P528LinuxHvD.cpp` instead.
- The submodule exposes one public entry point: `P528()` with C linkage in `submodules/p528-linux/src/p528/P528.cpp`. The `extern "C"` on that function must not be removed.
- The submodule is **not** built via its own `Makefile` in this repo — the root `Makefile` compiles its sources directly into `libp528.so`.
