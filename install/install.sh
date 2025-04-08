#!/bin/bash

set -e

# display help
display_help() {
    echo "Usage: $0  "
    echo
    echo "  -h,--help          Display this help message."
    echo "  -p,--precision     Precision to use FLOAT/HALF/INT8."
    echo "  -t,--threads       Total number of threads, i.e minibatch size."
    echo "  -f,--factor        Factor for auto minibatch size determination from SMs, default 2."
    echo "  --cpu              Force installation on the CPU even if machine has GPU."
    echo "  --no-egbb          Do not install 5-men egbb."
    echo "  --no-lcnets        Do not install lczero nets."
    echo "  --no-scnets        Do not download scorpio nets."
    echo "  --trt              84 is for latest GPUs."
    echo "                     60 is for older GPUs (default)."
    echo
    echo "Example: ./install.sh -p INT8 -t 80"
    echo
}

# Scorpio version number
VERSION=3.0
VR=`echo $VERSION | tr -d '.'`

# Autodetect operating system
OS=windows
exep=bin/Windows
if [[ "$OSTYPE" == "linux"* ]]; then
  OS=ubuntu
  exep=bin/Linux
elif [[ "$OSTYPE" == "darwin"* ]]; then
  OS=macosx
  exep=bin/Macosx
else
  OS=android
  exep=bin/Android
fi

# number of cores and gpus
CPUS=`grep -c ^processor /proc/cpuinfo`
if [ ! -z `which nvidia-smi` ]; then
    GPUS=1
    DEV=gpu
else
    GPUS=0
    DEV=cpu
fi

# process cmd line arguments
PREC=
THREADS=
IEGBB=1
ILCNET=1
ISCNET=1
if [ $DEV = "gpu" ]; then
  FACTOR=2
else
  FACTOR=1
fi
TRT=

while ! [ -z "$1" ]; do
    case $1 in
        -p | --precision )
            shift
            PREC=$1
            ;;
        -t | --threads )
            shift
            THREADS=$1
            ;;
        -f | --factor )
            shift
            FACTOR=$1
            ;;
        --cpu )
            GPUS=0
            DEV=cpu
            FACTOR=1
            ;;
        --no-egbb )
            IEGBB=0
            ;;
        --no-lcnets )
            ILCNET=0
            ;;
        --no-scnets )
            ISCNET=0
            ;;
        --trt )
            shift
            [ "$1" != "60" ] && TRT="-trt-$1"
            ;;
        -h | --help)
            display_help
            exit 0
    esac
    shift
done

# download
SCORPIO=Scorpio-$(date '+%d-%b-%Y')
mkdir -p $SCORPIO
[ -L Scorpio ] && rm -rf Scorpio
ln -sf $SCORPIO Scorpio
cd $SCORPIO

# egbbdll & linnnprobe
LNK=https://github.com/dshawul/Scorpio/releases/download
EGBB=nnprobe-${OS}-${DEV}${TRT}
wget --no-check-certificate ${LNK}/${VERSION}/${EGBB}.zip
unzip -o ${EGBB}.zip
wget --no-check-certificate ${LNK}/${VERSION}/egbblib-${OS}.zip
unzip -j -o egbblib-${OS}.zip -d ${EGBB}

# networks
NET="nets-nnue"
if [ $ISCNET -eq 1 ]; then
    NET="${NET} nets-scorpio"
fi
if [ $DEV = "gpu" ] && [ $ILCNET -eq 1 ]; then
    NET="${NET} nets-lczero"
fi
for N in $NET; do
    wget --no-check-certificate ${LNK}/${VERSION}/$N.zip
    unzip -o $N.zip
done

# egbbs
if [ $IEGBB -ge 1 ]; then
wget --no-check-certificate ${LNK}/${VERSION}/egbb.zip
unzip -o egbb.zip
fi

# scorpio binary
wget --no-check-certificate ${LNK}/${VERSION}/scorpio${VR}-mcts-nn.zip
unzip -o scorpio${VR}-mcts-nn.zip

