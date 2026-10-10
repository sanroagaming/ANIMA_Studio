# Getting started
> From your first scene to the native Windows application.

## 1. Understand the current runtimes

ANIMA currently exists as two related tracks. The browser runtime mounts programs into a web host. The native application opens a Windows window and renders its dedicated room demo through Direct3D 12. The native application is not a browser wrapper, and it does not yet translate every browser-language construct into native controls.

## 2. A minimal ANIMA program

```anima
app Greeting {
  state title = "Hello, world";
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

The outer `app` names the program. `state` declares a value. `scene` groups content into a navigable scene. `node` declares an element and its properties.

## 3. Build ANIMA Native in Visual Studio

1. Extract the full `ANIMA_Native_Studio` folder from the supplied ZIP.
2. Open `ANIMA_Studio.sln` in Visual Studio 2022.
3. Install the **Desktop development with C++** workload, MSVC v143, and a Windows 10/11 SDK if prompted.
4. In Solution Explorer, right-click **AnimaDocs** and choose **Set as Startup Project**.
5. Select **Debug | x64**, then choose **Build → Build Solution** (`Ctrl+Shift+B`).
6. Press **F5** to launch the native documentation app. Its **Run room demo** action launches the native renderer after the solution has built.

> TIP: Both projects are part of one solution. Build the whole solution so `AnimaNative.exe` is present for the room-demo action.

## 4. Project layout

```text
ANIMA_Native_Studio/
  ANIMA_Studio.sln
  AnimaNative/            C++ renderer and HLSL shaders
  AnimaDocs/              Native documentation app
    Content/              Markdown content rendered natively
    Assets/               Monochrome mark, icon art, videos, previews
    Examples/             .anima source examples
    ReferenceArchive/     Previous HTML docs retained for reference
```

## Troubleshooting

- **Project not found:** open `ANIMA_Studio.sln` from the extracted root folder, not from inside the ZIP preview.
- **MSVC toolset missing:** use Visual Studio Installer → Modify → install Desktop development with C++ and MSVC v143.
- **Windows SDK missing:** install a Windows 10 or Windows 11 SDK through the same installer.
- **Room demo won't launch:** build the entire solution first; the docs app searches for the sibling native renderer executable.
- **GPU adapter is WARP:** the renderer has fallen back to Microsoft software rendering. Update the NVIDIA driver and confirm that Windows is using the RTX 3050.
