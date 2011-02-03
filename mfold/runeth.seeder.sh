#!/bin/bash

ulimit -c 1024000
cd swift || exit 2
if [ ! -e ScottKim_2008P.mp4 ]; then
    wget -c http://video.ted.com/talks/podcast/ScottKim_2008P.mp4 || exit 1
fi
sudo bash -c "export LD_LIBRARY_PATH=$HOME/lib;bin/swift-o2 -w \
    -f ScottKim_2008P.mp4 -e $EMIF -p -D 2>$HOST-lerr | gzip > $HOST-lout.gz" \
    || exit 2
