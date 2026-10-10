# ANIMA Native 0.3 Preview — Changelog

## 0.3bc-preview.1 — combined checker and VM

- Combined the previous 0.3a frontend with a substantially expanded static checker and a typed-bytecode compiler/stack VM in a single `Anima03` build.
- `run` now executes bytecode after parser and checker success; the old direct evaluator remains only for explicit `--compat 0.2` runs.
- `check` performs static validation and current bytecode code-generation validation without executing a program.
- Added `dis` instruction output and updated runtime/CLI version strings.
- Added opcode-level coverage for control flow, typed numeric operations, map construction/indexing, map key/value iteration, functions with default and named arguments, interpolation, short-circuit operators and error paths.
- Added static type-error coverage for a branch that is not taken at runtime.
- Fixed map `repeat each key, value in ...` so the VM uses ordered key/value access rather than list-style indexing.
- Updated Linux smoke script to accept `ANIMA_BIN`, so out-of-tree CMake builds can run the suite without copying binaries into source folders.
- Kept the 0.2 reference evaluator behind `--compat 0.2` for regression/migration testing.

## Still incomplete against the engineering specification

The complete 0.3b type system and the full 0.3c instruction/VM set are not claimed complete. Closures/lambdas, objects/traits/enums, modules and the full standard library, member/index assignment, complete `attempt/rescue/always` handler tables, complete UTF-8 code-point semantics, fuzzing/golden conformance and differential tests remain. 0.3d–0.3f work remains planned as stated in the specification. No AOT compiler or new GPU kernel/resource language is included in 0.3.
