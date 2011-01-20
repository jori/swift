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
