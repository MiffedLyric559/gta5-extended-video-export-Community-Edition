@echo off
setlocal enabledelayedexpansion

echo Extended Video Export - Build Script
echo.

set BUILD_TYPE=Release
echo Build Configuration: %BUILD_TYPE%
echo.

where cl >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo WARNING: Visual Studio C++ compiler not found in PATH
    echo.
    echo Searching for Visual Studio installation...
    
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    
    if exist "!VSWHERE!" (
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
            set "VSINSTALL=%%i"
        )
        
        if exist "!VSINSTALL!\VC\Auxiliary\Build\vcvarsall.bat" (
            echo Found Visual Studio 2022 Community
            call "!VSINSTALL!\VC\Auxiliary\Build\vcvarsall.bat" x64
        ) else (
            echo ERROR: Could not find vcvarsall.bat
            pause
            exit /b 1
        )
    ) else (
        echo ERROR: Could not find Visual Studio
        pause
        exit /b 1
    )
)

echo.
echo Installing dependencies...
vcpkg\vcpkg.exe install --triplet=x64-windows
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to install vcpkg dependencies
    pause
    exit /b 1
)

echo.
echo Configuring CMake...
cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -T v142 -G "Visual Studio 17 2022"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
    pause
    exit /b 1
)

echo.
echo Building project...
cmake --build build --config %BUILD_TYPE%
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Build failed
    pause
    exit /b 1
)

echo Build completed successfully!
