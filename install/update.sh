#!/bin/bash
wget https://github.com/dshawul/Scorpio/releases/download/3.0/install.sh
chmod +x install.sh
./install.sh --trt 84 --no-egbb --no-lcnets

#RAM sizes
TR=$((45*1024))
NN=$((16*1024))
HT=$((32*1024))

#edit scorpio ini
sed -i.bak -e "s|^egbb_files_path.*|egbb_files_path /home/scorpio|" \
           -e "s/^delay.*/delay 0/" \
           -e "s/^nn_cache_size .*/nn_cache_size ${NN}/" \
           -e "s/^treeht.*/treeht ${TR}/" \
           -e "s/^ht.*/ht ${HT}/" \
           -e "s/^egbb_cache_size.*/egbb_cache_size 1024/" \
            Scorpio/bin/Linux/scorpio.ini

EXE=$PWD/Scorpio/bin/Linux/scorpio-mpich.sh

$EXE uci go quit
