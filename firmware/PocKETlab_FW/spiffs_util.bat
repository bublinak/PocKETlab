@echo off
REM SPIFFS Utility Script for PocKETlab (Windows)
REM This script helps manage SPIFFS operations

set ENVIRONMENT=PocKETlab
set PLATFORMIO_CMD=%USERPROFILE%\.platformio\penv\Scripts\platformio.exe

if "%1"=="build" (
    echo Building SPIFFS filesystem...
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t buildfs
    goto :eof
)

if "%1"=="upload" (
    echo Uploading SPIFFS filesystem...
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t uploadfs
    goto :eof
)

if "%1"=="erase" (
    echo Erasing flash...
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t erase
    goto :eof
)

if "%1"=="monitor" (
    echo Starting serial monitor...
    "%PLATFORMIO_CMD%" device monitor -e %ENVIRONMENT%
    goto :eof
)

if "%1"=="flash" (
    echo Building and uploading firmware...
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t upload
    goto :eof
)

if "%1"=="full" (
    echo Full deployment: erase -^> build -^> upload firmware -^> build SPIFFS -^> upload SPIFFS
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t erase
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t upload
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t buildfs
    "%PLATFORMIO_CMD%" run -e %ENVIRONMENT% -t uploadfs
    goto :eof
)

echo PocKETlab SPIFFS Utility
echo Usage: %0 {build^|upload^|erase^|monitor^|flash^|full}
echo.
echo Commands:
echo   build   - Build SPIFFS filesystem from data/ folder
echo   upload  - Upload SPIFFS filesystem to device
echo   erase   - Erase entire flash memory
echo   monitor - Start serial monitor
echo   flash   - Build and upload firmware only
echo   full    - Complete deployment (erase + firmware + SPIFFS)
