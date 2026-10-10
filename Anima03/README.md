# ANIMA Native 0.3 Integrated Preview

This project is the core ANIMA 0.3 toolchain inside the integrated Studio solution. It includes a handwritten C++20 lexer/parser, supported static-checker passes, typed bytecode compiler and stack VM; now also records/enum metadata and a constrained local declaration-only module loader.

Build with Visual Studio 2022 by opening `../ANIMA_Studio.sln` and building Debug|x64. For portable development: `cmake -S . -B build`, `cmake --build build`, `ctest --test-dir build --output-on-failure`.

Current verification snapshot: 84 internal self-tests and 15 CTest targets pass in the portable environment. This is a feature slice of 0.3, not all spec work packages complete. Read `docs/ANIMA_03_PREVIEW_SCOPE.md`, `docs/ANIMA_03_BC_INTEGRATED.md`, and `Reference/ANIMA_0.3_Engineering_Specification.docx`.
