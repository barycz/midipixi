@echo off
rem Runs a command inside a Visual Studio x64 developer environment.
rem Usage: scripts\dev.cmd cmake --preset desktop
rem
rem Works with any Visual Studio that has the C++ desktop tools (2022, 2026, ...).
rem Which one is used:
rem   1. MPX_VS, when set: a major version (17 = VS 2022, 18 = VS 2026) or an
rem      installation folder.
rem   2. The one an existing build folder was configured with, down to the MSVC
rem      toolset version. A Ninja build folder stores the compiler's path but
rem      takes headers and libraries from this environment, so the two must
rem      match. The folder comes from --preset NAME (the presets build in
rem      build\NAME), --build DIR, -B DIR or --test-dir DIR. --fresh skips this.
rem   3. The newest one.
rem Also puts the local toolchain from .tools on PATH and sets PICO_SDK_PATH when present.
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "!VSWHERE!" for %%w in (vswhere.exe) do set "VSWHERE=%%~$PATH:w"
if not exist "!VSWHERE!" (
  echo dev.cmd: vswhere.exe not found. Install Visual Studio with the C++ desktop tools. 1>&2
  exit /b 1
)
set "VCREQ=-products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"

rem --- Build folder named on the command line ----------------------------------
set "BUILDDIR="
set "FRESH="
:args
if "%~1"=="" goto argsdone
if /i "%~1"=="--fresh" set "FRESH=1"
if /i "%~1"=="--preset" set "BUILDDIR=%ROOT%\build\%~2"
if /i "%~1"=="--build" call :dirarg "%~2"
if "%~1"=="-B" call :dirarg "%~2"
if /i "%~1"=="--test-dir" call :dirarg "%~2"
shift
goto args
:argsdone

rem --- Pick the Visual Studio ----------------------------------------------------
set "VSINSTALL="
set "TOOLSET="
if defined MPX_VS goto fromenv
if defined FRESH goto newest
if not defined BUILDDIR goto newest
if not exist "!BUILDDIR!\CMakeCache.txt" goto newest
set "CXX="
for /f "tokens=1,* delims==" %%a in ('findstr /b /c:"CMAKE_CXX_COMPILER:" "!BUILDDIR!\CMakeCache.txt"') do set "CXX=%%b"
if not defined CXX goto newest
set "CXX=!CXX:/=\!"
if /i not "!CXX:~-6!"=="cl.exe" goto newest
for /f "usebackq delims=" %%i in (`call "!VSWHERE!" -all !VCREQ! -property installationPath`) do (
  set "REST=!CXX:*%%i\=!"
  if not "!REST!"=="!CXX!" (
    set "VSINSTALL=%%i"
    for /f "tokens=1-4 delims=\" %%a in ("!REST!") do if /i "%%a\%%b\%%c"=="VC\Tools\MSVC" set "TOOLSET=%%d"
  )
)
if not defined VSINSTALL (
  echo dev.cmd: !BUILDDIR! was configured with !CXX!, which belongs to no installed Visual Studio. 1>&2
  echo dev.cmd: configure it again with --fresh, e.g. scripts\dev.cmd cmake --preset NAME --fresh 1>&2
  exit /b 1
)
if defined TOOLSET if not exist "!VSINSTALL!\VC\Tools\MSVC\!TOOLSET!\bin" (
  echo dev.cmd: !BUILDDIR! was configured with MSVC !TOOLSET!, which is no longer installed. 1>&2
  echo dev.cmd: configure it again with --fresh, e.g. scripts\dev.cmd cmake --preset NAME --fresh 1>&2
  exit /b 1
)
goto found

:fromenv
if exist "!MPX_VS!\VC\Auxiliary\Build\vcvars64.bat" set "VSINSTALL=!MPX_VS!"
if defined VSINSTALL goto found
set /a "VSMAJOR=MPX_VS" 2>nul
set /a "VSNEXT=VSMAJOR+1"
if not "!VSMAJOR!"=="!MPX_VS!" (
  echo dev.cmd: MPX_VS=!MPX_VS! is neither a major version ^(17, 18, ...^) nor a Visual Studio folder. 1>&2
  exit /b 1
)
for /f "usebackq delims=" %%i in (`call "!VSWHERE!" -latest !VCREQ! -version "[!VSMAJOR!.0,!VSNEXT!.0)" -property installationPath`) do set "VSINSTALL=%%i"
if defined VSINSTALL goto found
echo dev.cmd: no Visual Studio !VSMAJOR! with the C++ desktop tools found ^(MPX_VS^). 1>&2
exit /b 1

:newest
for /f "usebackq delims=" %%i in (`call "!VSWHERE!" -latest !VCREQ! -property installationPath`) do set "VSINSTALL=%%i"
if defined VSINSTALL goto found
echo dev.cmd: no Visual Studio with the C++ desktop tools found. 1>&2
exit /b 1

rem --- Environment ---------------------------------------------------------------
:found
set "VCVARS_ARGS="
if defined TOOLSET set "VCVARS_ARGS=-vcvars_ver=!TOOLSET!"
rem The command runs without delayed expansion, so "!" in its arguments survives.
setlocal DisableDelayedExpansion
set "VCToolsInstallDir="
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" %VCVARS_ARGS% >nul 2>&1
if not defined VCToolsInstallDir goto vcfail
echo dev.cmd: %VSINSTALL%, MSVC %VCToolsVersion% 1>&2
if exist "%ROOT%\.tools\arm-gnu-toolchain\bin" set "PATH=%ROOT%\.tools\arm-gnu-toolchain\bin;%PATH%"
if not defined PICO_SDK_PATH if exist "%ROOT%\.tools\pico-sdk" set "PICO_SDK_PATH=%ROOT%\.tools\pico-sdk"
%*
exit /b

:vcfail
echo dev.cmd: setting up the environment of %VSINSTALL% failed. 1>&2
exit /b 1

rem Sets BUILDDIR from a --build / -B / --test-dir value (not from a following option).
:dirarg
set "D=%~1"
if not defined D exit /b 0
if "!D:~0,1!"=="-" exit /b 0
set "BUILDDIR=%~1"
exit /b 0
