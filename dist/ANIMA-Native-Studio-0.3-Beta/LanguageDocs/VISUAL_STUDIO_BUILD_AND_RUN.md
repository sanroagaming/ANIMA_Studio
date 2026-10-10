# One build, one launcher

## Fast path
1. Extract the entire ZIP to a normal folder.
2. Run `Build-ANIMA.bat`, or open `ANIMA_Studio.sln` in Visual Studio 2022.
3. In Visual Studio choose `Debug | x64`, then **Build Solution**.
4. Run `Run-ANIMA.bat` to open the native docs, or invoke `x64\Debug\anima.exe`.

## Unified commands
- `anima` or `anima docs`: native documentation app.
- `anima room`: native Direct3D 12 room prototype.
- `anima run Anima03Examples\fibonacci.ani`: run ANIMA 0.3 bytecode VM.
- `anima check <file.ani>`, `anima test <file-or-folder>`, `anima dis <file.ani>`.
- `anima migrate <old-file> -o <new-file.ani>`: experimental migration with review report.
- `anima legacy <args>`: use the preserved 0.2 reference runtime.
- `anima new <folder>`: create an ANIMA starter project.
- `anima doctor`: verify companion executables exist.

All Visual Studio projects output to `x64\Debug` or `x64\Release`, with separate intermediate directories. This is important because the hub locates the applications beside `anima.exe`.

## Limits
This is a unified development build, not a finished engine or AOT distribution. Some spec features and the Windows hardware GPU path remain to be completed/verified.
