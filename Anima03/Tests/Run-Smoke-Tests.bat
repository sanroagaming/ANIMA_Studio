@echo off
setlocal
set "ANIMADIR=%~dp0.."
set "ROOT=%ANIMADIR%\.."
if defined ANIMA_EXE_OVERRIDE (
  set "EXE=%ANIMA_EXE_OVERRIDE%"
) else (
  set "EXE=%ROOT%\x64\Debug\anima-core.exe"
  if not exist "%EXE%" set "EXE=%ROOT%\x64\Release\anima-core.exe"
)
if not exist "%EXE%" set "EXE=%ROOT%\x64\Debug\anima.exe"
if not exist "%EXE%" set "EXE=%ROOT%\x64\Release\anima.exe"
if not exist "%EXE%" set "EXE=%ANIMADIR%\x64\Debug\anima-core.exe"
if not exist "%EXE%" set "EXE=%ANIMADIR%\x64\Debug\anima.exe"
if not exist "%EXE%" set "EXE=%ANIMADIR%\x64\Release\anima-core.exe"
if not exist "%EXE%" set "EXE=%ANIMADIR%\x64\Release\anima.exe"
if not exist "%EXE%" (
  echo ERROR: anima.exe was not found.
  echo Build ANIMA_Studio.sln or Anima03.sln in Visual Studio first.
  exit /b 2
)
"%EXE%" self-test || exit /b 1
"%EXE%" check "%ANIMADIR%\Examples\fibonacci.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\fibonacci.ani" || exit /b 1
"%EXE%" check "%ANIMADIR%\Examples\machine-inspector.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\machine-inspector.ani" || exit /b 1
"%EXE%" check "%ANIMADIR%\Examples\fixed-step-simulation.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\fixed-step-simulation.ani" || exit /b 1
"%EXE%" check "%ANIMADIR%\Examples\map-iteration.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\map-iteration.ani" || exit /b 1
"%EXE%" check "%ANIMADIR%\Examples\enums-and-records.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\enums-and-records.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\Modules\module-use.ani" || exit /b 1
"%EXE%" run "%ANIMADIR%\Examples\Modules\enum-default-test.ani" || exit /b 1
"%EXE%" ui-catalog Button || exit /b 1
"%EXE%" dis "%ANIMADIR%\Examples\fibonacci.ani" >nul || exit /b 1
"%EXE%" test "%ANIMADIR%\Tests\conformance-smoke.ani" || exit /b 1
"%EXE%" scene "%ANIMADIR%\Examples\SpecificationTargets\ui-two-scenes.ani" || exit /b 1
set "MIGRATED_DIR=%TEMP%\anima-03a-migration-smoke"
if not exist "%MIGRATED_DIR%" mkdir "%MIGRATED_DIR%"
for %%S in (definitions math-and-conditions native-power) do (
  "%EXE%" migrate "%ANIMADIR%\Reference\0.2-examples\%%S.ani" -o "%MIGRATED_DIR%\%%S.ani" || exit /b 1
  "%EXE%" run "%MIGRATED_DIR%\%%S.ani" || exit /b 1
  "%EXE%" run "%ANIMADIR%\Reference\0.2-examples\%%S.ani" --compat 0.2 || exit /b 1
)
del /Q "%MIGRATED_DIR%\*.*" >nul 2>nul
rmdir "%MIGRATED_DIR%" >nul 2>nul
echo ANIMA 0.3 integrated checker/VM smoke suite passed.
exit /b 0
