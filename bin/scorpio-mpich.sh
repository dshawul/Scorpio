#!/bin/bash

WDIR="$( dirname "${BASH_SOURCE[0]}" )"
cd $WDIR >/dev/null 2>&1

NDIR=$( grep ^egbb_path scorpio.ini | awk '{ print $2 }' )
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$NDIR

cd $WDIR >/dev/null 2>&1
exec mpirun \
    -np 1 ./scorpio-mpich vote_weight 200 frac_abprior 0 "$@" : \
    -np 1 ./scorpio-mpich use_nn 0 montecarlo 0 mt auto/2 "$@"
