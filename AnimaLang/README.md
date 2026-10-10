# ANIMA Native 0.2 Preview

ANIMA 0.2 is a native-first language experiment with a source format and vocabulary of its own. Its entry point is `start`, reusable parameterless procedures are `define` blocks invoked with `perform`, variables are introduced with `let name: type <- value`, output uses `announce`, loops use `repeat`, and conditions use `when` / `otherwise`.

The runtime is compiled as a C++20 Windows console program in Visual Studio, but ANIMA programs are `.ani` source files. Pressing F5 on the project launches the native-power sample by default. They are not C++ and need not be rewritten as C++.

## Important front-end wording

This preview has no third-party lexer/parser library, generated parser, AST dependency, browser engine, JavaScript runtime, Node.js, or NuGet package. A small in-house direct reader/evaluator reads the source character stream and executes each statement as it encounters it. Any textual language still needs a built-in mechanism to recognize syntax; ANIMA's mechanism is bundled in this runtime rather than delegated to an external parser tool.

## Visual Studio build

1. Open either `AnimaLang.sln` for the language by itself, or `../ANIMA_Studio.sln` to work on the full ANIMA suite.
2. Right-click **AnimaLang** and select **Set as Startup Project**.
3. Build `Debug | x64`. F5 is configured to run `AnimaLangExamples/native-power.ani` by default.
4. Run `AnimaLang.exe --check AnimaLangExamples/native-power.ani` to verify a source file.
5. Run `AnimaLang.exe AnimaLangExamples/native-power.ani` to execute it.

After building through the solution, the post-build step copies `.ani` examples to `x64/Debug/AnimaLangExamples` or `x64/Release/AnimaLangExamples` (it uses a separate folder to avoid build-output clashes with the docs and renderer projects).

## Source form

```anima
ANIMA 0.2

project "My Program" {
    target native.windows
    capability cpu.parallel
}

define report {
    announce "Cores:"
    announce cpu.logical_cores()
}

start {
    perform report
    let total: whole <- cpu.parallel_sum(1000000)
    announce total
}
```

## Built-in value types

- `whole` — integral numeric value represented internally as a double in this preview, checked for whole-number values.
- `real` — floating-point number.
- `text` — string.
- `truth` — `true` or `false`.
- `series` — numeric series returned by GPU compute.
- `value` — any value type.

## Built-in capabilities

| Built-in | Description |
|---|---|
| `host.platform()` | Reports native/portable host label. |
| `cpu.logical_cores()` | Queries logical CPU count. |
| `cpu.parallel_sum(N)` | Uses native C++ worker threads to sum 1..N; N is limited to 5,000,000. |
| `gpu.available()` | Queries whether a usable D3D12 hardware adapter is available. |
| `gpu.adapter()` | Reports the selected hardware adapter; NVIDIA is preferred when found. |
| `gpu.backend()` | Reports the native GPU backend. |
| `gpu.vram_mb()` | Reports dedicated GPU memory in MiB, or 0 when unavailable/unreported. |
| `gpu.dxr_supported()` | Queries DXR feature support; does not itself execute ray tracing. |
| `gpu.squares(N)` | Dispatches a Direct3D 12 compute shader to calculate 1²..N² and reads back the results; N is limited to 65,536. |

The current capability model is explicit and bounded; it is not unrestricted raw access to arbitrary operating-system memory or all hardware registers. Additional native systems can be exposed as named, documented capabilities.

## Syntax rules

- File extension: `.ani` for ANIMA 0.2 Preview.
- Required first line: `ANIMA 0.2`.
- Identifiers use ASCII letters/underscore followed by letters, digits, or underscores. Built-in namespaces use dotted names such as `cpu.logical_cores()`.
- Text literals use double quotes with `\n`, `\t`, `\"`, and `\\` escapes.
- `#` and `//` start line comments.
- Statements are line-oriented and do not require semicolons.
- Expressions support parentheses, unary signed numeric literals, `*`, `/`, `+`, `-`, numeric comparisons, and equality/inequality. `+` also combines text with a value.
- Reusable definitions do not have parameters or return values in 0.2; variables live in the program runtime scope.
- `repeat` is capped at 10,000 iterations; CPU sum is capped at 5 million; GPU compute squares is capped at 65,536 items.

## Native code generation status

This is a native-compiled C++ runtime that directly evaluates `.ani` source; it is **not yet an ahead-of-time compiler that emits machine code from ANIMA source**. ANIMA remains its own language and source format. A later milestone can add a typed bytecode/IR and optional AOT backend without making the source syntax C++.
