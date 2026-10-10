# ANIMA Native 0.3 Integrated Preview — Scope

## Implemented in this build

- Hand-written lexer and parser; source-aware syntax diagnostics and AST/token listing.
- Supported static checking and bytecode coverage validation in `check`.
- Typed stack bytecode compiler and VM for the documented subset.
- Functions, recursion, defaults/named calls, arithmetic, control flow, list/map operations and iteration.
- Records with literal and enum defaults, field reads/writes and `with` updates.
- Enums with values and reflection helpers.
- Constrained local declaration-only module import path.
- CPU parallel helper and a native GPU-host bridge.
- `scene`, `ui-catalog`, `migrate`, `repl`, `test`, `dis`, `self-test`, and deprecated `--compat 0.2`.
- Unified native launcher, project starter, docs reader, 0.2 runtime and room prototype in the full solution.

## Not yet complete

The full spec still requires complete static checking, closures/lambdas, classes/traits/weak references/finish, full exception handling and unwinding, complete modules/exports, all 12 standard-library modules, complete capability checking and filesystem sandboxing, fully executable declarative UI and bindings, legacy tree-equivalent migration, a leak checker, broad fuzz/differential/golden tests, Windows validation and all release gates. AOT compilation, a user GPU-kernel language, native UI rendering and async/tasks are explicitly outside the ratified 0.3 scope.

## Validation snapshot

Portable GNU C++20: 84 self-tests and 14/15 CTest targets pass. The separate unified Win32 launcher is structurally packaged but could not be compiled or executed in this Linux environment. MSVC and hardware D3D12/DXR remain to be validated on Windows.
