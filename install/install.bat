@ECHO off
SET VERSION=3.0
SET VR=30
SET OSD=windows

IF "%1"=="-h" (
    :usage
    echo Usage: %0
    echo
    echo    -h   Display this help message.
    echo    -p   Use precision FLOAT/HALF/INT8.
    echo    -t   Threads per GPU/CPU cores.
    echo
    echo Example: install.bat -p INT8 - t 80
    exit /b
)

REM --------- Nvidia GPU
WHERE nvcuda.dll >nul 2>nul
IF %ERRORLEVEL% NEQ 0 (
  SET GPUS=0
  SET DEV=cpu
) ELSE (
  SET GPUS=1
  SET DEV=gpu
)

SET EGBB=nnprobe-%OSD%-%DEV%
SET STMP=%time:~3,2%-%time:~6,2%
SET LNK=http://github.com/dshawul/Scorpio/releases/download

REM --------- create directory
SET SCORPIO=Scorpio-%STMP%
mkdir %SCORPIO%
IF EXIST RMDIR /S /Q Scorpio
MKLINK /D Scorpio %SCORPIO%
cd %SCORPIO%
SET CWD=%cd%\

REM --------- download nnprobe
SET FILENAME=%EGBB%.zip
bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
powershell Expand-Archive %CWD%%FILENAME% -DestinationPath %CWD%
DEL %CWD%%FILENAME%

REM --------- download nnprobe
SET FILENAME=egbb.zip
bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
powershell Expand-Archive %CWD%%FILENAME% -DestinationPath %CWD%
DEL %CWD%%FILENAME%

REM --------- download scorpio binary
SET FILENAME=scorpio%VR%-mcts-nn.zip
bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
powershell Expand-Archive %CWD%%FILENAME% -DestinationPath %CWD%
DEL %CWD%%FILENAME%

REM --------- download networks
IF %GPUS% NEQ 0 (
    SET NETS=nets-scorpio.zip nets-lczero.zip nets-maddex.zip
) ELSE (
    SET NETS=nets-scorpio.zip
)
for %%N in ( %NETS% ) DO (
    bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%%N" %CWD%%%N
    powershell Expand-Archive %CWD%%%N -DestinationPath %CWD%
    DEL %CWD%%%N
)

cd %EGBB%
icacls "*.*" /grant %USERNAME%:F
cd ..

REM ---------- paths
SET egbbp=%CWD%%EGBB%
SET egbbfp=%CWD%egbb
SET EXE="%CWD%bin/Windows/scorpio.bat"

IF %GPUS% NEQ 0 (
  SET nnp=%CWD%nets-scorpio/ens-net-20x256.uff
  SET nnp_e=%CWD%nets-maddex/ME.uff
  SET nnp_m=
  SET nn_type=0
  SET nn_type_e=1
  SET nn_type_m=-1
  SET wdl_head=0
  SET wdl_head_e=1
  SET wdl_head_m=0
) ELSE (
  SET nnp=%CWD%nets-scorpio/ens-net-12x128.pb
  SET nnp_e=%CWD%nets-scorpio/ens-net-12x128.pb
  SET nnp_m=
  SET nn_type=0
  SET nn_type_e=-1
  SET nn_type_m=-1
  SET wdl_head=0
  SET wdl_head_e=0
  SET wdl_head_m=0
)

REM --------- determine GPU props
IF %GPUS% NEQ 0 (
    cd %EGBB%
    CALL device.exe
    FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe -n`) DO (
       SET GPUS=%%F
    )
    FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe --mp`) DO (
       SET THREADS=%%F
    )
    SET /a THREADS=%THREADS%*2

    REM ----- precision
    SET PREC=HALF
    FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe --fp16`) DO (
       SET HAS=%%F
    )
    IF "%HAS%"=="N" (
       FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe --int8`) DO (
          SET HAS=%%F
       )
       IF "%HAS%"=="Y" (
          SET PREC=INT8
       ) ELSE (
          SET PREC=FLOAT
       )
    )
    cd ..
) ELSE (
    SET PREC=FLOAT
    SET THREADS=4
)
REM --------- overwrite params
:loop
IF NOT "%1"=="" (
    IF "%1"=="-p" (
        SET PREC=%2
        SHIFT
    ) ELSE IF "%1"=="-t" (
        SET THREADS=%2
        SHIFT
    )
    SHIFT
    GOTO :loop
)

REM ---------- number of threads
SET delay=0
SET rt=0
IF %GPUS% NEQ 0 (
    SET /a mt=%GPUS%*%THREADS%
    SET /a rt=%mt%/%NUMBER_OF_PROCESSORS%
    IF %rt% GEQ 10 (
        SET delay=1
    )
) ELSE (
    SET /a mt=%NUMBER_OF_PROCESSORS%*%THREADS%
    SET delay=1
)

REM ---------- edit scorpio.ini
cd "%CWD%bin/Windows"
SETLOCAL ENABLEDELAYEDEXPANSION
IF EXIST output.txt DEL /F output.txt
for /F "delims=" %%A in (scorpio.ini) do (
   SET LMN=%%A
   IF /i "!LMN:~0,9!"=="egbb_path" (
     echo egbb_path                %egbbp%>> output.txt
   ) ELSE IF /i "!LMN:~0,15!"=="egbb_files_path" (
     echo egbb_files_path          %egbbfp%>> output.txt
   ) ELSE IF /i "!LMN:~0,2!"=="mt" (
     echo mt                  %mt% >> output.txt
   ) ELSE IF /i "!LMN:~0,5!"=="delay" (
     echo delay                    %delay% >> output.txt
   ) ELSE IF /i "!LMN:~0,11!"=="device_type" (
     IF %GPUS% NEQ 0 (
        echo device_type              GPU>> output.txt
     ) ELSE (
        echo device_type              CPU>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,9!"=="n_devices" (
     IF %GPUS% NEQ 0 (
        echo n_devices                %GPUS% >> output.txt
     ) ELSE (
        echo n_devices                1 >> output.txt
     )
   ) ELSE IF /i "!LMN:~0,10!"=="float_type" (
     echo float_type               %PREC% >> output.txt
   ) ELSE IF /i "!LMN:~0,9!"=="nn_path_e" (
     IF %nn_type_e% GEQ 0 (
        echo nn_path_e                %nnp_e% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,9!"=="nn_type_e" (
     IF %nn_type_e% GEQ 0 (
        echo nn_type_e                %nn_type_e% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,10!"=="wdl_head_e" (
     IF %nn_type_e% GEQ 0 (
        echo wdl_head_e               %wdl_head_e% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,9!"=="nn_path_m" (
     IF %nn_type_m% GEQ 0 (
        echo nn_path_m                %nnp_m% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,9!"=="nn_type_m" (
     IF %nn_type_m% GEQ 0 (
        echo nn_type_m                %nn_type_m% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,10!"=="wdl_head_m" (
     IF %nn_type_m% GEQ 0 (
        echo wdl_head_m               %wdl_head_m% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,7!"=="nn_path" (
     IF %nn_type% GEQ 0 (
        echo nn_path                  %nnp% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,7!"=="nn_type" (
     IF %nn_type% GEQ 0 (
        echo nn_type                  %nn_type% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,8!"=="wdl_head" (
     IF %nn_type% GEQ 0 (
        echo wdl_head                 %wdl_head% >> output.txt
     ) ELSE (
        echo %%A>> output.txt
     )
   ) ELSE (
     echo %%A>> output.txt
   )
)
MOVE output.txt scorpio.ini
cd ../..

REM ----------
echo "Making a test run"
CALL %EXE% go quit
