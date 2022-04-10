#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

inline bool TCPReceiver::SYN_check(const TCPSegment &seg) {
    auto & header = seg.header();
    return header.syn;
}

inline bool TCPReceiver::FIN_check(const TCPSegment &seg) {
    auto & header = seg.header();
    return header.fin;
}

void TCPReceiver::segment_received(const TCPSegment &seg) {
    auto header_seqno = seg.header().seqno;
    
    bool beginning = SYN_check(seg);
    bool ending = FIN_check(seg);
    
    // handle SYN
    if(beginning) {
        isn = make_shared<WrappingInt32>(header_seqno);
        ack = make_shared<WrappingInt32>(isn->raw_value() + 1);
    }

    // if(ack) printf("now ackno = %u\n", ack->raw_value());

    if(!isn) return;

    // process payload
    auto & data = seg.payload();
    auto absolute_seqno = unwrap(header_seqno, *isn, checkpoint);
    if(!beginning && absolute_seqno==0) return;
    auto stream_index = beginning?0:absolute_seqno-1;
    
    if(ack && ((beginning && header_seqno+1 == *ack) || (!beginning && header_seqno == *ack))) {
        auto stream_capacity_before = _reassembler.stream_out().remaining_capacity();
        _reassembler.push_substring(std::string(data.str()), stream_index, ending);
        auto stream_capacity_after = _reassembler.stream_out().remaining_capacity();
        auto bytes_into_stream = stream_capacity_before - stream_capacity_after;
        *ack = *ack + static_cast<uint32_t>(bytes_into_stream);
        checkpoint = absolute_seqno + bytes_into_stream;
        if(ending || _reassembler.stream_out().input_ended()) *ack = *ack + 1;
    } else {
        _reassembler.push_substring(std::string(data.str()), stream_index, ending);
    }

    // if(ack) printf("now ackno = %u\n", ack->raw_value());

}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if(!isn) return nullopt;
    else return *ack;
}

size_t TCPReceiver::window_size() const { 
    return _reassembler.stream_out().remaining_capacity();
}
