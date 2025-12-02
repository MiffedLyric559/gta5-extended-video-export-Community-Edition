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
    
    if not exist "!VSWHERE!" (
        echo ERROR: Could not find vswhere.exe
        exit /b 1
    )
    
    echo Checking for required Visual Studio components...
    
    REM Check for C++ Build Tools
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VSINSTALL_CPP=%%i"
    )
    
    if not defined VSINSTALL_CPP (
        echo ERROR: Visual Studio C++ Build Tools not found
        echo.
        echo Please install "Desktop development with C++" workload
        echo Open Visual Studio Installer and modify your installation
        exit /b 1
    )
    
    REM Check for Windows SDK
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -products * -requires Microsoft.VisualStudio.Component.Windows10SDK -property installationPath`) do (
        set "VSINSTALL_SDK=%%i"
    )
    
    if not defined VSINSTALL_SDK (
        REM Try Windows 11 SDK if Windows 10 SDK not found
        for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -products * -requires Microsoft.VisualStudio.Component.Windows11SDK -property installationPath`) do (
            set "VSINSTALL_SDK=%%i"
        )
    )
    
    if not defined VSINSTALL_SDK (
        echo WARNING: Windows SDK component not detected via vswhere
        echo Checking for Windows Kits installation...
        if exist "C:\Program Files (x86)\Windows Kits\10\" (
            set "VSINSTALL_SDK=found"
            echo Found Windows 10 SDK
        )
    )
    
    if not defined VSINSTALL_SDK (
        echo ERROR: Windows SDK not found
        echo.
        echo Please install Windows 10 SDK or Windows 11 SDK
        echo Open Visual Studio Installer ^> Modify ^> Individual Components
        echo Search for "Windows SDK" and install a recent version
        exit /b 1
    )
    
    REM Check for MSVC compiler
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -products * -requires Microsoft.VisualStudio.Component.VC.14.43.17.11.x86.x64 -property installationPath`) do (
        set "VSINSTALL_MSVC=%%i"
    )
    
    if not defined VSINSTALL_MSVC (
        echo WARNING: Specific MSVC v143 toolset not found, trying latest available...
    )
    
    REM Find a complete installation with C++ tools
    for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvarsall.bat" (
            set "VSINSTALL=%%i"
            goto :found_vs
        )
    )
    
    echo ERROR: Could not find a complete Visual Studio installation
    echo.
    echo Required components:
    echo   [X] Desktop development with C++
    echo   [X] MSVC v143 - VS 2022 C++ x64/x86 build tools
    echo   [?] Windows SDK detected but may not be properly registered
    echo.
    echo Please open Visual Studio Installer and verify all components are installed
    exit /b 1
    
    :found_vs
    echo.
    echo Found complete Visual Studio installation at: !VSINSTALL!
    echo   [OK] C++ Build Tools
    echo   [OK] Windows SDK
    echo   [OK] MSVC Compiler
    echo.
    
    call "!VSINSTALL!\VC\Auxiliary\Build\vcvarsall.bat" x64
    
    REM Verify C++ compiler is now available
    where cl >nul 2>&1
    if !ERRORLEVEL! NEQ 0 (
        echo ERROR: C++ compiler not found after running vcvarsall.bat
        echo Please ensure "Desktop development with C++" workload is installed
        exit /b 1
    )
    echo C++ compiler found: cl.exe
    
    REM Force the correct Visual Studio path AFTER vcvarsall (it may override)
    set "VSINSTALLDIR=!VSINSTALL!\"
    set "VCINSTALLDIR=!VSINSTALL!\VC\"
    
    REM Clear conflicting vcpkg environment variables  
    set "VCPKG_ROOT="
    set "VCPKG_INSTALLATION_ROOT="
) else (
    echo Visual Studio C++ compiler found in PATH
)

echo.
echo Checking vcpkg installation...
if not exist "vcpkg\vcpkg.exe" (
    echo vcpkg not found. Bootstrapping vcpkg...
    if not exist "vcpkg\bootstrap-vcpkg.bat" (
        echo ERROR: vcpkg submodule not initialized
        echo Please run: git submodule update --init --recursive
        exit /b 1
    )
    
    pushd vcpkg
    call bootstrap-vcpkg.bat
    if %ERRORLEVEL% NEQ 0 (
        echo ERROR: Failed to bootstrap vcpkg
        popd
        exit /b 1
    )
    popd
    echo vcpkg bootstrapped successfully!
) else (
    echo vcpkg already installed.
)

echo.
echo Installing dependencies...
echo VSINSTALL: %VSINSTALL%
echo VSINSTALLDIR: %VSINSTALLDIR%
echo.
echo Verifying Visual Studio installation...
if not exist "%VSINSTALL%\VC\Tools\MSVC" (
    echo ERROR: Visual Studio MSVC tools not found at %VSINSTALL%
    exit /b 1
)
echo Visual Studio installation verified.
echo.
echo Forcing Visual Studio path for vcpkg...
set "VSINSTALLDIR=%VSINSTALL%\"
set "VCINSTALLDIR=%VSINSTALL%\VC\"
echo Updated VSINSTALLDIR: %VSINSTALLDIR%
set "VCPKG_VISUAL_STUDIO_PATH=%VSINSTALL%"
set "VCPKG_PLATFORM_TOOLSET=v142"
set "VCPKG_PLATFORM_TOOLSET_VERSION=14.29"
vcpkg\vcpkg.exe install --triplet=x64-windows --vcpkg-root=vcpkg
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to install vcpkg dependencies
    exit /b 1
)

echo.
echo Configuring CMake...
if exist "build\CMakeCache.txt" (
    echo Cleaning old CMake cache...
    del /Q build\CMakeCache.txt 2>nul
    rmdir /S /Q build\CMakeFiles 2>nul
)
cmake -B build -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_TOOLCHAIN_FILE=vcpkg/scripts/buildsystems/vcpkg.cmake -T v142 -G "Visual Studio 17 2022"
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: CMake configuration failed
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
