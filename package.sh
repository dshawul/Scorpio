#!/bin/bash

set -e

VERSION=30

mkdir -p tmp/bin 
./build.sh gcc; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Linux
./build.sh win; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Windows;
mv tmp/bin/Windows/scorpio tmp/bin/Windows/scorpio.exe
cp uci/*.* tmp/bin/Windows
./build.sh arm; cp -r bin/ tmp/bin; mv tmp/bin/bin tmp/bin/Android;
cd tmp; zip -r ../scorpio$VERSION-mcts-nn.zip bin/; cd ..
rm -rf tmp
cd ..

