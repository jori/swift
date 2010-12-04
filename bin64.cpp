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

const char* bin_t::str (char * buf) const {
    if (is_all())
        return "(ALL)";
    else if (is_none())
        return "(NONE)";
    else
        sprintf(buf,"(%i,%lli)",(int)layer(),layer_offset());
    return buf;
}

std::ostream & operator << (std::ostream & ostream, const bin_t & bin) {
    char bin_name_buf[32];
    return ostream << bin.str(bin_name_buf);
}
