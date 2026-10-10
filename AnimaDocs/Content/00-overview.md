# ANIMA
> Code × Motion × Interfaces

ANIMA is a declarative language and runtime for describing interface structure, visual style, motion, scene flow, and interactive behavior. Its direction is to make expressive experiences feel authored rather than assembled from disconnected widgets.

## Start with the shape of a program

An ANIMA program declares an `app`, optional state, one or more `scene` blocks, and named `node` elements. Properties describe content, layout, appearance, and behavior. The runtime parses the source and mounts the resulting scene into a host.

```anima
app HelloWorld {
  state title = "Hello, ANIMA";

  scene Home {
    node Screen {
      flow: column(cross: center, route: center, gap: 16);
      node Title: Heading {
        text: title;
        motion: Reveal;
      }
    }
  }
}
```

## Designed for native and GPU-first exploration

[cards:overview]

## What this documentation covers

- **Language** — structure, properties, flow, motion composition, signals, scenes, and audio.
- **GPU graphics** — the browser GPU bridge, WGSL compute/render helpers, and the room study.
- **Native development** — the C++20 / Win32 / Direct3D 12 application and Visual Studio solution.
- **Examples and media** — source files, room scene, visual identity, and launch teasers.

## Status at a glance

> IMPORTANT: The native renderer is an early native milestone, not yet a complete native compiler for every ANIMA language feature. The browser runtime and native C++ application are separate implementations with different supported capabilities.

- **Browser runtime:** parses and mounts declarative UI scenes; includes five built-in motions, event bindings, scene navigation, chimes, and an optional WebGPU bridge.
- **Native renderer:** Windows desktop application using C++ and Direct3D 12 for the dedicated room scene. It structurally checks the room source but does not yet evaluate all UI syntax.
- **Graphics direction:** native DXR acceleration structures, richer materials, procedural animation, volumetrics, and simulation integration remain roadmap work unless noted in the relevant section.

## Brand and launch

The monochrome ribbon mark is the primary icon. Launch assets are credited to **Unrecorded Labs**.


## ANIMA Studio Beta

The native Studio launcher (`AnimaStudio.exe`) is the recommended entry point for the 0.3 Beta. It provides one place to open documentation, run language examples, launch the native room scene, access the 0.2 reference runtime, run diagnostics, and create a starter project. Use `Publish-ANIMA-Beta.bat` to package a Release build for testers.
