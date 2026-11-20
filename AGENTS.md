# Development conventions

First-party C++ is C++20 with explicit types, named callbacks and visible ownership. Do not use `auto`, lambdas, arrow member access or structured bindings. Numerical kernels use double precision without fast-math. Vendor code retains upstream style and license notices.

Preserve the classic Windows 7/10 Paint ribbon and the path, stamp, pattern and reshape tools. Test numerical and editing behavior, and inspect real rendered output. Validate each released platform independently.

Run CTest and `python3 scripts/check-style.py` before publishing changes. Keep development diaries, machine-specific receipts and working notes in the ignored `astra/` directory. Public prose must be self-contained. Credits use Author: Astra and Sponsor: Rainstar; the dedication belongs before the credits, never in a tagline.

Native development follows `docs/GUI_FORMS_PORT.md` and `scripts/native-build.sh`:
pinned standalone GUI.Forms, LLVM 22.1.8, ccache with compiler-content checks,
and the same public Application/Threading targets as PlaySuite. Use the
`check` target for CTest plus style and `paint-package` for packaging. Cache
imports never replace consumer tests. Keep toolkit checkouts and build outputs
in ignored `gui_forms/`, `.build/` and `.ccache/`; do not modify the shared toolkit.

On the configured M4 build host, run commands from the authoritative local
checkout through `~/.local/bin/m4build --`. For a configured Release tree with
`RAINSTAR_BENCHMARKS=ON`, substitute the build directory for `build/pinned`.

```sh
~/.local/bin/m4build -- cmake --build build/pinned --target paint-rapid-edit paint-forms-tests
~/.local/bin/m4build -- ctest --test-dir build/pinned --output-on-failure
~/.local/bin/m4build -- build/pinned/paint-rapid-edit numeric 2000 2000
~/.local/bin/m4build -- build/pinned/paint-forms-tests --measure-point-mesh
~/.local/bin/m4build -- build/pinned/paint-forms-tests --measure-rapid-resize
```

Run each timing candidate in a fresh process with other builds and tests idle.
