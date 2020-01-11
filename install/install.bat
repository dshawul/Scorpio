@ECHO off
SET VERSION=3.0
SET VR=30
SET OSD=windows

REM --------- Nvidia GPU
WHERE nvcuda.dll >nul 2>nul
IF %ERRORLEVEL% NEQ 0 (
  SET GPU=0
  SET DEV=cpu
  SET /a mt=%NUMBER_OF_PROCESSORS%*4
) ELSE (
  SET GPU=1
  SET DEV=gpu
  SET mt=128
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
IF %GPU% NEQ 0 (
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

IF %GPU% NEQ 0 (
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
  SET nnp=%CWD%nets-scorpio/ens-net-6x64.pb
  SET nnp_e=%CWD%nets-scorpio/ens-net-6x64.pb
  SET nnp_m=
  SET nn_type=0
  SET nn_type_e=-1
  SET nn_type_m=-1
  SET wdl_head=0
  SET wdl_head_e=0
  SET wdl_head_m=0
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
   ) ELSE IF /i "!LMN:~0,11!"=="device_type" (
     IF %GPU% NEQ 0 (
        echo device_type              GPU>> output.txt
     ) ELSE (
        echo device_type              CPU>> output.txt
     )
   ) ELSE IF /i "!LMN:~0,9!"=="n_devices" (
     IF %GPU% NEQ 0 (
        echo n_devices                %GPU% >> output.txt
     ) ELSE (
        echo n_devices                1 >> output.txt
     )
   ) ELSE IF /i "!LMN:~0,10!"=="float_type" (
     echo float_type               INT8 >> output.txt
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
IF %GPU% NEQ 0 (
echo "Generating calibrate.dat"
CALL %EXE% use_nn 0 nn_type %nn_type% runinpnn calibrate.epd calibrate.dat quit
)
echo "Running with opening net"
CALL %EXE% nn_type_m -1 nn_type_e -1 go quit

REM ----------
IF %nn_type_m% GTR 0 (
IF %GPU% NEQ 0 (
echo "Generating calibrate.dat"
CALL %EXE% use_nn 0 nn_type %nn_type_m% runinpnn calibrate.epd calibrate.dat quit
)
echo "Running with midgame net"
CALL %EXE% nn_type -1 nn_type_e -1 setboard 1r1q2k1/5pp1/2p4p/4p3/1PPpP2P/Q1n3P1/1R3PB1/6K1 w - - 5 24 go quit
)

REM ----------
IF %nn_type_e% GTR 0 (
IF %GPU% NEQ 0 (
echo "Generating calibrate.dat"
CALL %EXE% use_nn 0 nn_type %nn_type_e% runinpnn calibrate.epd calibrate.dat quit
)
echo "Running with endgame net"
CALL %EXE% nn_type -1 nn_type_m -1 setboard 6k1/2b2p1p/ppP3p1/4p3/PP1B4/5PP1/7P/7K w - - go quit
)
