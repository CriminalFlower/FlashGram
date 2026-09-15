@echo off
setlocal

set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\build"

call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44
if %errorlevel% neq 0 exit /b 1

set "TEMP=D:\Temp"
set "TMP=D:\Temp"
set "QT=5.15.19"
set "PATH=%PATH%;%ROOT%\.cache\tools\Scripts"

rem FlashGram reads API credentials at runtime from flashgram_api.json next to
rem FlashGram.exe, so no personal api_id/api_hash is compiled into the binary.
rem The compile-time values are only Telegram's public placeholders.
set "API_ARGS=-D TDESKTOP_API_TEST=ON"
if not exist "%BUILD_DIR%\Debug\flashgram_api.json" (
    echo [FlashGram] NOTE: build\Debug\flashgram_api.json not found.
    echo [FlashGram] Run flashgram_setup_credentials.ps1 before logging in.
)

cmake -S "%ROOT%" -B "%BUILD_DIR%" -G "Visual Studio 17 2022" -A x64 -T v143 ^
    -D DESKTOP_APP_DISABLE_AUTOUPDATE=ON %API_ARGS%
if errorlevel 1 exit /b 1

rem 20 MSBuild nodes each running cl /MP with 20 threads exhausts the
rem commit limit on this machine (32 GB RAM, 4 GB page file), so cap both.
set "STAMP=%BUILD_DIR%\flashgram_build_started.stamp"
echo started> "%STAMP%"
cmake --build "%BUILD_DIR%" --config Debug --target Telegram -- /m:2 /p:CL_MPCount=6 /nr:false
set "BUILD_EXIT=%errorlevel%"
echo [FlashGram] BUILD_EXIT=%BUILD_EXIT%
if not "%BUILD_EXIT%"=="0" exit /b 1

powershell -NoProfile -Command "if ((Test-Path '%BUILD_DIR%\Debug\Telegram.exe') -and ((Get-Item '%BUILD_DIR%\Debug\Telegram.exe').LastWriteTime -gt (Get-Item '%STAMP%').LastWriteTime)) { exit 0 } else { exit 1 }"
if errorlevel 1 (
    echo [FlashGram] Telegram.exe was not relinked by this build.
    exit /b 1
)
copy /Y "%BUILD_DIR%\Debug\Telegram.exe" "%BUILD_DIR%\Debug\FlashGram.exe" >nul
if errorlevel 1 (
    echo [FlashGram] Could not create FlashGram.exe, is it running?
    exit /b 1
)
echo [FlashGram] EXE: %BUILD_DIR%\Debug\FlashGram.exe
echo [FlashGram] Internal target: %BUILD_DIR%\Debug\Telegram.exe
