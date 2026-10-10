# ANIMA Unified Launcher

`anima.exe` is the single entry point for the native ANIMA Studio distribution. It routes the docs UI, D3D12 room, legacy 0.2 runtime and 0.3 VM without a browser dependency.

Commands: `anima`/`anima docs`, `anima room`, `anima new <folder>`, `anima doctor`, `anima legacy <args>`, and any `anima-core.exe` language command (`run`, `check`, `test`, `dis`, `scene`, `ui-catalog`, `migrate`, `self-test`, etc.).

All companion executables must live beside `anima.exe` in `x64\Debug` or `x64\Release`. Build the root `ANIMA_Studio.sln` to ensure that.
