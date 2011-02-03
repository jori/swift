#!/bin/bash
# This script runs a leecher at some server;
# env variables are set in env.default.sh

ulimit -c 1024000
cd swift || exit 1
rm -f core
rm -f $HOST-chunk
sleep $(( $RANDOM % 5 ))
sudo bash -c "export LD_LIBRARY_PATH=$HOME/lib;bin/swift-o2 -w -h $HASH \
    -f $HOST-chunk -e $EMIF -p -D 2>$HOST-lerr | gzip > $HOST-lout.gz" \
    || exit 2
