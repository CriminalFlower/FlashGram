@echo off
setlocal

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if %errorlevel% neq 0 exit /b 1

set "TEMP=D:\Temp"
set "TMP=D:\Temp"
set "PATH=%PATH%;%ROOT%\.cache\tools\Scripts"

if not exist "%ROOT%\flashgram.credentials.cmake" (
    echo [FlashGram] Run flashgram_setup_credentials.ps1 first.
    exit /b 1
)

if not exist "%BUILD_DIR%\CMakeCache.txt" (
    cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -T v143 ^
        -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON
    if errorlevel 1 exit /b 1
)

cmake --build "%BUILD_DIR%" --config Debug --target Telegram -- /m
if errorlevel 1 exit /b 1

if exist "%BUILD_DIR%\Debug\Telegram.exe" (
    echo [FlashGram] EXE: %BUILD_DIR%\Debug\Telegram.exe
) else (
    echo [FlashGram] Build finished but Telegram.exe was not found.
    exit /b 1
)
