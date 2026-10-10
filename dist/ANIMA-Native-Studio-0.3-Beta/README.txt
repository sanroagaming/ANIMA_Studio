ANIMA NATIVE STUDIO 0.3 BETA — PORTABLE TEST BUILD
Unrecorded Labs

START HERE
1. Extract the full ZIP to a normal folder (do not run from inside the ZIP viewer).
2. Double-click "Launch ANIMA Studio.bat" or run AnimaStudio.exe.
3. Use Home / Learn / Examples / Graphics lab / Developer tools in the app.
4. Run examples from the Examples page. Console windows display program output and errors.
5. Use Developer tools > Run self-tests to verify the bundled language runtime.

This is a portable Release build. It was built with the static MSVC runtime (/MT), so the Visual Studio IDE is not needed to launch it. Windows system graphics APIs/drivers are still required. The native room demo requires a Windows system with working Direct3D 12 support.

BETA STATUS
ANIMA Native 0.3 is a development preview. Some language features listed in the engineering specification remain incomplete. The room renderer is a prototype, not a full production ray-tracing renderer. Report the exact action, source file, error text and Windows/GPU details when submitting feedback. Avoid loading untrusted programs with filesystem or native capabilities.

FOLDERS
Content        Native documentation chapters
Assets         Logo, app icon, brand art and teaser media
Anima03Examples 0.3 language examples
AnimaLangExamples preserved 0.2 reference examples
Examples       Legacy UI/GPU host samples and room source
Shaders        native room HLSL shaders
LanguageDocs   language references and build notes
Reference      supplied 0.3 engineering specification

The portable package is built by Publish-ANIMA-Beta.bat from the source solution. Source maintainers should keep the whole source tree; testers only need this extracted runtime folder.
