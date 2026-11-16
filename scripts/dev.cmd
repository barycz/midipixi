@echo off
rem Runs a command inside a Visual Studio 2022 x64 developer environment.
rem Usage: scripts\dev.cmd cmake --preset desktop
rem Also puts the local toolchain from .tools on PATH and sets PICO_SDK_PATH when present.
setlocal
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`call "%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
  echo Visual Studio with C++ tools not found
  exit /b 1
)
call "%VSINSTALL%\VC\Auxiliary\Build\vcvars64.bat" >nul
set "ROOT=%~dp0.."
if exist "%ROOT%\.tools\arm-gnu-toolchain\bin" set "PATH=%ROOT%\.tools\arm-gnu-toolchain\bin;%PATH%"
if not defined PICO_SDK_PATH if exist "%ROOT%\.tools\pico-sdk" set "PICO_SDK_PATH=%ROOT%\.tools\pico-sdk"
%*
