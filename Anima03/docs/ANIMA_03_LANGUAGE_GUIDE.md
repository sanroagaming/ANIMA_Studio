# ANIMA 0.3 Language Guide — Preview Subset

This guide follows the uploaded ANIMA 0.3 Engineering Specification. Syntax below is specified for 0.3; each section states whether it is supported in the integrated 0.3bc preview subset.

## File header and project

Every new source file begins with `ANIMA 0.3`. A project may declare target and capabilities:

```anima
ANIMA 0.3

project "Machine Inspector" {
    target native.windows
    capability cpu.parallel
    capability gpu.compute
    capability gpu.dxr_query
}

start {
    announce host.platform()
    announce cpu.logical_cores()
}
```

**0.3bc status:** the header, project name, target/capability declarations, and `start` block are parsed. Current host calls are registered and type-checked, but complete capability gating is not implemented; declarations must not be treated as a complete security boundary.

## Functions and calls

ANIMA 0.3 writes calls as `name(args)`. A function body and its parameters are declared with `define`; parameters must be annotated. Return values use `give`.

```anima
define damage_after_armor(raw: whole, armor: whole <- 0, floor: whole <- 1) -> whole {
    give math.max(floor, raw - armor)
}

start {
    announce damage_after_armor(50, floor: 5)
}
```

**0.3bc status:** typed parameters, return annotations, recursion, defaults and named arguments are type-checked and executed through the bytecode VM. Lambda/closure semantics and complete spec-wide flow analysis are still future work.

## Values and declarations

- `whole`: signed 64-bit integer.
- `real`: IEEE-754 64-bit floating-point value.
- `truth`: `true` or `false`.
- `text`: UTF-8 text.
- `list of T`, `map of K to V`, `maybe T`, function types and user-defined types are part of the 0.3 specification.
- `let x <- expr` introduces a mutable local; `x <- expr` reassigns it.
- `fixed x <- expr` declares a constant.

```anima
start {
    let position: real <- 0.0
    let velocity: real <- 3.5
    let step: real <- 1.0 / 60.0
    repeat 120 {
        position <- position + velocity * step
    }
    announce "Position: {position}"
}
```

**0.3bc status:** scalar `whole`, `real`, `truth`, `text`, basic lists/maps/indexing, checked integer overflow and this scalar simulation execute on the VM. `maybe` values, Unicode-complete code-point text behavior, and all engine vector/matrix operations are not fully implemented.

## Branches and loops

```anima
when health <= 0 {
    announce "Game over"
} otherwise when health < 25 {
    announce "Low health"
} otherwise {
    announce "All clear"
}

repeat i in 0..=5 {
    announce i
}

repeat each item in ["north", "east", "south", "west"] {
    announce item
}
```

**0.3bc status:** core `when/otherwise`, counted/while/repeat-each/range loops, short-circuit logic, `stop` and `skip` execute through the VM. The checker catches many static issues including errors in untaken branches; full flow analysis and complete spec conformance remain work in progress.

## CPU and GPU host calls

The preview exposes the current native host surface:

```anima
start {
    announce "CPU cores: {cpu.logical_cores()}"
    announce "GPU adapter: {gpu.adapter()}"
    announce "GPU backend: {gpu.backend()}"
    announce "DXR supported: {gpu.dxr_supported()}"
    when gpu.available() {
        announce gpu.squares(8)
    } otherwise {
        announce "GPU not available"
    }
    announce cpu.parallel_sum(1000000)
}
```

**0.3bc status:** these call the existing native host implementation from VM builtins. The specification makes ANIMA a scripting/tools/UI language over a C++ engine core. It does not add a new GPU kernel/resource language in 0.3; native D3D12 still needs an MSVC/Windows GPU run before it is hardware-verified.

## Tests and declarative UI

The specification defines `test "name" { ... }`, records, classes, traits, enums, modules and `app/state/scene/node/on` UI declarations. The parser preserves the documented declaration shapes so they can be inspected, but preservation does not imply full static type checking, code generation or runtime execution. Executable objects are scheduled for 0.3d, modules/standard library for 0.3e, and unified UI/migration for 0.3f.

## CLI

From a terminal in the project folder:

```text
anima version
anima tokens Examples/fibonacci.ani
anima ast Examples/fibonacci.ani
anima check Examples/fibonacci.ani
anima run Examples/fibonacci.ani
anima test Tests
anima scene Examples/SpecificationTargets/ui-two-scenes.ani
anima migrate ../AnimaLang/Examples/native-power.ani -o Examples/native-power-migrated.ani
anima self-test
```

`anima dis` emits the current typed-bytecode instruction listing. The migrator remains experimental and does not yet meet the specification's final F6 migration guarantees. `anima check` validates parsing, the implemented checker, and current code-generation coverage without executing the program; `anima run` compiles then executes through the VM.
