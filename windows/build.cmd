@echo off
rem Build/test driver for the clang/mingw-w64 configuration on m93p.
rem Usage (from C:\dev\msword):  windows\build.cmd [build|test|clean|run]
rem Default action is build.

setlocal
set "PATH=C:\tools\msys64\mingw64\bin;%PATH%"
cd /d "%~dp0.."
set "REPO=%CD%"
rem CMake resolves --preset against the current directory's CMakePresets.json,
rem which lives in src\.
cd /d "%REPO%\src"

set "ACTION=%~1"
if "%ACTION%"=="" set "ACTION=build"

if /i "%ACTION%"=="clean" (
    rmdir /s /q "%REPO%\out-clang-mingw" 2>nul
    echo Cleaned out-clang-mingw.
    goto :eof
)

if not exist "%REPO%\out-clang-mingw\CMakeCache.txt" (
    cmake --preset x64-clang-mingw-debug || exit /b 1
)

if /i "%ACTION%"=="build" (
    cmake --build --preset x64-clang-mingw-debug || exit /b 1
    echo Build OK: %REPO%\bin\WORD1.exe
    goto :eof
)

if /i "%ACTION%"=="test" (
    cmake --build --preset x64-clang-mingw-debug || exit /b 1
    rem Focus/pixel UI tests need the interactive desktop; over SSH they fail
    rem by design of session 0, not because of a regression.
    ctest --test-dir "%REPO%\out-clang-mingw" -C Debug --output-on-failure
    goto :eof
)

if /i "%ACTION%"=="run" (
    start "" "%REPO%\bin\WORD1.exe"
    goto :eof
)

echo Unknown action "%ACTION%". Use: build ^| test ^| clean ^| run
exit /b 2
