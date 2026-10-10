# AnimaDocs — Native Windows documentation

A native C++20 Win32 application that renders the ANIMA developer manual from local UTF-8 Markdown files. Uses Win32, GDI+, and ShellExecute for native UI and actions; no browser or WebView.

## Build
Open the parent `ANIMA_Studio.sln`, set this project as startup, and build the whole solution with Visual Studio 2022 / MSVC v143 / Windows SDK / x64.

## Content
- `Content/*.md` — page contents loaded by the app at runtime.
- `Assets/anima-docs.ico` — app icon; `anima-monochrome.png` is the active in-app logo.
- `Assets/*.mp4` — teasers opened via the Windows default media player.
- `Examples/*` — legacy `.anima` programs, native ANIMA 0.2 `.ani` programs, and optional browser-host references.
- `ReferenceArchive/` — previous documentation HTML retained for legacy reference, not used to render the native app.
