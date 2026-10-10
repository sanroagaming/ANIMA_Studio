# ANIMA Native 0.2 Preview — language spec

## Design intent

ANIMA source should read as a declaration of intent, a set of reusable definitions, and a start sequence. It should not look like a C++ translation. Version 0.2 uses a bundled, hand-written direct reader/evaluator with no third-party lexer/parser or generated parser. The reader recognizes statements and evaluates them as it goes; it does not currently produce native machine code or a separate AST/bytecode.

## File identity

- Extension: `.ani`
- Required header: `ANIMA 0.2`
- Top-level declarations: `project`, `define`, `start`
- One `start` block is required. Definitions are named and parameterless in this preview.

## Grammar sketch

```text
program       := "ANIMA" "0.2" project? definition* start
project       := "project" text block
 definition   := "define" name block
start         := "start" block
statement     := announce | let | perform | repeat | when
announce      := "announce" expression newline
let           := "let" name (":" type)? "<-" expression newline
perform       := "perform" name newline
repeat        := "repeat" expression block
when          := "when" expression block ("otherwise" block)?
```

`project` content is declarative metadata for targets/capabilities in 0.2. The runtime currently uses a single global value scope. Blocks use `{` and `}`. Line comments start with `#` or `//`. Statement terminators are newlines, not semicolons.

## Naming rules

Identifiers begin with an ASCII letter or underscore, followed by ASCII letters, digits, or underscores. Use `snake_case` for values and definitions. Namespaced runtime capabilities use dotted paths, such as `cpu.logical_cores()` and `gpu.squares(16)`. Dotted names are capability names, not arbitrary operating-system symbol lookup.

## Values and types

| Type | Meaning |
|---|---|
| `whole` | Integral numeric value (represented as double in this preview; checked for integral value on typed assignment). |
| `real` | Numeric value. |
| `text` | UTF-8 string. |
| `truth` | Boolean `true` or `false`. |
| `series` | Numeric list produced by GPU compute. |
| `value` | Any value supported by this preview. |

Literals include numbers, double-quoted text, `true`, and `false`. Escape sequences are `\\n`, `\\r`, `\\t`, `\\\"`, and `\\\\`. Expressions support parentheses, `*`, `/`, `+`, `-`, `<`, `<=`, `>`, `>=`, `==`, and `!=`. `+` concatenates if either operand is text. Division by zero is rejected.

## Statements

- `announce expression` writes a value to stdout.
- `let name: type <- expression` assigns a typed value; type is optional.
- `perform definition_name` executes a declared reusable definition.
- `repeat expression { ... }` executes a block a whole number of times, maximum 10,000.
- `when condition { ... } otherwise { ... }` executes one of two blocks. The condition must be `truth`.

Definitions currently take no arguments, do not return values, and share the program's value scope. Recursion is capped at 64 calls.

## Native capability surface

| Call | Result / limits |
|---|---|
| `host.platform()` | `windows.native` in the Windows runtime; `portable.host` on a non-Windows development build. |
| `cpu.logical_cores()` | OS-reported logical CPU count. |
| `cpu.parallel_sum(N)` | Computes sum 1..N using native C++ worker threads; `0 <= N <= 5,000,000`. |
| `gpu.available()` | True if a usable hardware D3D12 adapter can be opened. |
| `gpu.adapter()` | Adapter name; NVIDIA is preferred when a supported adapter is enumerated. |
| `gpu.backend()` | Identifies D3D12 compute or unavailable backend. |
| `gpu.vram_mb()` | Dedicated adapter memory in MiB; 0 if unavailable/unreported. |
| `gpu.dxr_supported()` | Checks device DXR tier support. It does not imply the language's `gpu.squares` operation is a DXR ray-tracing dispatch. |
| `gpu.squares(N)` | Actual D3D12 compute dispatch that writes 1² through N² into a GPU storage buffer and reads it back; `1 <= N <= 65,536`. |

These named calls are the 0.2 capability boundary. They provide useful CPU/GPU access without granting arbitrary raw memory or driver access. More native APIs should be added as typed capabilities with explicit ownership/lifetime semantics.

## Program example

```anima
ANIMA 0.2
project "Native Power" {
    target native.windows
    capability cpu.parallel
    capability gpu.compute
}

define report_system {
    announce cpu.logical_cores()
    announce gpu.adapter()
}

start {
    perform report_system
    let total: whole <- cpu.parallel_sum(1000000)
    announce total
    when gpu.available() {
        let squares: series <- gpu.squares(16)
        announce squares
    } otherwise {
        announce "No Direct3D 12 GPU is available."
    }
}
```

## Error model

Errors report the current source line and fail the program with nonzero exit status. Unknown declarations, statements, built-ins, values, invalid types, invalid numeric ranges, unterminated blocks, and division by zero are diagnosed. `--check` evaluates the source shape while substituting capability placeholders; it does not create a device or run GPU work.

## Explicit non-goals in 0.2 Preview

- No general parameterized functions or return statements yet.
- No imports/modules, structs, user-defined types, asynchronous tasks, closures, or exception handling in ANIMA syntax yet.
- No arbitrary pointer arithmetic, OS handles, memory mapping, or unbounded thread creation.
- No ANIMA-to-machine-code AOT compiler yet. Visual Studio compiles the native language runtime; `.ani` source executes in that runtime.
- `gpu.dxr_supported()` is a capability query only. This preview does not expose a language-level DXR state-object/acceleration-structure API.

## Next milestones

0.3: typed intermediate representation/bytecode, function parameters and returns, modules, diagnostics, stable source spans, unit tests and language conformance tests.

0.4: explicit GPU resource handles, shader/module declarations, resource lifetime ownership and GPU compute graphs.

0.5+: optional AOT compiler backends after the source semantics and IR stabilize.
