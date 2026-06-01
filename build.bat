@echo off
setlocal enabledelayedexpansion

set ROOT=%~dp0
set LIB_DIR=%ROOT%filter\lib
set VCPKG_DIR=%ROOT%build\vcpkg

where nmake >nul 2>&1
if errorlevel 1 (
    for /f "tokens=*" %%i in ('"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath 2^>nul') do set VS_DIR=%%i
    if not defined VS_DIR (
        echo [ERROR] nmake not found ^& vswhere failed. Run from "x86 Native Tools Command Prompt for VS".
        exit /b 1
    )
    call "%VS_DIR%\VC\Auxiliary\Build\vcvarsall.bat" x86
)

echo === vcpkg setup + install deps ===
set OVERLAY=%ROOT%vcpkg_overlay\ports
if not exist "%VCPKG_DIR%" (
    git clone https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%"
)
if not exist "%VCPKG_DIR%\vcpkg.exe" (
    pushd "%VCPKG_DIR%"
    call bootstrap-vcpkg.bat
    popd
)
"%VCPKG_DIR%\vcpkg" install detours:x86-windows
"%VCPKG_DIR%\vcpkg" install libass:x86-windows-static --overlay-ports="%OVERLAY%"
"%VCPKG_DIR%\vcpkg" install zlib:x86-windows-static libpng:x86-windows-static

echo === Copy libs ===
if exist "%LIB_DIR%" del /f /q "%LIB_DIR%\*.lib" 2>nul
mkdir "%LIB_DIR%" 2>nul
copy /y "%VCPKG_DIR%\installed\x86-windows\lib\detours.lib" "%LIB_DIR%"
copy /y "%VCPKG_DIR%\installed\x86-windows-static\lib\ass.lib" "%LIB_DIR%"

echo === Build Filter ===
if not exist "%ROOT%filter\build" mkdir "%ROOT%filter\build"
pushd "%ROOT%filter\build"
cmake -A Win32 ..
cmake --build . --config Release
popd

echo === Build FPTool ===
if not exist "%ROOT%fptool\build" mkdir "%ROOT%fptool\build"
pushd "%ROOT%fptool\build"
cmake -A Win32 -DCMAKE_TOOLCHAIN_FILE="%VCPKG_DIR%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x86-windows-static ..
cmake --build . --config Release
popd

echo === Collect Artifacts ===
set DIST=%ROOT%build\Release
if not exist "%DIST%" mkdir "%DIST%"

copy /y "%ROOT%filter\build\Release\filter.dll" "%DIST%\" >nul 2>&1 && echo   filter.dll || echo   [skip] filter.dll
copy /y "%ROOT%filter\build\Release\loader.exe" "%DIST%\" >nul 2>&1 && echo   loader.exe || echo   [skip] loader.exe
copy /y "%ROOT%filter\build\Release\patch.tsv" "%DIST%\" >nul 2>&1 && echo   patch.tsv || echo   [skip] patch.tsv
copy /y "%VCPKG_DIR%\installed\x86-windows-static\bin\ass.dll" "%DIST%\" >nul 2>&1 && echo   ass.dll || echo   [skip] ass.dll
copy /y "%ROOT%fptool\build\Release\fptool.exe" "%DIST%\" >nul 2>&1 && echo   fptool.exe || echo   [skip] fptool.exe
copy /y "%ROOT%fptool\build\Release\rgba2rgb.exe" "%DIST%\" >nul 2>&1 && echo   rgba2rgb.exe || echo   [skip] rgba2rgb.exe

echo.
echo Done! Artifacts in dist\
