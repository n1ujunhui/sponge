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
    auto & data = seg.payload();
    
    if(!isn && SYN_check(seg)) {
        isn = make_shared<WrappingInt32>(header_seqno);
        ack = make_shared<WrappingInt32>(isn->raw_value() + 1);
        printf("set isn to %d, ack to %d\n", isn->raw_value(), ack->raw_value());
    }
    if(!isn) return;
    auto absolute_seqno = unwrap(header_seqno, *isn, checkpoint);
    auto stream_capacity_before = _reassembler.stream_out().remaining_capacity();

    printf("absolute_seqno = %ld\n", absolute_seqno);
    if(FIN_check(seg)) {
        printf("get final seqno\n");
        _reassembler.push_substring(std::string(data.str()), absolute_seqno == 0? 0: absolute_seqno-1, true);
        auto bytes_into_stream = stream_capacity_before - _reassembler.stream_out().remaining_capacity();
        *ack = *ack + static_cast<uint32_t>(bytes_into_stream+1);
        checkpoint = absolute_seqno + bytes_into_stream+1;
        return;
    }
    printf("flag\n");
    
    if(ack) printf("ackno = %d\n", ack->raw_value());

    // auto bytes_unassembled_before = _reassembler.unassembled_bytes();
    
    // seqno 正是我所需要的
    if(ack && header_seqno == *ack) {
        _reassembler.push_substring(std::string(data.str()), absolute_seqno-1, false);
        printf("ackno = %d\n", header_seqno.raw_value());
        auto bytes_into_stream = stream_capacity_before - _reassembler.stream_out().remaining_capacity();
        *ack = *ack + static_cast<uint32_t>(bytes_into_stream);
        checkpoint = absolute_seqno + bytes_into_stream;
        // while(true) {
        //     if(ack_record.size() == 0) return;
        //     auto first_record = ack_record.front().first;
        //     printf("ackno = %d, first_record = %d\n", ack->raw_value(), first_record.raw_value());
        //     if(ack->raw_value() >= first_record.raw_value()) {
        //         auto _size = ack_record.front().second;
        //         printf("seqno = %d, size = %ld\n", first_record.raw_value(), _size);
        //         *ack = WrappingInt32(first_record + static_cast<uint32_t>(_size));
        //         ack_record.pop_front();
        //         auto _absolute_seqno = unwrap(first_record, *isn, checkpoint);
        //         checkpoint = _absolute_seqno + _size;
        //     } else {
        //         break;
        //     }
        // }
    } else {
        // 这不是我需要的最小的 seqno，因此先放进去
        printf("record to private list\n");
        _reassembler.push_substring(std::string(data.str()), absolute_seqno-1, false);
        // ack_record.push_back({header_seqno, _reassembler.unassembled_bytes() - bytes_unassembled_before});
    }
}

optional<WrappingInt32> TCPReceiver::ackno() const { 
    if(!isn) return nullopt;
    else return *ack;
}

size_t TCPReceiver::window_size() const { 
    return _reassembler.stream_out().remaining_capacity();
}
