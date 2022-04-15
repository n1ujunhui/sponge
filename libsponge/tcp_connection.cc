#include "tcp_connection.hh"

#include <cassert>
#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}
bool _debug = false;
using namespace std;

inline void TCPConnection::send_RST_segment() {
    _sender.send_empty_segment();
    auto & _seg = _sender.segments_out().back();
    _seg.header().rst = true;
}

inline void TCPConnection::send_FIN_segment() {
    _sender.send_empty_segment();
    auto & _seg = _sender.segments_out().back();
    _seg.header().fin = true;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
__fortify_function int
print(const char *__restrict __fmt, ...) {
    if(_debug) {
        return __printf_chk (__USE_FORTIFY_LEVEL - 1, __fmt, __va_arg_pack ());
    } return 0;
}
#pragma GCC diagnostic pop

inline void TCPConnection::pop_sender_segment_out() {
    while(_sender.segments_out().size()!=0) {
        auto first_segment = _sender.segments_out().front();
        if(_receiver.ackno().has_value()) {
            first_segment.header().ack = true;
            first_segment.header().ackno = _receiver.ackno().value();
            first_segment.header().win = _receiver.window_size();
        }
        _segments_out.push(first_segment);
        _sender.segments_out().pop();
    }
}

inline TCPConnection::SenderState TCPConnection::sender_state() {
    if(_sender.stream_in().error()) return SenderState::ERROR_SEND;
    if(_sender.next_seqno_absolute() == 0) return SenderState::CLOSED;
    if(_sender.next_seqno_absolute() > 0 && _sender.next_seqno_absolute() == _sender.bytes_in_flight())
        return SenderState::SYN_SENT;
    if(_sender.next_seqno_absolute() > _sender.bytes_in_flight() && !_sender.stream_in().eof())
        return SenderState::SYN_ACKED;
    if(_sender.stream_in().eof() && _sender.next_seqno_absolute() < _sender.bytes_in_flight() + 2)
        return SenderState::SYN_ACKED_also;
    if(_sender.stream_in().eof() &&
        _sender.next_seqno_absolute() == _sender.bytes_in_flight() + 2 &&
        _sender.bytes_in_flight() > 0)
        return SenderState::FIN_SENT;
    if(_sender.stream_in().eof() &&
        _sender.next_seqno_absolute() == _sender.bytes_in_flight() + 2 &&
        _sender.bytes_in_flight() == 0)
        return SenderState::FIN_ACKED;
    return SenderState::UNKNOWN_SEND;
}

inline TCPConnection::ReceiverState TCPConnection::receiver_state() {
    if(_receiver.stream_out().error()) return {ReceiverState::ERROR_RECV};
    if(!_receiver.ackno().has_value()) return {ReceiverState::LISTEN};
    if(_receiver.ackno().has_value() && !_receiver.stream_out().input_ended()) return {ReceiverState::SYN_RECV};
    if(_receiver.stream_out().input_ended()) return {ReceiverState::FIN_RECV};
    return {ReceiverState::UNKNOWN_RECV};
}

size_t TCPConnection::remaining_outbound_capacity() const {
    return _sender.stream_in().remaining_capacity();
}

size_t TCPConnection::bytes_in_flight() const {
    return _sender.bytes_in_flight();
}

size_t TCPConnection::unassembled_bytes() const {
    return _receiver.unassembled_bytes();
}

size_t TCPConnection::time_since_last_segment_received() const {
    // print("<Method> `time_since_last_segment_received` called.\n");
    return _accumulate_ms - _accumulate_ms_last_segment_received;
}

size_t TCPConnection::send_something() {
    auto queue_size_before = _sender.segments_out().size();
    _sender.fill_window();
    return _sender.segments_out().size() - queue_size_before;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    _accumulate_ms_last_segment_received = _accumulate_ms;

    // On call to this method, the state of _receiver will change.
    _receiver.segment_received(seg);

    auto & header = seg.header();
    if(header.rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    bool _need_to_send_segment = _receiver.ackno().has_value() &&
                                (seg.length_in_sequence_space() > 0 ||
                                (seg.length_in_sequence_space() == 0 && seg.header().seqno == _receiver.ackno().value() - 1));

    if(header.ack) {
        // this will cause the state of _sender to change.
        _sender.ack_received(header.ackno, header.win);
        _sender.fill_window();
        
        if(_need_to_send_segment && _sender.segments_out().size() > 0) {
            _need_to_send_segment = false;
        }
    }

    auto _sender_state = sender_state();
    auto _receiver_state = receiver_state();
    assert(_sender_state != SenderState::UNKNOWN_SEND);
    assert(_receiver_state != ReceiverState::UNKNOWN_RECV);

    if(_sender_state == SenderState::CLOSED && _receiver_state == ReceiverState::SYN_RECV) {
        connect();
        return;
    }

    if(_sender_state == SenderState::SYN_ACKED && _receiver_state == ReceiverState::FIN_RECV) {
        _linger_after_streams_finish = false;  
    }
    if(_need_to_send_segment) _sender.send_empty_segment();
    pop_sender_segment_out();
}

inline bool TCPConnection::prerequisites_1_to_3() const {
    auto &inbound_stream = _receiver.stream_out();
    auto &outbound_stream = _sender.stream_in();
    return inbound_stream.input_ended() &&
            outbound_stream.input_ended() && 
            outbound_stream.eof() &&
            outbound_stream.bytes_written() + 2 == _sender.next_seqno_absolute() &&
            _sender.bytes_in_flight() == 0;
}

bool TCPConnection::active() const {
    print("<Method> `active` called.\n");
    auto &inbound_stream = _receiver.stream_out();
    auto &outbound_stream = _sender.stream_in();
    
    if(!inbound_stream.error() && !outbound_stream.error()) {
        // if(prerequisites_1_to_3() && !_linger_after_streams_finish) return false;
        // else return true;
        return status != CLOSED;
    } else return false;
    print("\n");
}

size_t TCPConnection::write(const string &data) {
    print("<Method> `write` called.\n");
    auto ret = _sender.stream_in().write(data);
    _sender.fill_window();
    pop_sender_segment_out();
    return ret;
    print("\n");
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    print("<Method> `tick` called.\n");
    print_status();
    _accumulate_ms += ms_since_last_tick;
        print("\tmax retry time: %u, now have tried %u times\n", TCPConfig::MAX_RETX_ATTEMPTS, _sender.consecutive_retransmissions());
    if(_sender.consecutive_retransmissions() >= TCPConfig::MAX_RETX_ATTEMPTS) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        send_RST_segment();
        pop_sender_segment_out();
        return;
    }
    _sender.tick(ms_since_last_tick);

    print("\t<Time> %lu ms has passed.\n", _accumulate_ms);
    print("\tbefore check, %lu segments need to be sent.\n\n", _sender.segments_out().size());
    if(status == TIME_WAIT && time_since_last_segment_received() >= 10* _cfg.rt_timeout) {
        status = CLOSED;
    }
    if(status == ESTABLISHED && _sender.stream_in().input_ended() && _sender.stream_in().bytes_written() == _sender.stream_in().bytes_read()) {
        send_FIN_segment();
        status = FIN_WAIT_1;
    }
    if(status == CLOSE_WAIT && _sender.stream_in().input_ended() && _sender.stream_in().bytes_written() == _sender.stream_in().bytes_read()) {
        send_FIN_segment();
        status = LAST_ACK;
    }

    print_status();
    print("\t%lu segments need to be sent.\n\n", _sender.segments_out().size());
    pop_sender_segment_out();
}

void TCPConnection::end_input_stream() {
    print("<Method> `end_input_stream` called.\n");
    _sender.stream_in().end_input();
    _sender.fill_window();
    pop_sender_segment_out();
    // _sender.send_empty_segment();
    // auto & _seg = _segments_out.back();
    // _seg.header().fin = true;
    // set_ACK_and_window();
}

void TCPConnection::connect() {
    print("<Method> `connect` called.\n");
    _sender.fill_window();
    // printf("sender queue size = %ld\n", _sender.segments_out().size());
    // printf("queue size = %ld\n", _segments_out.size());
    pop_sender_segment_out();
    status = SYN_SENT;
    // printf("sender queue size = %ld\n", _sender.segments_out().size());
    // printf("queue size = %ld\n", _segments_out.size());
    print("\n");
}

TCPConnection::~TCPConnection() {
    print("TCPConnection destructor called.\n");
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            send_RST_segment();
            pop_sender_segment_out();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}