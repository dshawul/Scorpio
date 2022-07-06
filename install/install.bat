@ECHO off

REM -------- Scorpio version number
SET VERSION=3.0
SET VR=30
SET OSD=windows

REM --------- Nvidia GPU
WHERE nvcuda.dll >nul 2>nul
IF %ERRORLEVEL% NEQ 0 (
  SET GPUS=0
  SET DEV=cpu
) ELSE (
  SET GPUS=1
  SET DEV=gpu
)

REM --------- process command line arguments
SET PREC=
SET THREADS=
SET IEGBB=1
SET ILCNET=1
SET ISCNET=1
SET FACTOR=2
IF %GPUS% EQU 0 (
   SET FACTOR=1
)
SET TRT=

:loop
IF NOT "%1"=="" (
    IF "%1"=="-p" (
        SET PREC=%2
        SHIFT
    ) ELSE IF "%1"=="--precision" (
        SET PREC=%2
        SHIFT
    ) ELSE IF "%1"=="-t" (
        SET THREADS=%2
        SHIFT
    ) ELSE IF "%1"=="--threads" (
        SET THREADS=%2
        SHIFT
    ) ELSE IF "%1"=="-f" (
        SET FACTOR=%2
        SHIFT
    ) ELSE IF "%1"=="--factor" (
        SET FACTOR=%2
        SHIFT
    ) ELSE IF "%1"=="--cpu" (
        SET GPUS=0
        SET DEV=cpu
        SET FACTOR=1
    ) ELSE IF "%1"=="--no-egbb" (
        SET IEGBB=0
    ) ELSE IF "%1"=="--no-lcnets" (
        SET ILCNET=0
    ) ELSE IF "%1"=="--no-scnets" (
        SET ISCNET=0
    ) ELSE IF "%1"=="--trt" (
        IF "%2"=="84" (
            SET TRT="-trt-%2"
        )
        SHIFT
    ) ELSE IF "%1"=="--help" (
        :usage
        echo Usage: %0
        echo
        echo   -h,--help          Display this help message.
        echo   -p,--precision     Precision to use FLOAT/HALF/INT8.
        echo   -t,--threads       Total number of threads, i.e minibatch size.
        echo   -f,--factor        Factor for auto minibatch size determination from SMs, default 2.
        echo   --cpu              Force installation on the CPU even if machine has GPU.
        echo   --no-egbb          Do not install 5-men egbb.
        echo   --no-lcnets        Do not install lczero nets.
        echo   --no-scnets        Do not install scorpio nets.
        echo   --trt              84 is for latest GPUs.
        echo                      60 is for older GPUs.
        echo
        echo Example: install.bat -p INT8 - t 80
        exit /b
    )
    SHIFT
    GOTO :loop
)

SET LNK=http://github.com/dshawul/Scorpio/releases/download
SET EGBB=nnprobe-%OSD%-%DEV%%TRT%

REM --------- create directory
SET SCORPIO=Scorpio
mkdir %SCORPIO%
IF EXIST RMDIR /S /Q %SCORPIO%
cd %SCORPIO%
SET CWD=%cd%\

REM --------- download nnprobe
SET FILENAME=%EGBB%.zip
bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
powershell Expand-Archive -Force %CWD%%FILENAME% -DestinationPath %CWD%
DEL %CWD%%FILENAME%
SET FILENAME=egbblib-%OSD%.zip
bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
powershell Expand-Archive -Force %CWD%%FILENAME% -DestinationPath %CWD%%EGBB%
MOVE "%CWD%%EGBB%\egbblib-%OSD%\*.*" "%CWD%%EGBB%"
RMDIR /S /Q %CWD%%EGBB%\egbblib-%OSD%
DEL %CWD%%FILENAME%

REM --------- download egbb
SET FILENAME=egbb.zip
IF %IEGBB% GEQ 1 (
    bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
    powershell Expand-Archive -Force %CWD%%FILENAME% -DestinationPath %CWD%
    DEL %CWD%%FILENAME%
)

REM --------- download scorpio binary
SET FILENAME=scorpio%VR%-mcts-nn.zip
bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%FILENAME%" %CWD%%FILENAME%
powershell Expand-Archive -Force %CWD%%FILENAME% -DestinationPath %CWD%
DEL %CWD%%FILENAME%

