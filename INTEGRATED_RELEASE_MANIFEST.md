# ANIMA Native Studio 0.3 — Integrated Preview Manifest

Release channel: ANIMA Native Studio 0.3 Beta
Publisher: Unrecorded Labs

## Included in the single solution

- `AnimaStudio`: native graphical launchpad with Learn, Examples, Graphics, Tools and About pages.
- `AnimaHub`: unified console `anima.exe` launcher and starter-project generator.
- `Anima03`: current 0.3 lexer/parser, AST, checker, typed bytecode compiler, stack VM, partial native host APIs, modules, records/enums, migration and scene/catalog tooling.
- `AnimaLang`: preserved 0.2 reference/compatibility runtime.
- `AnimaNative`: native Win32/Direct3D 12 room-renderer prototype.
- `AnimaDocs`: native documentation reader, code/content, monochrome app icon, brand assets and teaser media.
- `Anima03/Reference`: the owner's 0.3 engineering specification and extracted text.

## Build on Windows

1. Extract the whole archive.
2. Open `ANIMA_Studio.sln` in Visual Studio 2022.
3. Install Desktop development with C++, MSVC v143 and Windows 10/11 SDK.
4. Select `Debug | x64` and Build Solution.
5. Run `Run-ANIMA.bat` to open the docs app or run `x64\Debug\anima.exe` directly.

`Build-ANIMA.bat` runs MSBuild, the 0.3 self-tests and the smoke suite after a successful build.

## Unified launcher

- `anima` / `anima docs`: native documentation app
- `anima room`: native Direct3D 12 room prototype
- `anima run <file.ani>` / `check` / `test` / `dis` / `scene` / `ui-catalog` / `migrate`
- `anima legacy <args>`: retained 0.2 reference runtime
- `anima new <folder>`: starter project scaffolding
- `anima doctor`: checks expected companion executables are present

## Validated in this build environment

- Portable GNU C++20 build for the 0.3 language runtime.
- 84 internal self-tests pass.
- 15/15 CTest cases pass in Debug and Release.
- Smoke suite passes: examples, CLI checks, scene/catalog, migration, and 0.2 compatibility mode.
- Visual Studio XML/project paths, root solution references, local assets and ZIP integrity checked.

## Not certified here

This environment is Linux, so the integrated Windows solution has not been compiled with MSVC. Direct3D 12 and the RTX 3050 hardware path have not been exercised on a Windows machine. The language is still a preview, not full conformance with every requirement in the supplied spec. Known gaps include some class/trait/closure semantics, complete exception unwinding, full standard-library and module-export coverage, full migration conformance, long-running fuzz/differential/leak checks, native UI/engine services, AOT compilation and a dedicated DXR backend.

## Portable tester distribution

`Publish-ANIMA-Beta.bat` builds Release x64, runs the self-test and smoke suite against the Release runtime, stages the companion executables and files into `dist\ANIMA-Native-Studio-0.3-Beta`, and creates `dist\ANIMA-Native-Studio-0.3-Beta-Portable.zip`. All Release C++ projects use `/MT` so testers do not need Visual Studio or the VC++ redistributable for the C++ runtime. Test graphics features on the destination hardware; this build does not imply all 0.3 spec features are complete.

## Beyond the specification

The integrated launcher, one-folder companion executable layout, starter-project creation, basic `doctor` checks and cross-version regression route are additions aimed at making the project easier to build and use as one Studio rather than several separate experiments.
