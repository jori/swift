/*
 *  bin64.h
 *  bin numbers (binaty tree enumeration/navigation)
 *
 *  Created by Victor Grishchenko on ??/09/09 in Karlovy Vary
 *  Copyright 2009 Delft University of Technology. All rights reserved.
 *
 */
#ifndef BIN64_H
#define BIN64_H
#include <assert.h>
#include <iosfwd>
#include "compat.h"


/** Numbering for (aligned) logarithmical bins.
    Each number stands for an interval
    [o*2^l,(o+1)*2^l), where l is the layer and o
    is the offset.
    Bin numbers in the tail111 encoding: meaningless
    bits in the tail are set to 0111...11, while the
    head denotes the offset. Thus, 1101 is the bin
    at layer 1, offset 3 (i.e. fourth). 
    Obviously, bins form a binary tree. All navigation
    is made in terms of binary trees: left, right,
    sibling, parent, etc.
 */
struct bin_t {
    uint64_t v;
    static const bin_t NONE;
    static const bin_t ALL;

    bin_t() : v(NONE.v) {}
    explicit bin_t(const uint64_t val) : v(val) {}
    bin_t(uint8_t layer, uint64_t offset) :
        v( (offset<<(layer+1)) | ((1ULL<<layer)-1) ) {}

    bool operator == (const bin_t& b) const { return v==b.v; }
    bool operator != (const bin_t& b) const { return v!=b.v; }

    uint64_t toUInt() const { return v; }

    bool is_all() const { return (*this) == ALL; }
    bool is_none() const { return (*this) == NONE; }

    uint64_t layer_bits () const {
        return v ^ (v+1);
    }

    /** Get the sibling interval in the binary tree. */
    bin_t sibling () const {
        // if (is_all()) return NONE;
        return bin_t(v^(tail_bit()<<1));
    }

    int layer () const {
        int r = 0;
        uint64_t tail = ((v^(v+1))+1)>>1;
        if (tail>0xffffffffULL) {
            r = 32;
            tail>>=32;
        }
        // courtesy of Sean Eron Anderson
        // http://graphics.stanford.edu/~seander/bithacks.html
        static const int DeBRUIJN[32] = {
          0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8,
          31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
        };
        r += DeBRUIJN[((uint32_t)(tail*0x077CB531U))>>27];
        return r;
    }

    /** Get the bin's offset in base units, i.e. 4 for (1,2). */
    uint64_t base_offset () const {
        return (v&~(layer_bits()))>>1;
    }

    /** Get the bin's offset at its own layer, e.g. 2 for (1,2). */
    uint64_t layer_offset () const {
        return v >> (layer()+1);
    }

    /** Get a child bin; either right(true) or left(false). */
    bin_t   to (bool right) const {
        if (!(v&1))
            return NONE;
        uint64_t tb = ((layer_bits() >> 1) + 1) >> 1;
        if (right)
            return bin_t(v + tb);
        return bin_t(v ^ tb);
    }

    /** Get the left child bin. */
    bin_t   left () const {
        return to(false);
    }

    /** Get the right child bin. */
    bin_t   right () const {
        return to(true);
    }

    /** Check whether this bin is within the specified bin. */
    bool    contains (bin_t bin) const {
        if (is_none()) {
            return false;
        }

        return (v & (v + 1)) <= bin.v && bin.v < (v | (v + 1));
    }

    /** Left or right, depending whether the destination is. */
    bin_t   towards (bin_t dest) const {
        if (!contains(dest))
            return NONE;
        if (left().contains(dest))
            return left();
        else
            return right();
    }

    /** Twist/untwist a bin number according to the mask. */
    bin_t   twisted (uint64_t mask) const {
        return bin_t( v ^ ((mask<<1)&~layer_bits()) );
    }

    /** Get the paretn bin. */
    bin_t   parent () const {
        uint64_t tbs = layer_bits(), ntbs = (tbs+1)|tbs;
        return bin_t( (v&~ntbs) | tbs );
    }

    /** Check whether this bin is the left sibling. */
    inline bool is_left () const {
        uint64_t tb = tail_bit();
        return !(v&(tb<<1));
    }
    
    /** Check whether this bin is the right sibling. */
    inline bool is_right() const { return !is_left(); }

    /** Get the leftmost basic bin within this bin. */
    bin_t   base_left () const {
        if (is_none())
            return NONE;
        return bin_t(0,base_offset());
    }

    /** Whether layer is 0. */
    bool    is_base () const {
        return !(v & 1);
    }

    /** Return the number of basic bins within this bin. */
    uint64_t base_length () const {
        return (layer_bits()+1)>>1;
    }
    
    /** Get the standard-form null-terminated string
        representation of this bin, e.g. "(2,1)".
        The string is statically allocated, must
        not be reused or released. */
    const char* str () const;

    /** The array must have 64 cells, as it is the max
     number of peaks possible +1 (and there are no reason
     to assume there will be less in any given case. */
    static int peaks (uint64_t length, bin_t* peaks) ;

private:
    uint64_t tail_bit () const {
        return (layer_bits()+1)>>1;
    }
};


std::ostream & operator << (std::ostream & ostream, const bin_t & bin);


#endif

/**
                 00111
       0011                    1011
  001      101         1001          1101
0   10  100  110    1000  1010   1100   1110

                  7
      3                         11
  1        5             9             13
0   2    4    6       8    10      12     14

once we have peak hashes, this struture is more natural than bin-v1

*/
