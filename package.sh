#!/bin/bash

set -e

VERSION=30

mkdir -p tmp/bin 
cp ScorpioNN.png tmp/bin;
#linux
./build.sh COMP=clang profile; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Linux
./build.sh COMP=clang-cluster profile; cp bin/scorpio tmp/bin/Linux/scorpio-mpich
#copy mpi libs
cp /usr/lib/mpich/lib/libmpich.so.0 tmp/bin/Linux/libmpich.so.0
cp /usr/lib/mpich/lib/libmpichcxx.so.0 tmp/bin/Linux/libmpichcxx.so.0
cp /usr/lib/libcr.so.0 tmp/bin/Linux/libcr.so.0
#windows
./build.sh COMP=win; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Windows;
mv tmp/bin/Windows/scorpio tmp/bin/Windows/scorpio.exe
#android
./build.sh COMP=arm; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Android;
#zip
cd tmp; zip -r ../scorpio$VERSION-mcts-nn.zip bin/; cd ..
rm -rf tmp
cd ..

