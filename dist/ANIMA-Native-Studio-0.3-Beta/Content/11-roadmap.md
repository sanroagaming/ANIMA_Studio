# Roadmap and implementation status
> A clear boundary between what runs today and what comes next.

## Available in the browser runtime

- Declarative `app`, `state`, `scene`, and `node` declarations.
- A mapped set of content, color/effect, typography, sizing, layout, and background-image properties.
- Flex-like flow modes and named options.
- Five built-in motions: `Reveal`, `FadeIn`, `FadeUp`, `Glow`, `Pulse`.
- Event handlers for press, click, hover, and focus.
- Scene navigation using `go(...)` and generated chimes using `chime(...)`.
- Optional WebGPU request, compute, render, and room-demo helpers.

## Available in the native Windows track

- C++20 Win32 app and D3D12 adapter/device path.
- Dedicated room renderer using HLSL compute and presentation shaders.
- Adapter name and DXR capability reporting.
- Native structural checking of the room `.anima` source.
- This Win32 documentation app with search, navigation, code copying, local file launch, media launch, and theme switch.

## Not implemented yet

- Complete native language compiler and evaluator for every ANIMA declaration and expression.
- General reactive state updates, loops, and timeline blocks in the current browser runtime.
- Native control/layout/motion/event equivalents for all browser properties.
- DXR state object, BLAS/TLAS acceleration structures, and ray-generation/hit/miss shaders.
- BVH accelerated scene intersection, denoising, broad material/texture model, and production volumetrics.
- A validated orbital physics solver. Orbital systems and volumetric simulation are planned integration areas, not completed capabilities of the current sample.

## Next engineering milestones

1. Port the parser into structured AST nodes, not just structural validation.
2. Define a stable native runtime contract for properties, scenes, state, and signal dispatch.
3. Implement native layout/paint and a motion scheduler for a small supported language subset.
4. Add a proper DXR backend using acceleration structures and shader tables on compatible adapters; retain compute fallback where useful.
5. Add explicit scene/material/resource declarations before exposing advanced simulation APIs.
6. Add tests and sample projects for each supported language feature.

## Reliability rule

The documentation should label roadmap concepts as planned until a tested implementation is shipped. In particular, WebGPU is a general graphics/compute API and the native room currently uses D3D12 compute; neither statement by itself means ANIMA already owns a complete hardware-accelerated path tracer or orbital physics engine.


## ANIMA Native 0.2 Preview

The solution now includes **AnimaLang**, a native C++20 runtime for `.ani` source files headed by `ANIMA 0.2`. The source vocabulary includes `project`, `define`, `start`, `let`, `announce`, `perform`, `repeat`, and `when / otherwise`. The runtime's source reader is built in-house, with no external parser generator or browser/JavaScript dependency. See **ANIMA Native 0.2 Preview** in the sidebar for the syntax and capability reference.

CPU capabilities include logical core queries and a bounded parallel-sum operation. GPU capabilities query the D3D12 adapter and DXR feature tier, and `gpu.squares(N)` dispatches a real Direct3D 12 compute shader. This does not yet make ANIMA an ahead-of-time compiler or expose general DXR ray tracing, arbitrary pointers, or unrestricted hardware access. Those features require deliberate IR, resource ownership, and compiler milestones.
