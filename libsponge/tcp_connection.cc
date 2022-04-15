#include "tcp_connection.hh"

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

inline void TCPConnection::print_status() {
    print("\t<Status> now is %s\n", status_mapper.find(status)->second.c_str());
}

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
    print("<Method> `time_since_last_segment_received` called.\n");
    return _accumulate_ms - _accumulate_ms_last_segment_received;
}

size_t TCPConnection::send_something() {
    auto queue_size_before = _sender.segments_out().size();
    _sender.fill_window();
    return _sender.segments_out().size() - queue_size_before;
}

void TCPConnection::segment_received(const TCPSegment &seg) {
    print("<Method> `segment_received` called.\n");
    print_status();
    _accumulate_ms_last_segment_received = _accumulate_ms;

    auto & header = seg.header();
    if(header.rst) {
        _sender.stream_in().set_error();
        _receiver.stream_out().set_error();
        return;
    }

    auto _before_ack = _receiver.ackno();
    _receiver.segment_received(seg);
    auto _seq_valid = _before_ack != _receiver.ackno();
    print(_seq_valid?"\tThis segment has meaningful seq.\n": "This segment has meaningless seq.\n");
    auto _ack_valid = false;
    if(header.ack) {
        auto _before_seq = _sender.bytes_in_flight();
        _sender.ack_received(header.ackno, header.win);
        _ack_valid = _sender.bytes_in_flight() != _before_seq;
        print(_ack_valid?"\tThis segment has meaningful ack.\n": "This segment has meaningless ack.\n");
    }
    if(_receiver.ackno().has_value())
    print("Now ack = %u, seqno = %u\n", _receiver.ackno().value().raw_value(), _sender.next_seqno().raw_value());

    switch (status) {
        /**
         *  The connection has closed.
         **/
        case CLOSED:
            break;

        /**
         * @brief listen for a conncetion
         * 
         */
        case LISTEN:
            if(header.syn) {
                if(send_something() == 0) {
                    print("<<something wrong>>\n");
                }
                status = SYN_RECEIVED;
            }
            break;
        case SYN_RECEIVED:
            if(_ack_valid) {
                status = ESTABLISHED;
            }
            break;
        /**
         * The connection sent a SYN and is waiting for ACK and SYN
         * If get a ACK & SYN packet, respond a ACK packet and the status becomes ESTABISHED.
         * If not, wait
         */
        case SYN_SENT:
            if(header.ack && header.syn) {
                _sender.send_empty_segment();
                status = ESTABLISHED;
            }
            if(header.syn && !header.ack) {
                _sender.send_empty_segment();
                status = SYN_RECEIVED;
            }
            break;

        /**
         * @brief status: ESTABLISHED
         * The connection is in ESTABLISHED status.
         * If the packet is normal (without FIN), then do something normal.
         * If the packet has FIN, then we need to change to next passive close status.
         */
        case ESTABLISHED:
            print("\t\t status is ESTABLISHED\n");
            if(header.fin) {
                _sender.send_empty_segment();
                status = CLOSE_WAIT;
                _linger_after_streams_finish = false;
                break;
            }


            print("\t\theader.seqno = %u\n", header.seqno.raw_value());
            print("\t\tseg.length_in_sequence_space() = %lu\n", seg.length_in_sequence_space());
            if(seg.length_in_sequence_space() == 0 && _receiver.ackno().has_value()
                && header.seqno == _receiver.ackno().value() - 1) {
                print("sending a empty segment\n");
                _sender.send_empty_segment();
                break;
            }
            
            if(seg.length_in_sequence_space() > 0) {
                if(send_something() == 0) _sender.send_empty_segment();
                break;
            } else {
                send_something();
            }
            break;
            
        /**
         * @brief status: CLOSE_WAIT
         * passive close, so nothing to deal with the segment.
         */
        case CLOSE_WAIT:
            if(send_something() == 0) {
                _sender.send_empty_segment();
            }
            break;

        /**
         * @brief status: FIN_WAIT_1
         * active close and just sent a FIN packet.
         */
        case FIN_WAIT_1:
            // Get ACK and FIN, go to TIME_WAIT status.
            if(header.fin && header.ack) {
                status = TIME_WAIT; 
                _sender.send_empty_segment();
            }

            // GET FIN packet, go to CLOSING status
            if(header.fin && !header.ack) {
                status = CLOSING;
                _sender.send_empty_segment();
            }

            // The peer seems not to close the connection
            if(!header.fin && _seq_valid) {
                status = FIN_WAIT_2;
            }
            break;

        /**
         * @brief status: FIN_WAIT_2
         * In this state, connection needs the peer to send FIN packet.
         */
        case FIN_WAIT_2:
            if(header.fin) {
                status = TIME_WAIT;
                _sender.send_empty_segment();
            }
            break;
        
        /**
         * @brief status: CLOSING
         * In this state, needs peer to send an ACK of FIN
         */
        case CLOSING:
            if(_seq_valid) {
                status = TIME_WAIT;
            }
            break;
        
        /**
         * @brief TIME_WAIT status
         * waiting for timeout to be CLOSED
         */
        case TIME_WAIT:
            if(seg.length_in_sequence_space() > 0) {
                _sender.send_empty_segment();
            }
            break;
        case LAST_ACK:
            if(_ack_valid) status = CLOSED;
            break;
        default:
            break;
    }
    pop_sender_segment_out();

    // if(status == FIN_WAIT_2) {
        
    //     if(header.fin) {
    //         status = TIME_WAIT; 
    //         _sender.send_empty_segment();
    //         pop_sender_segment_out();
    //     }
    //     return;
    // }

    // if(status == TIME_WAIT) {
    //     printf("case: status == TIME_WAIT\n");
    //     _receiver.segment_received(seg);
    //     _sender.ack_received(header.ackno, header.win);
    //     printf("%lu segments need to be sent.\n", _sender.segments_out().size());
    //     _sender.send_empty_segment();
    //     printf("%lu segments need to be sent.\n", _sender.segments_out().size());
    //     return;
    // }
    
    // // status: ESTABLISHED
    // _receiver.segment_received(seg);

    // // if(_receiver.stream_out().input_ended() && !_sender.stream_in().input_ended()) {
    // //     _linger_after_streams_finish = false;
    // // }

    // if(header.ack) {
    //     printf("get ack: %u\n", header.ackno.raw_value());
    //     printf("size = %lu\n", seg.length_in_sequence_space());
    //     _sender.ack_received(header.ackno, header.win);
    // }

    // printf("segment sequence length = %lu\n", seg.length_in_sequence_space());

    // auto _seqno = header.seqno;
    // if(seg.length_in_sequence_space() == 0 && _receiver.ackno().has_value()
    //     && _seqno == _receiver.ackno().value() - 1) {
    //     printf("sending a empty segment\n");
    //     _sender.send_empty_segment();
    //     set_ACK_and_window();
    //     return;
    // }
    // auto before = _sender.segments_out().size();
    // _sender.fill_window();
    // if(_sender.segments_out().size() == before) {
    //     printf("sending a empty segment\n");
    //     _sender.send_empty_segment();
    // }
    // set_ACK_and_window();
    print_status();
    print("\n");
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
    if(status == ESTABLISHED) status = FIN_WAIT_1;
    if(status == CLOSE_WAIT) status = LAST_ACK;
    // _sender.send_empty_segment();
    // auto & _seg = _segments_out.back();
    // _seg.header().fin = true;
    // set_ACK_and_window();
    print("\n");
}

void TCPConnection::connect() {
    print("<Method> `connect` called.\n");
    if(status != LISTEN) {
        print("not in CLOSED status, should not connect.\n");
        return;
    }
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