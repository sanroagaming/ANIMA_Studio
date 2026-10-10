# ANIMA Studio — Native Beta Launcher

ANIMA Studio is a native Win32/GDI+ launchpad for ANIMA Native 0.3 Beta. It is not a WebView and does not require a browser to display its own UI.

## Features
- Modern light/dark native desktop interface using the monochrome ANIMA icon.
- Open the native documentation app or Direct3D 12 room demo.
- Browse, check and run bundled `.ani` examples through the 0.3 CLI; run the 0.2 reference runtime separately.
- Launch both teaser videos, the ray-tracing comparison and legacy WebGPU reference page.
- Run self-tests/doctor, choose an external `.ani` file, create a starter project, and open packaged folders.
- F5 performs the primary action for the selected page; F1–F6 switches pages.

The app resolves companion applications and assets relative to its own executable directory. The same build can run from `x64\Release` and from the portable package.

## Build and package
Build `ANIMA_Studio.sln` in Visual Studio 2022 using Release | x64. Run `Publish-ANIMA-Beta.bat` to stage the executable, companion tools, docs, examples, shaders and media into a portable folder and ZIP. Release binaries use the static MSVC CRT (`/MT`). System components such as Direct3D 12, GDI+, Windows shell APIs and graphics drivers must still exist on the target Windows machine.
