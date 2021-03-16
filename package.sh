#!/bin/bash

set -e

VERSION=30

mkdir -p tmp/bin 
cp ScorpioNN.png tmp/bin;
./build.sh COMP=clang profile; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Linux
./build.sh COMP=win; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Windows;
mv tmp/bin/Windows/scorpio tmp/bin/Windows/scorpio.exe
./build.sh COMP=arm; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Android;
cd tmp; zip -r ../scorpio$VERSION-mcts-nn.zip bin/; cd ..
rm -rf tmp
cd ..

