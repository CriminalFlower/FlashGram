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

if exist "%ROOT%\flashgram.credentials.cmake" (
    echo [FlashGram] Using local credentials from flashgram.credentials.cmake.
    set "API_ARGS=-D TDESKTOP_API_TEST=OFF -D TDESKTOP_API_ID=0 -D TDESKTOP_API_HASH="
) else (
    echo [FlashGram] WARNING: flashgram.credentials.cmake not found.
    echo [FlashGram] Building with Telegram TEST API credentials, which are very limited.
    echo [FlashGram] Run flashgram_setup_credentials.ps1 and build again for real use.
    set "API_ARGS=-D TDESKTOP_API_TEST=ON"
)

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -T v143 ^
    -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON %API_ARGS%
if errorlevel 1 exit /b 1

cmake --build "%BUILD_DIR%" --config Debug --target Telegram -- /m
if errorlevel 1 exit /b 1

if exist "%BUILD_DIR%\Debug\Telegram.exe" (
    echo [FlashGram] EXE: %BUILD_DIR%\Debug\Telegram.exe
) else (
    echo [FlashGram] Build finished but Telegram.exe was not found.
    exit /b 1
)
