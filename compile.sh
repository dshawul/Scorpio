#!/bin/bash

set -e

VERSION=290

make clean; make; cp scorpio bin/Linux/scorpio;
make clean; make COMP=win; cp scorpio bin/Windows/scorpio.exe;
sed -i 's/DEFINES += -DHAS_POPCNT/#DEFINES += -DHAS_POPCNT/g' Makefile
sed -i 's/DEFINES += -DHAS_PREFETCH/#DEFINES += -DHAS_PREFETCH/g' Makefile
make clean; make; cp scorpio bin/Linux/scorpio-nopop;
make clean; make COMP=win; cp scorpio bin/Windows/scorpio-nopop.exe;
sed -i 's/#DEFINES += -DHAS_POPCNT/DEFINES += -DHAS_POPCNT/g' Makefile
sed -i 's/#DEFINES += -DHAS_PREFETCH/DEFINES += -DHAS_PREFETCH/g' Makefile
cp scorpio.ini calibrate.epd bin/Linux
cp scorpio.ini calibrate.epd bin/Windows
cp scorpio.ini calibrate.epd bin/Mac
zip -r scorpio$VERSION-mcts-nn.zip bin/

