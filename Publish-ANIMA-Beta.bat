@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "DIST=%ROOT%dist\ANIMA-Native-Studio-0.3-Beta"
set "ZIP=%ROOT%dist\ANIMA-Native-Studio-0.3-Beta-Portable.zip"
set "MSBUILD="

if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%I"
)
if not defined MSBUILD for /f "delims=" %%I in ('where msbuild 2^>nul') do if not defined MSBUILD set "MSBUILD=%%I"
if not defined MSBUILD (
  echo ERROR: MSBuild was not found. Install Visual Studio 2022 with Desktop development with C++.
  exit /b 2
)

echo.
echo [1/5] Building ANIMA Native Studio 0.3 Beta - Release x64...
"%MSBUILD%" "%ROOT%ANIMA_Studio.sln" /m /v:minimal /p:Configuration=Release /p:Platform=x64
if errorlevel 1 exit /b %errorlevel%

if not exist "%ROOT%x64\Release\AnimaStudio.exe" (
  echo ERROR: AnimaStudio.exe was not produced.
  exit /b 3
)
if not exist "%ROOT%x64\Release\anima-core.exe" (
  echo ERROR: anima-core.exe was not produced.
  exit /b 3
)

echo.
echo [2/5] Running Release language self-tests...
"%ROOT%x64\Release\anima-core.exe" self-test
if errorlevel 1 exit /b %errorlevel%

echo.
echo [3/5] Running Release language smoke suite...
set "ANIMA_EXE_OVERRIDE=%ROOT%x64\Release\anima-core.exe"
call "%ROOT%Anima03\Tests\Run-Smoke-Tests.bat"
if errorlevel 1 exit /b %errorlevel%
set "ANIMA_EXE_OVERRIDE="

echo.
echo [4/5] Staging portable folder...
if exist "%DIST%" rmdir /s /q "%DIST%"
if exist "%ZIP%" del /q "%ZIP%"
if not exist "%ROOT%dist" mkdir "%ROOT%dist"
mkdir "%DIST%"
robocopy "%ROOT%x64\Release" "%DIST%" /E /R:1 /W:1 /XD obj /XF *.pdb *.ilk *.exp *.lib >nul
set "ROBO=%ERRORLEVEL%"
if %ROBO% GEQ 8 (
  echo ERROR: Failed to stage Release files. Robocopy exit code %ROBO%.
  exit /b %ROBO%
)
copy /Y "%ROOT%PORTABLE_TESTER_README.txt" "%DIST%\README.txt" >nul
copy /Y "%ROOT%Launch-ANIMA-Studio.bat" "%DIST%\Launch-ANIMA-Studio.bat" >nul
copy /Y "%ROOT%ANIMA_NATIVE_0.3_BETA.txt" "%DIST%\VERSION.txt" >nul
if not exist "%DIST%\Assets\anima-monochrome.png" (
  echo ERROR: Logo asset is missing from portable package.
  exit /b 4
)
if not exist "%DIST%\Content\00-overview.md" (
  echo ERROR: Documentation content is missing from portable package.
  exit /b 4
)
if not exist "%DIST%\Anima03Examples\fibonacci.ani" (
  echo ERROR: 0.3 examples are missing from portable package.
  exit /b 4
)

(echo @echo off&echo setlocal&echo set "ROOT=%%~dp0"&echo if not exist "%%ROOT%%AnimaStudio.exe" ^(&echo   echo ANIMA Studio files are missing. Extract the complete portable ZIP first.&echo   pause&echo   exit /b 2&echo ^)&echo start "" "%%ROOT%%AnimaStudio.exe") > "%DIST%\Launch ANIMA Studio.bat"

echo.
echo [5/5] Creating portable ZIP...
powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '%DIST%\*' -DestinationPath '%ZIP%' -Force"
if errorlevel 1 (
  echo ERROR: Portable ZIP creation failed.
  exit /b 5
)

echo.
echo SUCCESS: Portable build created.
echo Folder: %DIST%
echo ZIP:    %ZIP%
echo.
echo Share the ZIP, not the source tree. Test it on a clean Windows account or PC before wider distribution.
exit /b 0