REM --------- download networks
SET NETS=nets-nnue.zip
IF %ISCNET% NEQ 0 (
   SET NETS=%NETS% nets-scorpio.zip
)
IF %GPUS% NEQ 0 (
    IF %ILCNET% NEQ 0 (
       SET NETS=%NETS% nets-lczero.zip
    )
)
for %%N in ( %NETS% ) DO (
    bitsadmin /transfer mydownload /dynamic /download /priority FOREGROUND "%LNK%/%VERSION%/%%N" %CWD%%%N
    powershell Expand-Archive -Force %CWD%%%N -DestinationPath %CWD%
    DEL %CWD%%%N
)

cd %EGBB%
icacls "*.*" /grant %USERNAME%:F
cd ..
MOVE "%EGBB%\*.*" "%CWD%bin/Windows"
RMDIR /S /Q %EGBB%

REM ---------- paths
SET egbbp=%CWD%bin/Windows
SET egbbfp=%CWD%egbb
SET EXE="%CWD%bin/Windows/scorpio.bat"

SET nnuep=%CWD%nets-nnue/net-scorpio-k16.bin
IF %GPUS% NEQ 0 (
  SET nnp=%CWD%nets-scorpio/ens-net-20x256.uff
  SET nnp_e=%CWD%nets-ender/ens-net-20x256.uff
) ELSE (
  SET nnp=%CWD%nets-scorpio/ens-net-12x128.pb
  SET nnp_e=%CWD%nets-scorpio/ens-net-12x128.pb
)
SET nnp_m=
SET nn_type=0
SET nn_type_e=-1
SET nn_type_m=-1
SET wdl_head=0
SET wdl_head_e=0
SET wdl_head_m=0

REM --------- determine GPU props
SET HAS=N
SETLOCAL ENABLEDELAYEDEXPANSION
IF %GPUS% NEQ 0 (
    cd %egbbp%
    CALL device.exe
    FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe -n`) DO (
       SET GPUS=%%F
    )
    IF "%THREADS%"=="" (
       FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe --mp`) DO (
          SET THREADS=%%F
       )
       SET /a THREADS*=%FACTOR%
    )

    REM ----- precision
    IF "%PREC%"=="" (
       SET PREC=HALF
       FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe --fp16`) DO (
          SET HAS=%%F
       )
       IF "!HAS!"=="N" (
          SET PREC=FLOAT
          FOR /F "tokens=* USEBACKQ" %%F IN (`device.exe --int8`) DO (
             SET HAS=%%F
          )
          IF "!HAS!"=="Y" (
             SET PREC=INT8
          )
       )
    )
    cd ..
) ELSE (
    IF "%PREC%"=="" ( SET PREC=FLOAT )
    IF "%THREADS%"=="" ( SET /a THREADS=%NUMBER_OF_PROCESSORS%*%FACTOR% )
)

REM ---------- number of threads
SET delay=0
SET rt=0
IF %GPUS% NEQ 0 (
    SET /a rt=%THREADS%/%NUMBER_OF_PROCESSORS%
    IF %rt% GEQ 9 (
        SET delay=1
    )
) ELSE (
    SET delay=1
)

REM ---------- edit scorpio.ini
cd "%CWD%bin/Windows"
IF EXIST output.txt DEL /F output.txt
for /F "delims=" %%A in (scorpio.ini) do (
   SET LMN=%%A
   IF /i "!LMN:~0,9!"=="egbb_path" (
     echo egbb_path                %egbbp%>> output.txt
   ) ELSE IF /i "!LMN:~0,15!"=="egbb_files_path" (
     echo egbb_files_path          %egbbfp%>> output.txt
   ) ELSE IF /i "!LMN:~0,9!"=="nnue_path" (
     echo nnue_path                %nnuep%>> output.txt
   ) ELSE IF /i "!LMN:~0,10!"=="montecarlo" (
     IF %ISCNET% EQU 0 (
        echo montecarlo          0 >> output.txt
     ) ELSE (
        echo montecarlo          1 >> output.txt
     )
   ) ELSE IF /i "!LMN:~0,7!"=="use_nn " (
     IF %ISCNET% EQU 0 (
        echo use_nn                   0 >> output.txt
     ) ELSE (
        echo use_nn                   1 >> output.txt
     )
   ) ELSE IF /i "!LMN:~0,2!"=="mt" (
     echo mt                  %THREADS% >> output.txt
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
ENDLOCAL

REM ----------
echo "Making a test run"
CALL %EXE% go quit
cd ..
