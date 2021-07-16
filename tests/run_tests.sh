#!/bin/bash
set -ax          

cd ../bin

#search
./scorpio.sh use_nn 0 use_nnue 0 mt 4 montecarlo 0 go quit
./scorpio.sh use_nn 0 use_nnue 0 mt 4 montecarlo 1 go quit
./scorpio.sh use_nn 0 use_nnue 0 mt 4 montecarlo 1 frac_alphabeta 50 frac_abrollouts 100 frac_freeze_tree 30 alphabeta_depth 0 multipv 1 go quit
./scorpio.sh use_nn 0 use_nnue 0 mt 4 montecarlo 1 setboard 8/5k2/8/2pn1BB1/6Kp/5P1P/6b1/8 w - - 2 66 multipv 1 go quit

#perft
./scorpio.sh use_nn 0 use_nnue 0 epd_run_perft ../tests/standard.epd quit | tee output
[ ! -z `grep Error output` ] && exit 1
./scorpio.sh variant fischerandom use_nn 0 use_nnue 0 epd_run_perft ../tests/fischer.epd quit | tee log
[ ! -z `grep Error output` ] && exit 1
rm -rf output

#selfplay
./scorpio.sh use_nn 0 use_nnue 0 mt 4 montecarlo 1 pvstyle 1 sv 20 selfplayp 8 games.pgn train.epd quit
./scorpio.sh use_nn 0 use_nnue 0 mt 4 montecarlo 0 pvstyle 1 sd 4 selfplayp 8 games.pgn train.epd quit
rm -rf games.pgn.* train.epd.*

cd -
