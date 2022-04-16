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

inline void TCPConnection::send_FIN_segment() {
    _sender.send_empty_segment();
    auto & _seg = _sender.segments_out().back();
    _seg.header().fin = true;
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

inline TCPConnection::SenderState TCPConnection::sender_state() {
    if(_sender.stream_in().error()) return SenderState::ERROR_SEND;
    if(_sender.next_seqno_absolute() == 0) return SenderState::CLOSED;
    if(_sender.next_seqno_absolute() > 0 && _sender.next_seqno_absolute() == _sender.bytes_in_flight())
        return SenderState::SYN_SENT;
    if(_sender.next_seqno_absolute() > _sender.bytes_in_flight() && !_sender.stream_in().eof())
        return SenderState::SYN_ACKED;
    if(_sender.stream_in().eof() && _sender.next_seqno_absolute() < _sender.stream_in().bytes_written() + 2)
        return SenderState::SYN_ACKED_also;
    if(_sender.stream_in().eof() &&
        _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 &&
        _sender.bytes_in_flight() > 0)
        return SenderState::FIN_SENT;
    if(_sender.stream_in().eof() &&
        _sender.next_seqno_absolute() == _sender.stream_in().bytes_written() + 2 &&
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
    return _time_since_last_segment;
}

size_t TCPConnection::send_something() {
    auto queue_size_before = _sender.segments_out().size();
    _sender.fill_window();
    return _sender.segments_out().size() - queue_size_before;
}

inline void TCPConnection::set_RST(bool send) {
    _sender.stream_in().set_error();
    _receiver.stream_out().set_error();
    _active = false;

    if(send) {
        _sender.send_empty_segment();
        auto & _seg = _sender.segments_out().back();
        _seg.header().rst = true;
        pop_sender_segment_out();
    }

}
void TCPConnection::segment_received(const TCPSegment &seg) {

    if(!_active) return;

    _time_since_last_segment = 0;

    // set_RST
    if(seg.header().rst) {
        set_RST(false);
        return;
    }

    auto _sender_state = sender_state();
    auto _receiver_state = receiver_state();

    bool _need_send_ACK = false;
    bool _need_send_ACK_SYN = false;

    /**
     * @brief 3-handshake state
     * The state of sender: CLOSED, SYN_SENT
     * The state of receiver: LISTEN
     * 
     */
    if(_receiver_state == ReceiverState::LISTEN) {

        // active connect
        if(_sender_state == SenderState::SYN_SENT) {

            // receive a SYN & ACK
            if(seg.header().syn && seg.header().ack) {
                _receiver.segment_received(seg);
                assert(receiver_state() == ReceiverState::SYN_RECV);
                _sender.ack_received(seg.header().ackno, seg.header().win);
                assert(sender_state() == SYN_ACKED);

                // We need to send an ACK back.
                _need_send_ACK = true;
                goto Send;
            }
            
            /**
             * @brief The peer also tried to connect actively.
             * At this time, only SYN is useful.
             * So let's become the passive one and send a ACK & SYN to the peer.
             */
            if(seg.header().syn && !seg.header().ack) {
                _receiver.segment_received(seg);
                assert(receiver_state() == ReceiverState::SYN_RECV);
                _need_send_ACK = true;
                goto Send;
            }
        }

        // passive connect
        if(_sender_state == SenderState::CLOSED) {
            /**
             * @brief We need to receive a SYN
             * And send a SYN & ACK to the peer.
             */
            if(seg.header().syn) {
                _receiver.segment_received(seg);
                assert(receiver_state() == ReceiverState::SYN_RECV);
                _need_send_ACK_SYN = true;
            }
            goto Send;
            
        }
    }

    if(_receiver_state == ReceiverState::SYN_RECV) {
        
        // passive connector
        if(_sender_state == SenderState::SYN_SENT) {
            if(seg.header().ack) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                assert(sender_state() == SenderState::SYN_ACKED);
                goto Send;
            }
        }

        /**
         * @brief Normal state
         * 
         */
        if(_sender_state == SenderState::SYN_ACKED || _sender_state == SenderState::SYN_ACKED_also) {
            _receiver.segment_received(seg);
            if(seg.header().fin) {
                assert(receiver_state() == FIN_RECV);
                _linger_after_streams_finish = false;
            }
            _need_send_ACK = (_receiver.ackno().has_value() && 
                            (seg.length_in_sequence_space() == 0)
                            && seg.header().seqno == _receiver.ackno().value() - 1) ||
                            (seg.length_in_sequence_space() > 0);

            if(seg.header().ack) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                _sender.fill_window();
                if(_sender.segments_out().size() > 0) _need_send_ACK = false;
            }
            goto Send;
        }

        /**
         * @brief We close the connection actively.
         * We need a FIN or FIN & ACK
         */
        if(_sender_state == SenderState::FIN_SENT) {

            // The peer also wants to close actively
            if(seg.header().ack && seg.header().fin) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                _receiver.segment_received(seg);
                assert(receiver_state() == FIN_RECV);
                assert(sender_state() == FIN_ACKED || sender_state() == FIN_SENT);
                _need_send_ACK = true;
                goto Send;
            }

            // The peer still wants to send.
            if(seg.header().ack && !seg.header().fin) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                _receiver.segment_received(seg);
                assert(receiver_state() == SYN_RECV);
                assert(sender_state() == FIN_ACKED || sender_state() == FIN_SENT);
                _need_send_ACK = false;
                goto Send;
            }

            // The peer's last segment.
            if(seg.header().fin) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                _receiver.segment_received(seg);
                assert(receiver_state() == FIN_RECV);
                assert(sender_state() == FIN_ACKED);
                _need_send_ACK = true;
                goto Send;
            }
        }
        if(_sender_state == SenderState::FIN_ACKED) {
            if(seg.header().fin) {
                _receiver.segment_received(seg);
                assert(receiver_state() == FIN_RECV);
                assert(sender_state() == FIN_ACKED);
                _need_send_ACK = true;
                goto Send;
            }
        }
    }

    if(_receiver_state == ReceiverState::FIN_RECV) {
        if(_sender_state == SenderState::SYN_ACKED || _sender_state == SenderState::SYN_ACKED_also) {
            _receiver.segment_received(seg);
            if(seg.header().fin) {
                assert(receiver_state() == FIN_RECV);
                _linger_after_streams_finish = false;
            }
            _need_send_ACK = (_receiver.ackno().has_value() && 
                            (seg.length_in_sequence_space() == 0)
                            && seg.header().seqno == _receiver.ackno().value() - 1) ||
                            (seg.length_in_sequence_space() > 0);

            if(seg.header().ack) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                _sender.fill_window();
                if(_sender.segments_out().size() > 0) _need_send_ACK = false;
            }
            goto Send;
        }
        if(_sender_state == SenderState::FIN_SENT) {
            if(seg.header().ack) {
                _sender.ack_received(seg.header().ackno, seg.header().win);
                if(!_linger_after_streams_finish && sender_state() == SenderState::FIN_ACKED) _active = false;
                goto Send;
            }
        }
        if(_sender_state == SenderState::FIN_ACKED) {
            if(!_linger_after_streams_finish) _active = false;
        }
    }

    /**
     * @brief We have received a FIN segment.
     * So just send anything you want.
     */
    _receiver.segment_received(seg);
    if(seg.header().ack) {
        _sender.ack_received(seg.header().ackno, seg.header().win);
    }
    _need_send_ACK = (_receiver.ackno().has_value() && 
                            (seg.length_in_sequence_space() == 0)
                            && seg.header().seqno == _receiver.ackno().value() - 1) ||
                            (seg.length_in_sequence_space() > 0);
    _sender.fill_window();
    if(_sender.segments_out().size() > 0) _need_send_ACK = false;

    Send:
    assert(!(_need_send_ACK && _need_send_ACK_SYN));
    if(_need_send_ACK) {
        _sender.send_empty_segment();
    }
    if(_need_send_ACK_SYN) {
        _sender.fill_window();
    }
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
    return _active;
}

