# ANIMA Native 0.3 — Integrated Language Preview

ANIMA 0.3 is now the main language path in ANIMA Native Studio. The working pipeline is:

`source → hand-written lexer → parser/AST → static checker → typed bytecode compiler → stack VM`

## Features present in this preview

- Hand-written source processing, AST/token inspection, source-span diagnostics, and `check` that never executes user code.
- Functions with typed parameters, default/named arguments, recursion, arithmetic, branches, loops, ranges, and text interpolation.
- Lists and insertion-ordered maps, including `repeat each key, value in map`.
- Records with literal and enum-case defaults, member read/write, constructors, and copy-update with `with`.
- Enums with explicit/implicit values, `.name`, `.value`, `.all()`, `.from_name()`, equality, and map-key support.
- A constrained local declaration-only module import path, CPU parallel-sum capability, and existing native GPU host bridge.
- `run`, `check`, `test`, `tokens`, `ast`, `dis`, `scene`, `ui-catalog`, `migrate`, `repl`, and `self-test` commands.

## Example

```anima
ANIMA 0.3

enum ActorState { idle, roaming, hunting <- 10 }
record Actor {
    name: text <- "Unknown"
    state: ActorState <- ActorState.idle
    health: whole <- 100
}

start {
    let actor <- Actor(name: "Mara")
    let moving <- actor with { state: ActorState.roaming }
    announce "{moving.name}: {moving.state.name}"
    announce "Original state remains {actor.state.name}"
}
```

## Current boundary

This is an integrated preview, not full conformance to the engineering specification. Full closures/lambdas, class/trait lifecycle, all static rules, module graph/export behavior, complete standard-library modules, all error-unwinding semantics, full `.anima` tree-equivalent migration, native UI rendering, GPU resource/kernel language support, and AOT compilation remain incomplete. The GPU bridge is not a claim that the current 0.3 language supports arbitrary shader programs. Windows/MSVC and physical RTX/D3D12 behavior need verification on a Windows machine.

The original 46-page engineering specification is packaged with the source under `Anima03/Reference/`. For exact scope see `Anima03/docs/ANIMA_03_PREVIEW_SCOPE.md` and `Anima03/docs/ANIMA_03_INTEGRATED_STUDIO.md`.
