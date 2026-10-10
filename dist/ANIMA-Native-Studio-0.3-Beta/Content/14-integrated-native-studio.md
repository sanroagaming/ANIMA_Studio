# ANIMA Native Studio 0.3 Beta

ANIMA Native Studio is the consolidated Windows development preview. The new **AnimaStudio.exe** is a native Win32/GDI+ launchpad with Home, Learn, Examples, Graphics Lab, Developer Tools and About sections. It is a native desktop interface, not a WebView wrapper.

## One place to launch the project

- **Documentation:** opens the native Markdown documentation reader.
- **Examples:** checks and runs the bundled `.ani` programs in a visible console, and opens older browser-host examples separately.
- **Graphics Lab:** launches the native Direct3D 12 room prototype and both cinematic teaser files.
- **Developer Tools:** runs the language self-test and package doctor, browses a source file, opens the examples folder, or creates a starter project.
- **Legacy:** the 0.2 runtime remains included as a reference/compatibility executable; it is not the primary language path.

## Compile and share

For source development, open `ANIMA_Studio.sln` in Visual Studio 2022 and build Debug or Release | x64. To make a portable tester package, run `Publish-ANIMA-Beta.bat`. It builds Release x64, tests the Release language runtime, and creates `dist/ANIMA-Native-Studio-0.3-Beta-Portable.zip` containing the Studio app, companion executables, documentation, examples, shaders, logo, and media.

All Release C++ targets use the static MSVC runtime (`/MT`), so testers do not need Visual Studio or the VC++ redistributable to launch the app. They still need Windows system graphics components and compatible drivers. The publisher should test the staged archive on a clean Windows user or PC before sharing broadly.

## Beta scope

ANIMA 0.3 is still a development preview. The compiler/VM implements a useful tested subset, not every item in the engineering specification. Native UI drawing, the complete object/exception/module model, AOT compilation and a dedicated DXR backend are not claimed as finished. See the release manifest and gap matrix for current details.
