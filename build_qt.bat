@echo off
setlocal

set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "VSDEVCMD=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat"
set "CMAKE=E:\CMAKE\bin\cmake.exe"
set "QT_PREFIX=D:\Qt\6.7.3\msvc2019_64"
set "BUILD_DIR=%ROOT%\build\nmake-qt"
set "EXE=%ROOT%\build\bin\football_auto_broadcast.exe"

if not exist "%VSDEVCMD%" (
    echo Visual Studio developer command not found: %VSDEVCMD%
    exit /b 1
)

if not exist "%CMAKE%" (
    echo CMake not found: %CMAKE%
    exit /b 1
)

if not exist "%QT_PREFIX%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo Qt 6 not found: %QT_PREFIX%
    exit /b 1
)

call "%VSDEVCMD%" -arch=x64
if errorlevel 1 exit /b 1

"%CMAKE%" -G "NMake Makefiles" -S "%ROOT%" -B "%BUILD_DIR%" -DCMAKE_PREFIX_PATH="%QT_PREFIX%" -DCMAKE_BUILD_TYPE=Release
if errorlevel 1 exit /b 1

"%CMAKE%" --build "%BUILD_DIR%"
if errorlevel 1 exit /b 1

"%QT_PREFIX%\bin\windeployqt.exe" --release "%EXE%"
if errorlevel 1 exit /b 1

echo.
echo Build complete: %EXE%
endlocal
