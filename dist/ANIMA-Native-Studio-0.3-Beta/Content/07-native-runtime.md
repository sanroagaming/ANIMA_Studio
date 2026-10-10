# Native runtime
> A browser-free C++ application for Windows.

## Native architecture

```text
ANIMA_Native_Studio/
  ANIMA_Studio.sln
  AnimaNative/
    src/         Win32 application, source checker, D3D12 renderer
    shaders/     HLSL compute and presentation shaders
    Examples/    raytrace-room.anima
  AnimaDocs/
    Content/     Markdown files displayed by this native documentation UI
    Assets/      icon, logo, previews and teaser videos
    Examples/    copyable source programs
```

## Technology stack

- C++20 and Win32 windowing/input.
- Direct3D 12 resources, command queues and presentation.
- HLSL compute shader for the procedural room scene and a presentation shader.
- The legacy native structural checker for room source, plus the new AnimaLang 0.2 direct reader/evaluator as a sibling project.
- A separate native documentation program that paints its interface with Win32/GDI+ and reads local Markdown content files.

## What is native today

The desktop renderer opens a native window, enumerates D3D12 adapters, reports whether DXR is present on the selected adapter, and renders its dedicated room scene through D3D12 compute and presentation shaders. The language front end loads and structurally checks the room `.anima` source.

The native documentation app is also a true Win32 desktop application; it doesn't use WebView, HTML, or a local server to render its pages. It reads `.md` content files, supports search and navigation, copies code samples, launches the native room renderer, and opens bundled videos in the system's default media player.

## What is not native-complete yet

The AnimaNative project’s older C++ front end is not a full compiler or evaluator for all browser ANIMA syntax. The separate AnimaLang project implements a small native-first ANIMA 0.2 source dialect (`.ani`) with an in-house direct reader/evaluator, CPU worker-thread sum, and D3D12 compute-shader capability. The 0.2 language does not yet include function parameters/returns, modules, user-defined record types, asynchronous tasks, arbitrary pointer access, or an ahead-of-time machine-code compiler. It also does not yet expose a language-level DXR dispatch API. The room renderer remains a specialized sample scene; it is not a generic render backend for arbitrary `.anima` programs.

## Build

Open `ANIMA_Studio.sln`, use the Desktop development with C++ workload, choose `Debug | x64`, and build the whole solution. Set `AnimaDocs` as the startup project for the native documentation app. The room-demo action works after the `AnimaNative` project has also built.
