#include "wrapping_integers.hh"

// Dummy implementation of a 32-bit wrapping integer

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! Transform an "absolute" 64-bit sequence number (zero-indexed) into a WrappingInt32
//! \param n The input absolute 64-bit sequence number
//! \param isn The initial sequence number
WrappingInt32 wrap(uint64_t n, WrappingInt32 isn) {
    uint32_t n_32bit = static_cast<uint32_t>(n);
    return WrappingInt32{n_32bit + isn.raw_value()};
    
}

//! Transform a WrappingInt32 into an "absolute" 64-bit sequence number (zero-indexed)
//! \param n The relative sequence number
//! \param isn The initial sequence number
//! \param checkpoint A recent absolute 64-bit sequence number
//! \returns the 64-bit sequence number that wraps to `n` and is closest to `checkpoint`
//!
//! \note Each of the two streams of the TCP connection has its own ISN. One stream
//! runs from the local TCPSender to the remote TCPReceiver and has one ISN,
//! and the other stream runs from the remote TCPSender to the local TCPReceiver and
//! has a different ISN.
uint64_t unwrap(WrappingInt32 n, WrappingInt32 isn, uint64_t checkpoint) {
    #define Convert32To64(x) static_cast<uint64_t>(x)
    uint64_t initial_absolute = n.raw_value() >= isn.raw_value()? Convert32To64(n.raw_value()-isn.raw_value()): Convert32To64(n.raw_value())+Convert32To64(1UL<<32)-Convert32To64(isn.raw_value());
    bool whether_mid = (initial_absolute & 0x80000000) > 0;
    if(whether_mid) {
        auto boundary = initial_absolute - 0x80000000UL;
        auto checkpoint_left = checkpoint & 0xffffffff00000000UL;
        auto checkpoint_right = checkpoint & 0xffffffffUL;
        if(checkpoint_right <= boundary && checkpoint_left!=0UL) return initial_absolute | ((((checkpoint_left) >> 32) - 1) << 32);
        else return initial_absolute | checkpoint_left;
    } else {
        auto boundary = initial_absolute + 0x80000000UL;
        auto checkpoint_left = checkpoint & 0xffffffff00000000UL;
        auto checkpoint_right = checkpoint & 0xffffffffUL;
        if(checkpoint_right > boundary && (~static_cast<uint32_t>(checkpoint_left>>32))!=0UL) return initial_absolute | ((((checkpoint_left) >> 32) + 1) << 32); 
        else return initial_absolute | checkpoint_left;
    }
    // if(initial_absolute >= checkpoint) return initial_absolute;
    // uint64_t upper_seqno = initial_absolute + (1UL<<32);
    // uint64_t & lower_seqno = initial_absolute;
    // while(upper_seqno < checkpoint) {
    //     lower_seqno = upper_seqno;
    //     upper_seqno += (1UL<<32);
    //     if(upper_seqno < lower_seqno) break;
    // }
    // return (upper_seqno - checkpoint) > (checkpoint - lower_seqno)? lower_seqno: upper_seqno;
}
