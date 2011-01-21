/*
 *  bin64.cpp
 *  swift
 *
 *  Created by Victor Grishchenko on 10/10/09.
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */
#include "bin64.h"
#include <ostream>

const bin_t bin_t::NONE(0xffffffffffffffffULL);
const bin_t bin_t::ALL(0x7fffffffffffffffULL);

int bin_t::peaks (uint64_t length, bin_t* peaks) {
    int pp=0;
    uint8_t layer = 0;
    while (length) {
        if (length&1) 
            peaks[pp++] = bin_t(layer,length^1);
        length>>=1;
        layer++;
    }
    for(int i=0; i<(pp>>1); i++) {
        const bin_t memo = peaks[pp-1-i];
        peaks[pp-1-i] = peaks[i];
        peaks[i] = memo;
    }
    peaks[pp] = NONE;
    return pp;
}

#include <stdio.h>

const char* bin_t::str () const {
    static char _b64sr[4][32];
    static int _rsc;
    _rsc = (_rsc+1) & 3;
    if (is_all())
        return "(ALL)";
    else if (is_none())
        return "(NONE)";
    else
        sprintf(_b64sr[_rsc],"(%i,%lli)",(int)layer(),layer_offset());
    return _b64sr[_rsc];
}

std::ostream & operator << (std::ostream & ostream, const bin_t & bin) {
    return ostream << bin.str();
}
