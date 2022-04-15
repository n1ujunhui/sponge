#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

#define SEGMENT_MAX_SEQNO(seg) seg.header().seqno.raw_value() + seg.length_in_sequence_space() + (seg.header().syn?1:0) + (seg.header().fin?1:0)

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : _isn(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , _initial_retransmission_timeout{retx_timeout}
    , _stream(capacity) {}

uint64_t TCPSender::bytes_in_flight() const {
    return _bytes_in_flight;
}

void TCPSender::fill_window() {
    // printf("fill window\n");
    if(!isn_sent) {
        TCPSegment segment{};
        TCPHeader header{};
        header.syn = 1;
        header.seqno = _isn;
        segment.header() = header;
        _segments_out.push(segment);
        outstanding_segments.push(segment);
        isn_sent = true;
        _next_seqno = 1;
        // printf("isn = %u\n", _isn.raw_value());
        // printf("_bytes_in_flight = %lu\n", _bytes_in_flight);
        _bytes_in_flight += 1;
        // printf("_bytes_in_flight = %lu\n", _bytes_in_flight);
        currenct_RTO = _initial_retransmission_timeout;
        if(!alarm.on) {
            alarm.expired_ms = alarm.accumulate_ms + currenct_RTO;
            alarm.on = true;
        }
        return;
    }

    // calculate the size going to send

    while(true) {
        if(test) break;
        uint64_t _size = 0;
        if(_window_size == 0) {
            _size = 1;
            test = true;
        }
        else _size = _window_size <= _bytes_in_flight? 0UL: ((_window_size - _bytes_in_flight) < TCPConfig::MAX_PAYLOAD_SIZE? (_window_size - _bytes_in_flight): TCPConfig::MAX_PAYLOAD_SIZE);
        // printf("_size = %lu\n", _size);
        
        if(_size == 0 || _eof) break;
        
        TCPSegment segment{};

        {
            std::string str_out = _stream.read(_size);
            if(_stream.eof() && str_out.size() + 1 <= (_window_size == 0?1:_window_size)) {
                _eof = true;
                // printf("stream has ended.\n");
            }
            if(!_eof && str_out.size() == 0) break;
            TCPHeader header{};
            header.seqno = wrap(_next_seqno, _isn);
            // printf("header seqno = %u\n", header.seqno.raw_value());
            header.fin = _eof;
            Buffer buffer(std::move(str_out));
            segment.header() = header;
            segment.payload() = buffer;
        }

        outstanding_segments.push(segment);
        
        if(!alarm.on) {
            alarm.expired_ms = alarm.accumulate_ms + currenct_RTO;
            alarm.on = true;
        }

        // printf("before next seqno = %lu\n", _next_seqno);
        // printf("unwrap out = %lu\n", unwrap(_seqno, _isn, _next_seqno));
        _next_seqno += segment.length_in_sequence_space();
        _bytes_in_flight += segment.length_in_sequence_space();
        _segments_out.push(segment);
        

        // printf("now next seqno = %lu\n", _next_seqno);
        // printf("going to send: %s, size: %ld, seqno: %u\n",
        // outstanding_segments.back().payload().str().cbegin(), outstanding_segments.back().payload().size(), outstanding_segments.back().header().seqno.raw_value());
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    // printf("ack received called, ackno = %u, window_size = %u\n", ackno.raw_value(), window_size);
    // printf("unwrap(ackno, _isn, _next_seqno) = %lu, _next_seqno = %lu\n", unwrap(ackno, _isn, _next_seqno), _next_seqno);
    // ignore impossible ackno
    if(unwrap(ackno, _isn, _next_seqno) > _next_seqno) return;
    bool pop = false;
    while(outstanding_segments.size()>0) {
        auto & front = outstanding_segments.front();
        auto & front_header = front.header();
        auto segment_size = front.length_in_sequence_space();
        // printf("front_header.seqno: %u\n", front_header.seqno.raw_value());
        if(front_header.seqno.raw_value() + segment_size <= ackno.raw_value()) {
            if(!pop) pop = true;
            // printf("find an outstanding segment, size = %lu\n", segment_size);
            // printf("before bytes_in_flisht: %lu\n", _bytes_in_flight);
            _bytes_in_flight -= segment_size;
            // printf("after bytes_in_flisht: %lu\n", _bytes_in_flight);
            outstanding_segments.pop();
            continue;
        }
        break;
    }
    if(pop) {
        currenct_RTO = _initial_retransmission_timeout;
        _consecutive_retransmissions = 0;
    }

    if(outstanding_segments.size() == 0) {
        alarm.on = false;
        currenct_RTO = _initial_retransmission_timeout;
    } else if(pop) {
        alarm.on = true;
        alarm.expired_ms = alarm.accumulate_ms + currenct_RTO;
    }

    _seqno = ackno;
    _window_size = window_size;
    if(pop && window_size==0) test = false;
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // printf("tick\n");
    // no outstanding segments, turn off the alarm.
    // printf("outstanding_num = %lu\n", outstanding_segments.size());
    if(outstanding_segments.size() == 0) {
        alarm.on = false;
        currenct_RTO = _initial_retransmission_timeout;
        alarm.accumulate_ms += ms_since_last_tick;
        return;
    }

    // there is outstanding segments and alarm expired
    // printf("accumulate_ms = %lu, ms_since_last_tick=%lu\n", alarm.accumulate_ms, ms_since_last_tick);
    // alarm.print();
    if(alarm.on && ms_since_last_tick + alarm.accumulate_ms >= alarm.expired_ms) {
        // ("resend\n");

        // resend
        auto & segment = outstanding_segments.front();
        _segments_out.push(segment);

        // set RTO
        if(_window_size != 0) {
            currenct_RTO += currenct_RTO;
            _consecutive_retransmissions++;
        }

        // reset the alarm
        alarm.accumulate_ms += ms_since_last_tick;
        alarm.expired_ms = alarm.accumulate_ms + currenct_RTO;
        return;
    }
    alarm.accumulate_ms += ms_since_last_tick;
    DUMMY_CODE(ms_since_last_tick);
}

unsigned int TCPSender::consecutive_retransmissions() const { return {
    _consecutive_retransmissions
}; }

void TCPSender::send_empty_segment() {
    TCPSegment segment{};
    // printf("send_empty_segment\n");
    segment.header().seqno = wrap(_next_seqno, _isn);
    _segments_out.push(segment);

}
