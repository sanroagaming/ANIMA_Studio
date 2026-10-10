# ANIMA Native conversion roadmap

## Phase 0.1 — Native window and GPU compute (included)

A C++20 Windows executable reads the existing room `.anima` file, structurally checks declarations, creates a D3D12 device on a hardware adapter (preferring NVIDIA), checks the D3D12 DXR tier, compiles HLSL compute/presentation shaders at runtime, renders a room, and supports lighting ON/OFF.

## Phase 0.2 — True DXR pipeline (next)

Replace the compute shader's explicit SDF intersection loops with a DirectX Raytracing pipeline:

1. Add scene meshes/primitives and materials to a typed native scene representation.
2. Build bottom-level acceleration structures (BLAS) for geometry and a top-level acceleration structure (TLAS) for instances.
3. Create a DXR state object with ray-generation, miss, and closest-hit shaders; add shadow and reflection ray types as needed.
4. Create shader tables, a ray output UAV, and use `DispatchRays` per frame.
5. Keep a compute-shader fallback, but mark it explicitly as non-DXR.
6. Add device-capability diagnostics, debug-layer validation, GPU timestamps, and render-scale controls.

Microsoft's official DirectX Graphics Samples include DXR tutorials such as Hello World and Simple Lighting; use these to validate the API setup and shader-table lifecycle before scaling to the ANIMA room. See `https://github.com/microsoft/DirectX-Graphics-Samples/tree/master/Samples/Desktop/D3D12Raytracing`.

## Phase 1 — Native ANIMA language front-end

Move the current JavaScript implementation into explicit native layers:

- lexer and source spans (line/column/error messages);
- parser and typed AST for `app`, `state`, `scene`, `node`, properties, expressions, and motion composition;
- evaluator for constants, arrays/objects, operators and approved built-in/capability calls;
- runtime scene graph with typed nodes and property values;
- event routing and scene navigation;
- native widget/layout adapter separate from the renderer.

Keep the existing `.anima` syntax stable and create conformance tests that feed the same programs to the JS and C++ front-ends, comparing AST summaries and runtime outcomes.

## Phase 2 — Native rendering API

Define renderer-neutral ANIMA operations (scene, camera, mesh, material, light, texture, animation, simulation) and back them with D3D12. The renderer should own GPU resources and lifetimes; language values should refer to resource handles, not raw API pointers.

## Phase 3 — simulation and animation

Implement fixed-step simulation time, stable orbital integrators (e.g. velocity Verlet/leapfrog), collision/spatial queries, GPU particle and field kernels, and volume-density grids. Keep numerical simulation semantics independent of graphics settings so frame-rate and render-sample changes do not change the physical result.

## Build principle

Compile the **runtime** with Visual Studio first; load `.anima` source as input. Later, add an optional ahead-of-time compiler that turns `.anima` into a validated intermediate representation or bytecode. Don't try to translate every UI property straight into hand-written C++ source as the first migration step.


## ANIMA Native 0.2 Preview — in-house source reader and capabilities

The solution now includes a third project, `AnimaLang`, which executes `.ani` source files that begin with `ANIMA 0.2`. The source surface adds `project`, reusable `define` blocks, `start`, typed `let name: type <- value`, `announce`, `perform`, `repeat`, and `when / otherwise`. It is a native C++20 runtime and does not use an external/generated lexer/parser, JavaScript, or a browser. The in-house direct reader recognizes and evaluates the source as it goes; it is not yet an ahead-of-time machine-code compiler.

Current built-ins: `cpu.logical_cores()`, `cpu.parallel_sum(N)`, `gpu.available()`, `gpu.adapter()`, `gpu.backend()`, `gpu.dxr_supported()`, and `gpu.squares(N)`. On Windows, `gpu.squares(N)` dispatches a D3D12 compute shader. DXR support is queried but no ANIMA-language DXR dispatch is exposed yet. GPU resources are accessed through named, bounded capabilities rather than unrestricted raw GPU/OS memory.

### Following milestones

- Add functions with typed parameters and return values, modules, structs/user-defined record types, scope rules, improved diagnostics and conformance tests.
- Move to a typed intermediate representation / bytecode so source checking and execution are separate and predictable.
- Add explicit resource handles, device/queue ownership, GPU kernel declarations and graph scheduling.
- Add a DXR state-object/BLAS/TLAS backend separately from the current compute shader demonstration.
- Consider optional ahead-of-time compilation only after the language semantics and IR stabilize.
