# ANIMA Native — Visual Studio 2022 starter

This package contains the **complete Visual Studio project**, not just the `.sln` file. Keep the solution, project, `src`, `shaders`, and `Examples` together after extracting the archive.

## Build it in Visual Studio — quick start

1. Download `ANIMA_Native_Visual_Studio_Ready.zip` and choose **Extract All**. Do not try to open the `.sln` from inside the ZIP preview.
2. Open the extracted `AnimaNative/AnimaNative.sln` in **Visual Studio 2022**.
3. If prompted, install the **Desktop development with C++** workload and a Windows 10/11 SDK via Visual Studio Installer. This is a Windows-only Direct3D 12 application.
4. In the toolbar, choose **Debug** and **x64**.
5. Use **Build → Build Solution** (Ctrl+Shift+B).
6. Press **F5** to launch, or **Ctrl+F5** to launch without the debugger.

The project uses the Visual Studio `v143` C++ toolset and C++20. No NuGet packages, Node.js, Python, CMake, browser, WebView, or web server are required when building with the included `.sln`.

## If Visual Studio says a project or file was not found

- Make sure you extracted the ZIP first.
- Open `AnimaNative.sln` from the extracted `AnimaNative` folder.
- Verify `AnimaNative.vcxproj` is in the **same folder** as `AnimaNative.sln`.
- Verify the `src`, `shaders`, and `Examples` subfolders are still present.
- If Visual Studio says the `v143` platform toolset is missing, use Visual Studio Installer → Modify → install **Desktop development with C++** and the latest MSVC v143 build tools.
- If it says a Windows SDK is missing, install a Windows 10/11 SDK from the same installer.

## What runs

The app opens a native Win32 window and uses Direct3D 12 compute/presentation shaders to render the room scene. The source `.anima` file is loaded and structurally checked at startup; the room renderer is still a dedicated native sample, not yet a complete native evaluator for all ANIMA syntax.

**GPU-tracing terminology:** the ON mode uses ray-marching / ray tests in a D3D12 compute shader. The app checks and reports whether the adapter supports DXR, but this milestone does not yet create a DXR state object or BLAS/TLAS acceleration structures. Do not interpret the current ON toggle as a guarantee that dedicated RTX RT cores are being used. That DXR pipeline is the next milestone.

## Controls

- Click the in-scene `RT ON` / `RT OFF` segmented buttons, or press `R` to toggle.
- Press `1` to enable the enhanced mode.
- Press `0` to disable it.
- Press `Esc` to close.

The title bar reports the selected adapter and whether DXR support was detected. For the RTX 3050, confirm the title names the NVIDIA GeForce RTX 3050 rather than Microsoft WARP (software fallback).

## Files

- `AnimaNative.sln` — open this file in Visual Studio.
- `AnimaNative.vcxproj` — the C++ application project referenced by the solution.
- `src/main.cpp` — Win32 app entry point and input loop.
- `src/NativeAnima.*` — initial source lexer/structural checker.
- `src/NativeRenderer.*` — D3D12 device, pipeline, resources, presentation.
- `shaders/room_compute.hlsl` — procedural room and ON/OFF compute render.
- `shaders/blit.hlsl` — full-screen presentation shader.
- `Examples/raytrace-room.anima` — source passed to the native front end.
- `docs/NATIVE_ROADMAP.md` — migration plan.

## Validation note

The solution/project XML and all project-relative source/resource paths are checked in the packaged build. The portable C++ source front end also compiles and successfully parses the included room `.anima` sample. Actual MSVC + Windows SDK + Direct3D 12/HLSL compilation can only be confirmed on Windows with Visual Studio installed.
