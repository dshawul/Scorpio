#!/bin/bash

set -e

VERSION=30

rm -rf tmp
mkdir -p tmp/bin tmp/bin-2 tmp/bin-2/log
cp ScorpioNN.png tmp/bin;
cp bin/calibrate.epd bin/scorpio.ini bin/*.sh bin/*.bat tmp/bin-2/

#-----linux------
cp -r tmp/bin-2/ tmp/bin
mv tmp/bin/bin-2 tmp/bin/Linux
./build.sh COMP=clang profile; cp bin/scorpio tmp/bin/Linux/scorpio
./build.sh COMP=clang-cluster profile; cp bin/scorpio tmp/bin/Linux/scorpio-mpich

#-----windows------
cp -r tmp/bin-2/ tmp/bin
mv tmp/bin/bin-2 tmp/bin/Windows
./build.sh COMP=win; cp bin/scorpio.exe tmp/bin/Windows/scorpio.exe
./build.sh COMP=win-cluster; cp bin/scorpio.exe tmp/bin/Windows/scorpio-mpich.exe
#copy mpi libs
cp /usr/local/msmpi/Lib/x64/msmpi.dll tmp/bin/Windows/msmpi.dll

#-----android------
cp -r tmp/bin-2/ tmp/bin
mv tmp/bin/bin-2 tmp/bin/Android
./build.sh COMP=arm; cp bin/scorpio tmp/bin/Android/scorpio
#------------------

rm -rf tmp/bin-2
cd tmp; zip -r ../scorpio$VERSION-mcts-nn.zip bin/; cd ..
rm -rf tmp
cd ..