rm -rf *.zip
cd ${EGBB}
chmod 755 *
cd ..
mv ${EGBB}/* ${exep}
rm -rf ${EGBB}
cd ..

#paths
cd $SCORPIO
PD=`pwd`
PD=`echo $PD | sed 's/\/cygdrive//g'`
PD=`echo $PD | sed 's/\/c\//c:\//g'`
egbbp=${PD}/${exep}
egbbfp=${PD}/egbb
exep=${PD}/${exep}

nnuep=${PD}/nets-nnue/net-scorpio-k16.bin
if [ $DEV = "gpu" ]; then
    nnp=${PD}/nets-scorpio/ens-net-20x256.onnx
    nnp_e=${PD}/nets-scorpio/ens-net-20x256.onnx
else
    nnp=${PD}/nets-scorpio/ens-net-12x128.pb
    nnp_e=${PD}/nets-scorpio/ens-net-12x128.pb
fi
nnp_m=
nn_type=0
nn_type_e=-1
nn_type_m=-1
wdl_head=0
wdl_head_e=0
wdl_head_m=0
cd $exep

#determin GPU props
if [ $DEV = "gpu" ]; then
    export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${egbbp}
    cd ${egbbp}
    ./device
    GPUS=`./device -n`
    if [ -z $THREADS ]; then 
       THREADS=`./device --mp`
       THREADS=$((THREADS*FACTOR))
    fi
    if [ -z $PREC ]; then 
       PREC=HALF
       HAS=`./device --fp16`
       if [ "$HAS" = "N" ]; then
          HAS=`./device --int8`
          PREC=FLOAT
          if [ "$HAS" = "Y" ]; then
             PREC=INT8
          fi
       fi
    fi
    cd $exep
else
    [ -z $PREC ] && PREC=FLOAT
    [ -z $THREADS ] && THREADS=$((CPUS*FACTOR))
fi

# number of threads
delay=0
if [ $DEV = "gpu" ]; then
    rt=$((THREADS/CPUS))
    if [ $rt -ge 9 ]; then
       delay=1
    fi
else
    delay=1
fi

# Edit scorpio.ini
egbbp_=$(echo $egbbp | sed 's_/_\\/_g')
egbbfp_=$(echo $egbbfp | sed 's_/_\\/_g')
nnp_=$(echo $nnp | sed 's_/_\\/_g')
nnp_e_=$(echo $nnp_e | sed 's_/_\\/_g')
nnp_m_=$(echo $nnp_m | sed 's_/_\\/_g')
nnuep_=$(echo $nnuep | sed 's_/_\\/_g')

sed -i "s/^egbb_path.*/egbb_path                ${egbbp_}/g" scorpio.ini
sed -i "s/^egbb_files_path.*/egbb_files_path          ${egbbfp_}/g" scorpio.ini
sed -i "s/^delay.*/delay                    ${delay}/g" scorpio.ini
sed -i "s/^float_type.*/float_type               ${PREC}/g" scorpio.ini
sed -i "s/^mt.*/mt                  ${THREADS}/g" scorpio.ini
sed -i "s/^nnue_path.*/nnue_path                ${nnuep_}/g" scorpio.ini

if [ $DEV = "gpu" ]; then
    sed -i "s/^device_type.*/device_type              GPU/g" scorpio.ini
    sed -i "s/^n_devices.*/n_devices                ${GPUS}/g" scorpio.ini
else
    sed -i "s/^device_type.*/device_type              CPU/g" scorpio.ini
    sed -i "s/^n_devices.*/n_devices                1/g" scorpio.ini
fi
if [ $nn_type -ge 0 ]; then
    sed -i "s/^nn_path .*/nn_path                  ${nnp_}/g" scorpio.ini
    sed -i "s/^nn_type .*/nn_type                  ${nn_type}/g" scorpio.ini
    if [ $wdl_head -gt 0 ]; then
      sed -i "s/^wdl_head.*/wdl_head               1/g" scorpio.ini
    fi
fi
if [ $nn_type_m -ge 0 ]; then
    sed -i "s/^nn_path_m.*/nn_path_m                ${nnp_m_}/g" scorpio.ini
    sed -i "s/^nn_type_m.*/nn_type_m                ${nn_type_m}/g" scorpio.ini
    if [ $wdl_head_m -gt 0 ]; then
      sed -i "s/^wdl_head_m.*/wdl_head_m               1/g" scorpio.ini
    fi
fi
if [ $nn_type_e -ge 0 ]; then
    sed -i "s/^nn_path_e.*/nn_path_e                ${nnp_e_}/g" scorpio.ini
    sed -i "s/^nn_type_e.*/nn_type_e                ${nn_type_e}/g" scorpio.ini
    if [ $wdl_head_e -gt 0 ]; then
      sed -i "s/^wdl_head_e.*/wdl_head_e               1/g" scorpio.ini
    fi
fi
if [ $ISCNET -eq 0 ]; then
    sed -i "s/^montecarlo.*/montecarlo          0/g" scorpio.ini
    sed -i "s/^use_nn .*/use_nn                   0/g" scorpio.ini
fi

# Prepare script for scorpio and set PATH env variable
if [ $OS = "windows" ]; then
    EXE=scorpio.bat
else
    EXE=scorpio.sh
fi

cd ../..

# Test
echo "Making a test run"
$exep/$EXE go quit
