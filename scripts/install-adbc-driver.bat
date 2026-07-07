@echo off
REM DuckDB Snowflake ADBC Driver Installer for Windows
REM Usage: install-adbc-driver.bat

setlocal enabledelayedexpansion

REM Configuration
REM The Snowflake driver moved from apache/arrow-adbc to adbc-drivers/snowflake
REM (release line go/vX.Y.Z). The old wheels are deprecated and lack GeoArrow
REM GEOGRAPHY/GEOMETRY support.
set DRIVER_VERSION=1.11.0
set BASE_URL=https://github.com/adbc-drivers/snowflake/releases/download/go/v%DRIVER_VERSION%

REM Colors for output (if supported)
set INFO=[INFO]
set SUCCESS=[OK]
set ERROR=[ERROR]
set WARNING=[WARN]

REM Detect platform architecture
set ARCH=%PROCESSOR_ARCHITECTURE%
if "%ARCH%"=="AMD64" (
    set PLATFORM=windows_amd64
    set ASSET_NAME=snowflake_windows_amd64_v%DRIVER_VERSION%.tar.gz
) else if "%ARCH%"=="ARM64" (
    echo %ERROR% Windows ARM64 is not supported by the ADBC Snowflake driver
    echo.
    echo The ADBC Driver Foundry only provides pre-built binaries for:
    echo   - Windows x86_64 (AMD64)
    echo   - Linux x86_64 and ARM64
    echo   - macOS ARM64
    echo.
    echo To use DuckDB Snowflake on Windows ARM64, you would need to:
    echo   1. Build the ADBC driver from source
    echo   2. Request Windows ARM64 support from Apache Arrow team
    echo   3. Use Windows x86_64 emulation (performance may be affected)
    echo.
    exit /b 1
) else (
    echo %ERROR% Unsupported architecture: %ARCH%
    echo Only Windows x86_64 (AMD64) is supported
    exit /b 1
)

echo.
echo ================================================================
echo      DuckDB Snowflake ADBC Driver Installer

REM Detect DuckDB version
echo %INFO% Detecting DuckDB version...
for /f "tokens=*" %%i in ('duckdb -c "SELECT version()" 2^>nul') do set DUCKDB_OUTPUT=%%i

REM Extract version from output (format: "v1.4.2")
for /f "tokens=*" %%v in ("%DUCKDB_OUTPUT%") do (
    set VERSION_LINE=%%v
)

REM Parse version - extract vX.Y.Z pattern
echo %VERSION_LINE% | findstr /r "v[0-9]*\.[0-9]*\.[0-9]*" >nul
if errorlevel 1 (
    echo %ERROR% Could not detect DuckDB version. Is DuckDB installed?
    echo Please install DuckDB from https://duckdb.org/docs/installation/
    exit /b 1
)

REM Extract version using string manipulation
for /f "tokens=2 delims= " %%a in ("%VERSION_LINE%") do set DUCKDB_VERSION=%%a

echo      DuckDB Version: %DUCKDB_VERSION%
echo ================================================================
echo.

REM Determine installation directory
if defined DUCKDB_EXTENSION_DIR (
    set INSTALL_DIR=%DUCKDB_EXTENSION_DIR%
) else (
    REM Default to DuckDB extension directory
    set INSTALL_DIR=%USERPROFILE%\.duckdb\extensions\%DUCKDB_VERSION%\%PLATFORM%
)

echo %INFO% Detected platform: Windows x86_64 (%PLATFORM%)
echo %INFO% Installation directory: %INSTALL_DIR%

REM Create directory if it doesn't exist
if not exist "%INSTALL_DIR%" (
    mkdir "%INSTALL_DIR%"
    if errorlevel 1 (
        echo %ERROR% Failed to create installation directory
        exit /b 1
    )
)

REM Download the release tarball
set ASSET_PATH=%INSTALL_DIR%\%ASSET_NAME%
set ASSET_URL=%BASE_URL%/%ASSET_NAME%

if exist "%ASSET_PATH%" (
    echo %WARNING% Driver tarball already exists, checking if extraction is needed...
) else (
    echo %INFO% Downloading ADBC Snowflake driver...
    echo %INFO% URL: %ASSET_URL%

    REM Try PowerShell download first
    powershell -Command "& {Invoke-WebRequest -Uri '%ASSET_URL%' -OutFile '%ASSET_PATH%' -ErrorAction Stop}" 2>nul

    if errorlevel 1 (
        echo %ERROR% Failed to download ADBC driver
        echo Please check your internet connection and try again
        exit /b 1
    )

    echo %SUCCESS% Downloaded %ASSET_NAME%
)

REM Extract the driver from the tarball (tar.exe ships with Windows 10+)
echo %INFO% Extracting driver library...

cd /d "%INSTALL_DIR%"

tar -xzf "%ASSET_NAME%" 2>nul

if errorlevel 1 (
    echo %ERROR% Failed to extract driver library from tarball
    exit /b 1
)

REM The extension resolves the driver by the fixed name
REM libadbc_driver_snowflake.so on every platform (see CMakeLists.txt),
REM so rename the .dll accordingly.
if exist "libadbc_driver_snowflake.dll" (
    move /Y "libadbc_driver_snowflake.dll" "libadbc_driver_snowflake.so" >nul
    echo %SUCCESS% Extracted libadbc_driver_snowflake.so
) else (
    echo %ERROR% Driver library not found in tarball
    exit /b 1
)

REM Verify installation
echo %INFO% Verifying installation...
set FINAL_PATH=%INSTALL_DIR%\libadbc_driver_snowflake.so

if exist "%FINAL_PATH%" (
    for %%A in ("%FINAL_PATH%") do set FILE_SIZE=%%~zA
    set /a FILE_SIZE_MB=!FILE_SIZE! / 1048576

    echo %SUCCESS% ADBC Snowflake driver installed successfully!
    echo.
    echo Installation details:
    echo   * Driver: libadbc_driver_snowflake.so
    echo   * Location: %FINAL_PATH%
    echo   * Size: ~!FILE_SIZE_MB! MB
    echo   * Version: %DRIVER_VERSION%
    echo.
    echo To use with DuckDB Snowflake extension:
    echo   1. The extension will automatically find the driver at:
    echo      %FINAL_PATH%
    echo   2. If you have issues, set the environment variable:
    echo      set SNOWFLAKE_ADBC_DRIVER_PATH=%FINAL_PATH%
    echo.
    echo %SUCCESS% Installation complete! You can now use the Snowflake extension.
) else (
    echo %ERROR% Installation verification failed
    exit /b 1
)

endlocal
