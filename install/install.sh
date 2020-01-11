#!/bin/bash

# display help
display_help() {
    echo "Usage: $0 [OS] [MACHINE] "
    echo
    echo "  -h,--help     Display this help message."
    echo
    echo "Example: ./install.sh"
    echo
}

if [ "$1" == "-h" ] || [ "$1" == "--help" ]; then
  display_help
  exit 0
fi

set -eux

# Autodetect operating system
OSD=windows
if [[ "$OSTYPE" == "linux-gnu" ]]; then
  OSD=ubuntu
elif [[ "$OSTYPE" == "darwin"* ]]; then
  OSD=macosx
fi

# number of cores and gpus
CPUS=`grep -c ^processor /proc/cpuinfo`
if [ ! -z `which nvidia-smi` ]; then
    GPUS=`nvidia-smi --query-gpu=name --format=csv,noheader | wc -l`
    DEV=gpu
else
    GPUS=1
    DEV=cpu
fi

# Select
OS=${1:-$OSD}      # OS is either ubuntu/centos/windows/android
DEV=${2:-$DEV}     # Device is either gpu/cpu
VERSION=3.0        # Version of scorpio

# paths
VR=`echo $VERSION | tr -d '.'`
EGBB=nnprobe-${OS}-${DEV}
if [ $DEV = "gpu" ]; then
    NET="nets-scorpio nets-lczero nets-maddex"
else
    NET="nets-scorpio"
fi

# download
SCORPIO=Scorpio-$(date '+%d-%b-%Y')
mkdir -p $SCORPIO
[ -L Scorpio ] && rm -rf Scorpio
ln -sf $SCORPIO Scorpio
cd $SCORPIO

# egbbdll & linnnprobe
LNK=https://github.com/dshawul/Scorpio/releases/download
wget --no-check-certificate ${LNK}/${VERSION}/${EGBB}.zip
unzip -o ${EGBB}.zip
# networks
for N in $NET; do
    wget --no-check-certificate ${LNK}/${VERSION}/$N.zip
    unzip -o $N.zip
done
# egbbs
wget --no-check-certificate ${LNK}/${VERSION}/egbb.zip
unzip -o egbb.zip
# scorpio binary
wget --no-check-certificate ${LNK}/${VERSION}/scorpio${VR}-mcts-nn.zip
unzip -o scorpio${VR}-mcts-nn.zip

rm -rf *.zip
chmod 755 ${EGBB}
cd ${EGBB}
chmod 755 *
cd ../..

# number of threads
delay=0
if [ $DEV = "gpu" ]; then
    if [ $CPUS -le 4 ] || [ $GPUS -ge 2 ]; then
       delay=1
    fi
    if [ $CPUS -eq 1 ]; then
        mt=$((GPUS*64))
    else
        mt=$((GPUS*128))
    fi
else
    mt=$((CPUS*4))
    delay=1
fi

#paths
cd $SCORPIO
PD=`pwd`
PD=`echo $PD | sed 's/\/cygdrive//g'`
PD=`echo $PD | sed 's/\/c\//c:\//g'`
egbbp=${PD}/${EGBB}
egbbfp=${PD}/egbb
if [ $DEV = "gpu" ]; then
    nnp=${PD}/nets-scorpio/ens-net-20x256.uff
    nnp_e=${PD}/nets-maddex/ME.uff
    nnp_m=
    nn_type=0
    nn_type_e=1
    nn_type_m=-1
    wdl_head=0
    wdl_head_e=1
    wdl_head_m=0
else
    nnp=${PD}/nets-scorpio/ens-net-6x64.pb
    nnp_e=${PD}/nets-scorpio/ens-net-6x64.pb
    nnp_m=
    nn_type=0
    nn_type_e=-1
    nn_type_m=-1
    wdl_head=0
    wdl_head_e=0
    wdl_head_m=0
fi
if [ $OS = "windows" ]; then
    exep=${PD}/bin/Windows
elif [ $OS = "android" ]; then
    exep=${PD}/bin/Android
else
    exep=${PD}/bin/Linux
fi
cd $exep

# Edit scorpio.ini
egbbp_=$(echo $egbbp | sed 's_/_\\/_g')
egbbfp_=$(echo $egbbfp | sed 's_/_\\/_g')
nnp_=$(echo $nnp | sed 's_/_\\/_g')
nnp_e_=$(echo $nnp_e | sed 's_/_\\/_g')
nnp_m_=$(echo $nnp_m | sed 's_/_\\/_g')

sed -i "s/^egbb_path.*/egbb_path                ${egbbp_}/g" scorpio.ini
sed -i "s/^egbb_files_path.*/egbb_files_path          ${egbbfp_}/g" scorpio.ini
sed -i "s/^delay.*/delay                    ${delay}/g" scorpio.ini
sed -i "s/^float_type.*/float_type               INT8/g" scorpio.ini

if [ $DEV = "gpu" ]; then
    sed -i "s/^device_type.*/device_type              GPU/g" scorpio.ini
    sed -i "s/^n_devices.*/n_devices                ${GPUS}/g" scorpio.ini
    sed -i "s/^mt.*/mt                  ${mt}/g" scorpio.ini
else
    sed -i "s/^device_type.*/device_type              CPU/g" scorpio.ini
    sed -i "s/^n_devices.*/n_devices                1/g" scorpio.ini
    sed -i "s/^mt.*/mt                  ${mt}/g" scorpio.ini
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

# Prepare script for scorpio and set PATH env variable
if [ $OS = "windows" ]; then
    EXE=scorpio.bat
else
    EXE=scorpio.sh
fi

cd ../..

# Test
if [ $DEV = "gpu" ]; then
echo "Generating calibrate.dat"
$exep/$EXE use_nn 0 nn_type ${nn_type} runinpnn calibrate.epd calibrate.dat quit
fi
echo "Running with opening net"
$exep/$EXE nn_type_m -1 nn_type_e -1 go quit

if [ $nn_type_m -ge 0 ]; then
if [ $DEV = "gpu" ]; then
echo "Generating calibrate.dat"
$exep/$EXE use_nn 0 nn_type ${nn_type_m} runinpnn calibrate.epd calibrate.dat quit
fi
echo "Running with midgame net"
$exep/$EXE nn_type -1 nn_type_e -1 setboard 1r1q2k1/5pp1/2p4p/4p3/1PPpP2P/Q1n3P1/1R3PB1/6K1 w - - 5 24 go quit
fi

if [ $nn_type_e -ge 0 ]; then
if [ $DEV = "gpu" ]; then
echo "Generating calibrate.dat"
$exep/$EXE use_nn 0 nn_type ${nn_type_e} runinpnn calibrate.epd calibrate.dat quit
fi
echo "Running with endgame net"
$exep/$EXE nn_type -1 nn_type_m -1 setboard 6k1/2b2p1p/ppP3p1/4p3/PP1B4/5PP1/7P/7K w - - go quit
fi

