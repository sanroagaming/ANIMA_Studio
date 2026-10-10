# ANIMA Native 0.3 Beta — polish and gap matrix

This matrix prevents the beta launcher from suggesting that every planned feature is finished.

## Implemented / integrated in this preview
- Handwritten source front end, source diagnostics, AST inspection, partial static checker.
- Typed bytecode compiler and stack VM for the documented runnable subset.
- Control flow, expressions, functions supported by the subset, list/map iteration, record construction/update, enums, and constrained local-module imports.
- CLI: `version`, `tokens`, `ast`, `check`, `run`, `test`, `dis`, `scene`, `ui-catalog`, `migrate`, `repl` (preview capability varies by command), `self-test`, `doctor`, and starter scaffolding.
- Older 0.2 runtime retained for compatibility/reference.
- Native docs reader, D3D12 room prototype, unified Studio launcher, and a Release packaging script.
- Portable packaging path that statically links the MSVC runtime for Release (`/MT`).

## Partial / needs conformance work
- Complete static type rules, whole-file error recovery and diagnostic help text.
- Full function argument/default/named-argument semantics and closures/upvalues.
- Full records/classes/traits/inheritance/weak-reference/finalization behaviours.
- Complete exception handling (`attempt` / `rescue` / `always`) and cleanup semantics.
- Module exports/re-exports/cycle detection and complete standard-library modules.
- Full capability checking, filesystem project-root sandbox and deterministic mode.
- Complete legacy `.anima` and 0.2 `.ani` migrations with reports and golden UI tree equality.
- UI declaration catalogue, binding purity, event/navigation handling and deterministic scene dumps.
- Windows/MSVC CI, sanitizer/leak/fuzz/differential/performance gates.

## Explicitly not claimed in 0.3
- AOT compilation or bundling `.ani` into standalone native executables.
- A new GPU-kernel/resource-handle/compute-graph language surface.
- A native UI renderer for declarative `app/scene/node` trees.
- Async/tasks, a package manager, formatter or debugger.
- Dedicated DXR implementation in the room demo; the renderer remains a prototype and must be verified on hardware.

## Next beta gates
1. Run MSVC Release and Debug builds on Windows and preserve the logs.
2. Run the portable ZIP on a clean Windows machine without Visual Studio installed.
3. Test all launcher routes and every shipped sample; capture process exit codes and diagnostic output.
4. Add targeted conformance tests before extending classes/closures/exception unwinding.
5. Complete migration coverage and native GPU smoke testing before promoting 0.3 Beta to a wider preview.
