@echo off
rem OutRunners for Dreamcast: makes the disc image from your own orunners.zip.
rem Run it and pick the file, or drop orunners.zip onto this file.
setlocal
set "ROM=%~1"
if "%ROM%"=="" (
  echo Choose your OutRunners ROM set ^(orunners.zip^) in the window that opens...
  for /f "usebackq delims=" %%F in (`powershell -NoProfile -STA -Command "Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; $d.Title = 'Select your OutRunners ROM set (orunners.zip)'; $d.Filter = 'ROM set (*.zip)|*.zip|All files (*.*)|*.*'; if ($d.ShowDialog() -eq 'OK') { $d.FileName }"`) do set "ROM=%%F"
)
if "%ROM%"=="" (
  echo No file chosen.
  pause
  exit /b 1
)
set "ROM=%ROM:"=%"
"%~dp0tools\python\python.exe" "%~dp0make_disc.py" "%ROM%"
echo.
pause
