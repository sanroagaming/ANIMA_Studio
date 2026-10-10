# ANIMA Native 0.2 Preview

ANIMA now has a dedicated native-first source dialect in `.ani` files. It uses its own vocabulary and structure rather than translating user programs into C++: `ANIMA 0.2`, `project`, `define`, `start`, `let`, `announce`, `perform`, `repeat`, and `when / otherwise`.

## The core shape

```anima
ANIMA 0.2

project "Native Power" {
    target native.windows
    capability cpu.parallel
    capability gpu.compute
}

define report_machine {
    announce "CPU logical cores:"
    announce cpu.logical_cores()
    announce "GPU adapter:"
    announce gpu.adapter()
}

start {
    perform report_machine
    let total: whole <- cpu.parallel_sum(1000000)
    announce total
    when gpu.available() {
        let result: series <- gpu.squares(16)
        announce result
    } otherwise {
        announce "Direct3D 12 compute is not available."
    }
}
```

## Native capabilities

- `cpu.logical_cores()` queries the host CPU.
- `cpu.parallel_sum(N)` uses native worker threads and is range-limited.
- `gpu.adapter()`, `gpu.backend()`, `gpu.vram_mb()`, `gpu.available()`, and `gpu.dxr_supported()` query device capability.
- `gpu.squares(N)` compiles and dispatches an HLSL compute shader through Direct3D 12 on Windows, then reads results back into an ANIMA `series` value.

ANIMA 0.2 has no external parser generator, JavaScript engine or browser dependency. Its small hand-written direct reader/evaluator is bundled into `AnimaLang.exe`; no generated token list or external AST package is used. A language must still recognize its own source grammar—the important point is that the recognition and execution path belongs to ANIMA's native runtime.

## Try it

Open the **AnimaLang** project in `ANIMA_Studio.sln` and set it as Startup Project. Build `Debug | x64` then run this from the output folder:

```powershell
.\AnimaLang.exe --check .\AnimaLangExamples\native-power.ani
.\AnimaLang.exe .\AnimaLangExamples\native-power.ani
```

The `native-power.ani` example reports the CPU and GPU, calculates 1+2+…+1,000,000 with CPU worker threads, and computes square values on the GPU when a D3D12 hardware adapter is available.

## What 0.2 means

This is an executable native language preview, not a full AOT compiler. The engine is built by Visual Studio as C++20; `.ani` programs are evaluated by ANIMA's in-house runtime. There are no general parameterized functions, modules, structs, async tasks or arbitrary pointer operations yet. The GPU surface is intentionally capability-based: native access should be powerful but explicit about resources, bounds, device loss and ownership.
