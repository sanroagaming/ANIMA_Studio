@echo off
setlocal EnableExtensions
set "ROOT=%~dp0"
set "MSBUILD="
if exist "%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" (
  for /f "usebackq tokens=*" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do if not defined MSBUILD set "MSBUILD=%%I"
)
if not defined MSBUILD for /f "delims=" %%I in ('where msbuild 2^>nul') do if not defined MSBUILD set "MSBUILD=%%I"
if not defined MSBUILD (
  echo ERROR: MSBuild was not found. Install Visual Studio 2022 with Desktop development with C++.
  exit /b 2
)
echo Building ANIMA Native Studio 0.3 Beta...
"%MSBUILD%" "%ROOT%ANIMA_Studio.sln" /m /v:minimal /p:Configuration=Debug /p:Platform=x64
if errorlevel 1 exit /b %errorlevel%
echo.
echo Build succeeded. Running ANIMA 0.3 self-tests...
"%ROOT%x64\Debug\anima-core.exe" self-test
if errorlevel 1 exit /b %errorlevel%
call "%ROOT%Anima03\Tests\Run-Smoke-Tests.bat"
if errorlevel 1 exit /b %errorlevel%
echo.
echo Build and smoke check succeeded.
echo Launching native Studio...
if exist "%ROOT%x64\Debug\AnimaStudio.exe" start "" "%ROOT%x64\Debug\AnimaStudio.exe"
exit /b 0
