# ANIMA Native Studio — Integrated Preview

**One native Windows distribution for ANIMA 0.2 compatibility, the 0.3 language toolchain, the native documentation app, and the D3D12 room demo.**

## Quick start

1. Extract this entire folder.
2. Run `Build-ANIMA.bat` or open `ANIMA_Studio.sln` in Visual Studio 2022.
3. Select **Debug | x64** and build the solution.
4. Run `Run-ANIMA.bat` or `x64\Debug\anima.exe`. No arguments opens the native documentation app.

## Unified launcher (`anima.exe`)

- `anima docs` — native documentation UI
- `anima room` — native Direct3D 12 room prototype
- `anima new MyGame` — generate a starter `.ani` project
- `anima doctor` — check companion executables are packaged
- `anima run Anima03Examples\fibonacci.ani` — run the 0.3 VM
- `anima check <file.ani>`, `test`, `dis`, `scene`, `ui-catalog`, `migrate`, `repl`, `self-test` — use 0.3 tooling
- `anima legacy <args...>` — use the preserved 0.2 reference runtime

The 0.3 compiler executable is also directly available as `anima-core.exe`. The legacy runtime remains separately available as `AnimaLang.exe`. This clean split keeps the old evaluator isolated while the new bytecode VM becomes the primary path.

## Projects

- **ANIMA (AnimaHub)** — one entry-point executable and starter-project creator
- **AnimaDocs** — native C++20/Win32/GDI+ documentation reader and project/resource browser
- **AnimaNative** — native Win32/Direct3D 12 room renderer with ray-tracing-style compute mode
- **AnimaLang** — 0.2 reference and compatibility runtime
- **Anima03** — 0.3 handwritten lexer/parser, AST, checker, typed bytecode compiler and stack VM

All Visual Studio executables output to `x64\Debug` / `x64\Release` with project-specific intermediate directories. The hub locates companion programs in its own output folder.

## ANIMA 0.3 feature subset in this integrated preview

Includes hand-written syntax processing, source diagnostics, control flow, typed bytecode VM, functions, list/map operations, map iteration, records with literals and enum defaults, enums and metadata, record copy-update, local declaration-only function modules, CPU parallel helpers, GPU capability/compute bridge, `scene`, `ui-catalog`, experimental migration and 0.2 compatibility. `check` verifies the current type rules and compiler support but never runs user code.

**Beyond-spec additions** in this build: `anima` unified launch/routing, `anima new` project scaffolding, `anima doctor`, single-folder output layout, module/enum/record regression tests, and a combined test/build command.

## Test status

Portable GNU C++20 build and CTest run in this environment; see `Anima03/docs/ANIMA_03_INTEGRATED_STUDIO.md` for exact results and feature boundaries. The full spec still has unsupported portions: closures/lambdas, class/trait lifecycle, complete exception unwinding, complete module exports and IO security, all type-system rules, native UI rendering, AOT compilation and a dedicated DXR pipeline. Windows MSVC and physical RTX 3050/D3D12 validation still must be run on Windows hardware. The GPU frontend is a prototype, not a production game engine.

The source specification from the project owner is stored in `Anima03/Reference/ANIMA_0.3_Engineering_Specification.docx`; it remains the compatibility contract.


## ANIMA Studio Beta GUI

`AnimaStudio.exe` is the new native launcher. It provides Home, Learn, Examples, Graphics Lab, Developer Tools and About pages. It launches the native docs app, D3D12 room demo, example programs, 0.2 reference runtime, test commands and launch media. The command-line `anima.exe` remains available for scripting and automation.

## Build and share

- For development: open `ANIMA_Studio.sln`, choose Debug or Release | x64, and build.
- To create a tester-ready portable folder and ZIP: run `Publish-ANIMA-Beta.bat` on Windows with Visual Studio 2022 and MSVC v143 installed.
- Share `dist\ANIMA-Native-Studio-0.3-Beta-Portable.zip`, not the source tree. Test the ZIP on a clean Windows machine/account before distributing.

Release projects use `/MT` to statically link the MSVC C/C++ runtime; Visual Studio is not required on the tester's PC. Windows system libraries and appropriate graphics drivers remain prerequisites.
