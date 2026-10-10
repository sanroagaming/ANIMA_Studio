# ANIMA 0.3 Integrated Preview — Checker, Bytecode VM, Data and Module Slice

## Pipeline

```text
.ani source
  → hand-written lexer (tokens carry source spans)
  → parser / AST
  → static checker (supported name/type/call/scope rules)
  → typed bytecode compiler
  → stack VM
```

`anima check` runs syntax, static checks and a bytecode support pass; it never executes the user's program. `anima run` compiles and executes through the VM. `anima dis` prints instruction listings. Code that is not in the implemented subset should be rejected during checking or compilation rather than silently rerouted to the legacy evaluator.

## Implemented preview slice

- Typed `whole`/`real`, `truth`, text interpolation and numeric / comparison / short-circuit operators.
- Named functions, recursion, typed parameters, defaults and named arguments.
- Lexical block scopes, conditionals, repeat loops, ranges, `stop`/`skip`, function return and basic `fail`.
- Lists, insertion-ordered maps, indexing/slicing, length, and key/value map iteration.
- Records with literal and enum-case defaults, positional/named construction, field read/write and independent `with` updates.
- Enums with implicit/explicit integer values, typed equality/map keys, `.name`, `.value`, `.all()` and `.from_name()`.
- A constrained relative local module import path for declaration-only modules containing functions, records and enums. Imported declarations are namespaced through an alias. Module globals, nested imports and full `from ... use` remain unsupported.
- CPU core count and native parallel sum; platform and existing GPU adapter / DXR-query / small compute bridge.
- Parse-preserving UI declaration inspection, `scene`, `ui-catalog`, experimental migrator, and preserved `--compat 0.2` reference evaluator.

## Try it

```text
anima check Anima03Examples/fibonacci.ani
anima run Anima03Examples/fibonacci.ani
anima dis Anima03Examples/fibonacci.ani
anima run Anima03Examples/enums-and-records.ani
anima run Anima03Examples/Modules/module-use.ani
anima scene Anima03Examples/SpecificationTargets/ui-two-scenes.ani
anima ui-catalog Button
anima self-test
```

## Known gaps

This is not full conformance to the 46-page 0.3 spec. Closures/lambdas; all checker rules; full list/map/field mutation; classes, traits, weak references and finishers; complete exception handler/unwinding behavior; robust module graph and exports; all standard-library modules; exact Unicode code-point indexing; secure full-file I/O; full UI binding/event runtime; full `.anima` tree-equivalent migrator; leak checker, full fuzz/golden/differential test gates; native UI drawing; GPU resource handles/new shader/kernel syntax; and AOT are incomplete or not in this version.

The D3D12 adapter/compute path and DXR capability query need actual Windows/MSVC/RTX validation. A GPU capability query is not a production ray-tracing engine.

## Tests

The current portable C++20 build passes 84 self-tests and 15 CTest targets. Run `ctest --test-dir build --output-on-failure` in a CMake build, or `Anima03/Tests/Run-Smoke-Tests.bat` after a complete Windows solution build. The root launcher, project generator and D3D12 renderer require Windows to exercise.
