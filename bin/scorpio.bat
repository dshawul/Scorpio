@echo off

set CWD=%cd%

cd %~dp0 

for /f "delims=" %%a in ('findstr "^egbb_path" scorpio.ini') do @set LN=%%a
for /f "tokens=* delims= " %%a in ("%LN:~9%") do set LN=%%a
set PATH=%PATH%;%LN%

scorpio.exe %*

cd %CWD%
