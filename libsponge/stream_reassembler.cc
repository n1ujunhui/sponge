#include "stream_reassembler.hh"
#include <cstring>

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

#define BUFFER_STREAM_OFFSET(index) buffer_for_offset[index]

StreamReassembler::StreamReassembler(const size_t capacity) : _output(capacity), _capacity(capacity) {
    this->buffer = std::make_shared<Buffer>(capacity);
    this->stream_need_offset = 0;
    this->_eof = false;
}

inline std::tuple<size_t, size_t, bool, size_t> StreamReassembler::whetherToBuffer(const size_t index, const size_t size) {
    // clear useless buffers first
    size_t buffer_needed_offset = buffer->getValidStartOffset();
    if(index + size < buffer_needed_offset) return {0, 0, false, 0};
    else if (index > buffer_needed_offset) {
        if(size <= _capacity - (index - buffer_needed_offset))
            return {index, 0, true, size};
        else return {index, 0, true, _capacity - (index - buffer_needed_offset)};
    } else {
        if(size - (buffer_needed_offset - index) <= _capacity)
            return {buffer_needed_offset, buffer_needed_offset - index, true, index + size - buffer_needed_offset};
        else return {buffer_needed_offset, buffer_needed_offset - index, true, _capacity};
    }
    return {0,0, false, 0};
    
    // size_t buffer_index = buffer_start_index;
    // while(BUFFER_STREAM_OFFSET(buffer_index) < stream_need_offset && (BUFFER_STREAM_OFFSET(buffer_index)!=0)) {
    //     buffer_for_offset[buffer_index] = 0;
    //     buffer_used--;
    //     buffer_left_capacity++;
    //     buffer_index++;
    //     _unassembled_bytes--;
    //     if(buffer_index == _capacity) buffer_index = 0;
    // }

    // // write some buffer into the stream
    // if(BUFFER_STREAM_OFFSET(buffer_index) == stream_need_offset) {
    //     size_t remaining_capacity = _output.remaining_capacity();
    //     size_t in_stream_size = 0;
    //     for(size_t i = 0; i < remaining_capacity; ++i) {
    //         if(buffer_stream_offset(buffer_index+i)!=0) {
    //             in_stream_size++;
    //         } else {
    //             break;
    //         }
    //     }
    //     if(buffer_index + in_stream_size >= _capacity) {
    //         _output.write(std::string(&buffer_for_char[buffer_index], _capacity - buffer_index));
    //         _output.write(std::string(&buffer_for_char[0], buffer_index + in_stream_size - _capacity));
    //         for(size_t i = buffer_index; i < _capacity; ++i) {
    //             buffer_stream_offset(i) = 0;
    //         }
    //         for(size_t i = 0; i < buffer_index + in_stream_size - _capacity; ++i) {
    //             buffer_stream_offset(i) = 0;
    //         }
    //         buffer_start_index = buffer_index + in_stream_size - _capacity;
    //     } else {
    //         _output.write(std::string(&buffer_for_char[buffer_index], in_stream_size));
    //         for(size_t i = 0; i < in_stream_size; ++i) {
    //             buffer_stream_offset(buffer_index + i) = 0;
    //         }
    //         buffer_start_index = buffer_index + in_stream_size;
    //     }
    //     buffer_used-=in_stream_size;
    //     buffer_left_capacity+=in_stream_size;
    //     stream_need_offset+=in_stream_size;
    //     _unassembled_bytes-=in_stream_size;
    // }

    // if(index <= stream_need_offset) return false;

    // if(buffer_for_offset[buffer_start_index + index - stream_need_offset]!=0) return false;

    // if(index - stream_need_offset + size > buffer_left_capacity) return false;

    // return true;
}

inline std::pair<size_t, bool> StreamReassembler::whetherToStream(const size_t index, const size_t size) {
    if(index <= stream_need_offset) {
        if(index+size<stream_need_offset) return std::make_pair(0, false);
        else return std::make_pair(stream_need_offset-index, true);
    } else return std::make_pair(0, false);
}
//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    size_t data_size = data.length();
    printf("push string: %s\n", data.c_str());
    auto _whether2Stream = whetherToStream(index, data_size);
    if(_whether2Stream.second) {
        auto str_index = _whether2Stream.first;
        auto writed_num = _output.write(data.substr(str_index));
        printf("write from string index: %ld, writing %ld bytes.\n", str_index, writed_num);
        printf("data size: %ld\n", data_size);
        stream_need_offset += (writed_num);
        if(str_index + writed_num == data_size) {
            stream_need_offset = buffer->removeUselessBuffer(stream_need_offset, _output);
            printf("needing: %ld\n", stream_need_offset);
            printf("unused buffer: %ld\n", buffer->remainingStorage());
            if(eof) {
                this->_output.end_input();
            }
        } else {
            auto data_left = data.substr(str_index+writed_num);
            auto _whether2Buffer = whetherToBuffer(index+str_index+writed_num, data_size-str_index+writed_num);
            printf("whether buffer: %d\n", std::get<2>(_whether2Buffer));
            if(std::get<2>(_whether2Buffer)) {
                buffer->enBuffer(std::get<0>(_whether2Buffer), data_left.substr(std::get<1>(_whether2Buffer), std::get<3>(_whether2Buffer)), eof);
            }
        }
    } else {
        auto _whether2Buffer = whetherToBuffer(index, data_size);
        printf("whether buffer: %d\n", std::get<2>(_whether2Buffer));
        if(std::get<2>(_whether2Buffer)) {
            buffer->enBuffer(std::get<0>(_whether2Buffer), data.substr(std::get<1>(_whether2Buffer), std::get<3>(_whether2Buffer)), eof);
        }
    }
}

size_t StreamReassembler::unassembled_bytes() const { return buffer->getUnassembled(); }

bool StreamReassembler::empty() const { return buffer->getUnassembled() == 0; }
