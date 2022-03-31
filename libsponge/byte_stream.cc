#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t _capacity) {
    this->length = 0;
    this->length_left = _capacity;
    this->capacity = _capacity;
    this->byte_array = std::unique_ptr<char[]>(new char[this->capacity]);
}

void ByteStream::insert(const char & insert_char) {
    if(this->write_pointer >= this->capacity) {
        this->write_pointer -= this->capacity;
    }
    byte_array[this->write_pointer] = insert_char;
    this->write_pointer++;
}

size_t ByteStream::write(const string &data) {
    if(this->_end) return 0;
    size_t length_string = data.length();
    size_t ret = (length_string > this->length_left? this->length_left: length_string);
    for(size_t i = 0; i < ret; i++) {
        insert(data.at(i));
    }
    this->length += ret;
    this->length_left -= ret;
    this->_bytes_written += ret;
    return ret;
}

char & ByteStream::get(size_t index) const {
    if(index >= this->capacity) {
        index -= this->capacity;
    }
    return byte_array[index];
}
//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const {
    size_t ret_length = len > this->length? this->length: len;
    string ret_string = "";
    for(size_t i = 0; i<ret_length; i++) {
        ret_string += get(read_pointer+i);
    }
    return ret_string;
}

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    size_t pop_length = len > this->length? this->length: len;
    read_pointer += pop_length;
    while(read_pointer >= capacity) {
        read_pointer -= capacity;
    }
    this->length -= pop_length;
    this->length_left += pop_length;
    this->_bytes_read += pop_length;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    std::string ret = peek_output(len);
    pop_output(len);
    return ret;
}

void ByteStream::end_input() {
    this->_end = true;
}

bool ByteStream::input_ended() const { return this->_end; }

size_t ByteStream::buffer_size() const { return this->length; }

bool ByteStream::buffer_empty() const { return this->length==0; }

bool ByteStream::eof() const { return this->_end && (this->length==0); }

size_t ByteStream::bytes_written() const { return _bytes_written; }

size_t ByteStream::bytes_read() const { return _bytes_read; }

size_t ByteStream::remaining_capacity() const { return this->length_left; }