size_t TCPConnection::write(const string &data) {
    auto ret = _sender.stream_in().write(data);
    _sender.fill_window();
    pop_sender_segment_out();
    return ret;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    _time_since_last_segment += ms_since_last_tick;

    if(!_active) return;

    if(_sender.consecutive_retransmissions() >= TCPConfig::MAX_RETX_ATTEMPTS) {
        set_RST(true);
        return;
    }
    _sender.tick(ms_since_last_tick);

    auto _receiver_state = receiver_state();
    auto _sender_state = sender_state();

    if(!_linger_after_streams_finish && _receiver_state == FIN_RECV && _sender_state == FIN_ACKED) {
        _active = false;
    }

    if(_linger_after_streams_finish && _receiver_state == FIN_RECV && _sender_state == FIN_ACKED) {
        if(time_since_last_segment_received() >= 10* _cfg.rt_timeout) {
            _active = false;
        }
    }

    pop_sender_segment_out();
}

void TCPConnection::end_input_stream() {
    _sender.stream_in().end_input();
    _sender.fill_window();
    pop_sender_segment_out();
    // _sender.send_empty_segment();
    // auto & _seg = _segments_out.back();
    // _seg.header().fin = true;
    // set_ACK_and_window();
}

void TCPConnection::connect() {
    _sender.fill_window();
    pop_sender_segment_out();
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            set_RST(true);
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}