#!/bin/bash

set -x

wget https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh
chmod +x install.sh
./install.sh --trt 109 --no-egbb --no-lcnets --factor 1

# RAM sizes for TCEC machine
TR=$((128*1024))
NN=$((16*1024))
HT=$((64*1024))

# Edit scorpio ini
sed -i.bak -e "s|^egbb_files_path.*|egbb_files_path /home/scorpio|" \
           -e "s/^nn_cache_size .*/nn_cache_size ${NN}/" \
           -e "s/^treeht.*/treeht ${TR}/" \
           -e "s/^ht.*/ht ${HT}/" \
           -e "s/^egbb_cache_size.*/egbb_cache_size 1024/" \
            Scorpio/bin/Linux/scorpio.ini

# Testing pure mcts
EXE=$PWD/Scorpio/bin/Linux/scorpio.sh
$EXE st 30 delay 0 go quit
$EXE st 30 delay 1 go quit

# Testing hybrid mcts-ab search
EXE=$PWD/Scorpio/bin/Linux/scorpio-mpich.sh
$EXE st 30 delay 0 go quit
$EXE st 30 delay 1 go quit
