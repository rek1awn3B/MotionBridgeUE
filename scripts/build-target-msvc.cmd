@echo off
setlocal

if "%~1"=="" (
    echo Usage: build-target-msvc.cmd target_name
    exit /b 2
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo Visual Studio Installer vswhere.exe was not found.
    exit /b 2
)

for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSINSTALL=%%i"
if not defined VSINSTALL (
    echo Visual Studio with C++ tools was not found.
    exit /b 2
)

call "%VSINSTALL%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%

cmake -S . -B build-nmake -G "NMake Makefiles"
if errorlevel 1 exit /b %errorlevel%

cmake --build build-nmake --target %~1
if errorlevel 1 exit /b %errorlevel%

build-nmake\%~1.exe
