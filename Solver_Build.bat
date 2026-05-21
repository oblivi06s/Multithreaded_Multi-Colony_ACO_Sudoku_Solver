@echo off
setlocal

echo ========================================================
echo SudoSoLVRR - Automated C++ Engine Build Script
echo ========================================================
echo.

echo [1/3] Locating Visual Studio Installation...
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

:: Check if vswhere exists
if not exist "%VSWHERE%" (
    echo ERROR: Visual Studio Installer not found. Please ensure Visual Studio 2017 or newer is installed.
    echo.
    pause
    exit /b 1
)

:: Use vswhere to find the exact path to VsDevCmd.bat
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.Component.MSBuild -find Common7\Tools\VsDevCmd.bat`) do (
    set "VS_CMD=%%i"
)

:: Check if the path was actually found
if not defined VS_CMD (
    echo ERROR: Could not find VsDevCmd.bat. Please ensure 'Desktop development with C++' is installed.
    echo.
    pause
    exit /b 1
)

echo Found Visual Studio environment at: 
echo "%VS_CMD%"
echo.

echo [2/3] Initializing Visual Studio Build Environment...
call "%VS_CMD%" -arch=x64

echo.
echo [3/3] Compiling the Multi-threaded ACO Solver...
msbuild ".\vs2017\sudoku_ants.vcxproj" /t:Clean,Build /p:Configuration=Release;Platform=x64;PlatformToolset=v143 /p:WindowsTargetPlatformVersion=10.0.22621.0 /m /bl:build.binlog

echo.
if %ERRORLEVEL% EQU 0 (
    echo ========================================================
    echo SUCCESS: The C++ engine was compiled successfully!
    echo You can now proceed to launch the Python web visualizer.
    echo ========================================================
) else (
    echo ========================================================
    echo ERROR: The build failed. Please check the terminal above.
    echo ========================================================
)

echo.
pause