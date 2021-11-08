@echo off

set CWD=%cd%

cd %~dp0 

for /f "delims=" %%a in ('findstr "^egbb_path" scorpio.ini') do @set LN=%%a
for /f "tokens=* delims= " %%a in ("%LN:~9%") do set LN=%%a
set PATH=%LN%;%PATH%

mpiexec -aa -al 3:N ^
   -np 1 scorpio-mpich.exe vote_weight 300 pvstyle 0 frac_abprior 0 %* : ^
   -np 1 scorpio-mpich.exe vote_weight 100 pvstyle 0 use_nn 0 montecarlo 0 mt auto/2 %*

cd %CWD%
