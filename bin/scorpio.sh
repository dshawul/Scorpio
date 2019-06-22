#!/bin/bash

WDIR="$( dirname "${BASH_SOURCE[0]}" )"
cd $WDIR >/dev/null 2>&1

NDIR=$( grep ^egbb_path scorpio.ini | awk '{ print $2 }' )
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$NDIR

cd $WDIR >/dev/null 2>&1
exec ./scorpio "$@"
