@echo off
setlocal EnableExtensions EnableDelayedExpansion
REM ============================================================================
REM  PlayoutONE WebVT post-processing with RaDiff Voice Processor + Stereo Tool
REM
REM  Chain:  raw voice track -> RaDiffVoiceCLI (gate, de-ess, EQ, compression)
REM                          -> stereo_tool_cmd (station sound)  -> final file
REM
REM  Called by PlayoutONE with the same arguments as the previous REAPER/RX script:
REM    %1 input WAV   %2 final output WAV   %4 Stereo Tool .sts preset   %6 license key
REM
REM  RaDiffVoiceCLI never opens a sound card. It writes to a temp file next to the
REM  destination, verifies it and renames it into place; exit code 0 means verified
REM  success only. The license key is never written to the log.
REM
REM  Edit the three paths in the "Paths" block for your machine. Nothing else needs
REM  to change.
REM ============================================================================

REM === Paths (edit for your machine) ===
set "RADIFF=C:\PlayoutONE\Modules\RaDiff\RaDiffVoiceCLI.exe"
set "RADIFF_PRESET=C:\PlayoutONE\Modules\RaDiff\Presets\Station Voice.radvoice.json"
set "STEREOTOOL=C:\PlayoutONE\Modules\stereo_tool_cmd.exe"
set "WORKDIR=D:\WebVT Processing\Temp"
set "LOGDIR=D:\WebVT Processing\Logs"

if not exist "%WORKDIR%" mkdir "%WORKDIR%"
if not exist "%LOGDIR%" mkdir "%LOGDIR%"

REM === PlayoutONE arguments ===
set "INPUT=%~1"
set "FINAL_OUTPUT=%~2"
set "STS_FILE=%~4"
set "LICENSE_KEY=%~6"

REM === Unique temp names ===
for /f %%A in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss_fff"') do set "JOBID=%%A_%RANDOM%"

set "RADIFF_OUTPUT=%WORKDIR%\radiff_%JOBID%.wav"
set "LOGFILE=%LOGDIR%\process_%JOBID%.log"

echo === Processing Started === > "%LOGFILE%"
echo Input: %INPUT% >> "%LOGFILE%"
echo RaDiff preset: %RADIFF_PRESET% >> "%LOGFILE%"
echo Final Output: %FINAL_OUTPUT% >> "%LOGFILE%"
echo STS: %STS_FILE% >> "%LOGFILE%"
echo. >> "%LOGFILE%"

REM === Sanity checks before touching anything ===
if not exist "%INPUT%" (
    echo ERROR: Input file not found: %INPUT% >> "%LOGFILE%"
    exit /b 1
)
if not exist "%RADIFF%" (
    echo ERROR: RaDiffVoiceCLI.exe not found: %RADIFF% >> "%LOGFILE%"
    exit /b 1
)
if not exist "%RADIFF_PRESET%" (
    echo ERROR: RaDiff preset not found: %RADIFF_PRESET% >> "%LOGFILE%"
    exit /b 1
)

REM If no Stereo Tool preset was passed, RaDiff writes the final file directly.
set "USE_STEREOTOOL=1"
if "%STS_FILE%"=="" set "USE_STEREOTOOL=0"
if not exist "%STEREOTOOL%" set "USE_STEREOTOOL=0"
if "%USE_STEREOTOOL%"=="0" set "RADIFF_OUTPUT=%FINAL_OUTPUT%"

REM === STEP 1: RaDiff voice processing ===
REM  --wait-for-input 30     : wait until PlayoutONE has finished writing the take (size stable)
REM  --reject-silent-input   : an all-zero take is an error (exit 7), never silently "processed"
REM  Up to 3 attempts 3 s apart cover a take that was still being filled in when we started.
set "ATTEMPT=0"
:radiff_retry
set /a ATTEMPT+=1
echo Running RaDiff voice processing (attempt %ATTEMPT%)... >> "%LOGFILE%"
"%RADIFF%" --input "%INPUT%" --output "%RADIFF_OUTPUT%" --preset "%RADIFF_PRESET%" --overwrite --verbose --wait-for-input 30 --reject-silent-input >> "%LOGFILE%" 2>&1
set "RC=%ERRORLEVEL%"

if "%RC%"=="7" if %ATTEMPT% LSS 3 (
    echo Input read as digital silence; waiting 3 s and retrying... >> "%LOGFILE%"
    timeout /t 3 /nobreak >nul
    goto radiff_retry
)

if not "%RC%"=="0" (
    if "%RC%"=="2" echo ERROR: RaDiff usage error ^(bad arguments^) >> "%LOGFILE%"
    if "%RC%"=="3" echo ERROR: RaDiff could not read the input ^(missing, still being written, or unsupported^) >> "%LOGFILE%"
    if "%RC%"=="4" echo ERROR: RaDiff preset invalid or unreadable >> "%LOGFILE%"
    if "%RC%"=="5" echo ERROR: RaDiff processing failed >> "%LOGFILE%"
    if "%RC%"=="6" echo ERROR: RaDiff could not write the output >> "%LOGFILE%"
    if "%RC%"=="7" echo ERROR: RaDiff refused the input ^(digital silence^) or the output failed validation >> "%LOGFILE%"
    echo ERROR: RaDiff exited with code %RC% >> "%LOGFILE%"
    REM Keep a copy of the offending take so it can be examined later (Audiobin files are temporary).
    copy /Y "%INPUT%" "%WORKDIR%\failed_%JOBID%_input%~x1" >nul 2>&1 && echo Input preserved as %WORKDIR%\failed_%JOBID%_input%~x1 >> "%LOGFILE%"
    exit /b 1
)

if not exist "%RADIFF_OUTPUT%" (
    echo ERROR: RaDiff output not created: %RADIFF_OUTPUT% >> "%LOGFILE%"
    exit /b 1
)
echo RaDiff output created. >> "%LOGFILE%"

if "%USE_STEREOTOOL%"=="0" (
    echo Stereo Tool skipped ^(no .sts preset or stereo_tool_cmd.exe missing^). >> "%LOGFILE%"
    echo Success >> "%LOGFILE%"
    exit /b 0
)

REM === STEP 2: Stereo Tool on the RaDiff output ===
echo Running Stereo Tool... >> "%LOGFILE%"
"%STEREOTOOL%" "%RADIFF_OUTPUT%" "%FINAL_OUTPUT%" -s "%STS_FILE%" -k "%LICENSE_KEY%" >> "%LOGFILE%" 2>&1

if errorlevel 1 (
    echo ERROR: Stereo Tool failed >> "%LOGFILE%"
    exit /b 1
)

if not exist "%FINAL_OUTPUT%" (
    echo ERROR: Final output not created by Stereo Tool: %FINAL_OUTPUT% >> "%LOGFILE%"
    exit /b 1
)

echo Success >> "%LOGFILE%"

REM Optional cleanup of the intermediate file
del "%RADIFF_OUTPUT%" >nul 2>&1

exit /b 0
