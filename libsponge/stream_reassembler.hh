#ifndef SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
#define SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH

#include "byte_stream.hh"

#include <cstdint>
#include <string>

//! \brief A class that assembles a series of excerpts from a byte stream (possibly out of order,
//! possibly overlapping) into an in-order byte stream.
class StreamReassembler {
  private:
    // Your code here -- add private members as necessary.

    ByteStream _output;  //!< The reassembled in-order byte stream
    size_t _capacity;    //!< The maximum number of bytes

    class Buffer {
      private:
        std::unique_ptr<char[]> buffer_for_char{};
        std::unique_ptr<bool[]> valid_flags{};
        size_t valid_start_offset{};
        size_t valid_index{};
        size_t used_storage{};
        size_t _capacity{};
        size_t eof_index{};
        bool eof_set{};

        inline size_t indexToOffset(size_t index) {
          if (index < valid_index) {
            return index + _capacity - valid_index + valid_start_offset;
          } else return index - valid_index + valid_start_offset;
        }

        inline char& indexToChar(size_t index) {
          return buffer_for_char[index];
        }

        inline void singleWrite(size_t index, const char & c, bool eof) {
          // printf("writing %c, used_storage = %ld\n", c, used_storage);
          while(index>=_capacity) {
            index = index % _capacity;
          }
          if(valid_flags[index] == true) return;
          if(eof) {
            this->eof_set = true;
            this->eof_index = index;
          }
          buffer_for_char[index] = c;
          valid_flags[index] = true;
          used_storage++;
        }
      
      public:
        Buffer(size_t capacity) {
          this->buffer_for_char = std::unique_ptr<char[]>(new char[capacity]);
          this->valid_flags = std::unique_ptr<bool[]>(new bool[capacity]);
          this->valid_index = 0;
          this->used_storage = 0;
          this->valid_start_offset = 0;
          this->_capacity = capacity;
          for(size_t i = 0; i < capacity; ++i) {
            valid_flags[i] = false;
          }
          this->eof_set = false;
          this->eof_index = 0;
        }

        size_t getValidStartOffset() {
          return valid_start_offset;
        }

        void enBuffer(size_t string_offset, const std::string &data, bool eof) {
          size_t data_size = data.length();
          size_t buffer_start_offset = indexToOffset(valid_index);
          size_t start_write_index = valid_index + string_offset - buffer_start_offset;
          for(size_t i = 0; i < data_size; ++i) {
            singleWrite(start_write_index+i, data.at(i), eof);
          }
        }

        size_t remainingStorage() {
          return _capacity - used_storage;
        }

        size_t getUnassembled() {
          return used_storage;
        }

        size_t removeUselessBuffer(size_t needed_offset, ByteStream &stream) {
          // printf("need: %ld\n", needed_offset);
          // printf("now valid_start_offset: %ld\n", valid_start_offset);
          // printf("now used storage: %ld\n", used_storage);
          while(needed_offset > valid_start_offset) {
            if(valid_flags[valid_index]) {
              valid_flags[valid_index] = false;
              used_storage--;
            }
            valid_start_offset++;
            valid_index++;
            if(valid_index == _capacity) valid_index = 0;
          }
          // printf("now valid_start_offset: %ld\n", valid_start_offset);
          // printf("now used storage: %ld\n", used_storage);
          size_t writing_size = 0;
          size_t tmp_index = valid_index;
          bool overflow = false;
          bool _end = false;
          size_t stream_remaining_capacity = stream.remaining_capacity();
          // printf("stream has %ld unused.\n", stream_remaining_capacity);
          while(writing_size<stream_remaining_capacity && valid_flags[tmp_index]) {
            if(eof_set && eof_index == tmp_index) _end = true;
            writing_size ++;
            tmp_index++;
            if(tmp_index == _capacity) {
              overflow = true;
              tmp_index = 0;
            }
          }
          // printf("writing %ld bytes.\n", writing_size);
          if(writing_size > 0) {
            if(!overflow) {
              auto start = &valid_flags[valid_index];
              auto end = start + writing_size;
              std::fill(start, end, false);
              stream.write(std::string(&buffer_for_char[valid_index], writing_size));
              valid_index += writing_size;
              used_storage -= writing_size;
              if(valid_index == _capacity) valid_index = 0;
              valid_start_offset += writing_size;
            } else {
              auto start = &valid_flags[valid_index];
              auto end = start + _capacity-valid_index;
              std::fill(start, end, false);
              start = &valid_flags[0];
              end = start + writing_size + valid_index - _capacity;
              std::fill(start, end, false);
              stream.write(std::string(&buffer_for_char[valid_index], _capacity-valid_index));
              stream.write(std::string(&buffer_for_char[0], writing_size + valid_index - _capacity));
              valid_index = writing_size + valid_index - _capacity;
              used_storage -= writing_size;
              valid_start_offset += writing_size;
            }
            if(_end) stream.end_input();
          }
           // printf("now used storage: %ld\n", used_storage);
          return valid_start_offset;
        }

    };

    std::shared_ptr<Buffer> buffer{};

    inline std::tuple<size_t, size_t, bool, size_t> whetherToBuffer(const size_t index, const size_t size);

    inline std::pair<size_t, bool> whetherToStream(const size_t index, const size_t size);

    inline size_t & buffer_stream_offset(size_t);

    void insert2Buffer(const std::string &) const;

    size_t stream_need_offset{};
    
    bool _eof{};

  public:
    //! \brief Construct a `StreamReassembler` that will store up to `capacity` bytes.
    //! \note This capacity limits both the bytes that have been reassembled,
    //! and those that have not yet been reassembled.
    StreamReassembler(const size_t capacity);

    //! \brief Receive a substring and write any newly contiguous bytes into the stream.
    //!
    //! The StreamReassembler will stay within the memory limits of the `capacity`.
    //! Bytes that would exceed the capacity are silently discarded.
    //!
    //! \param data the substring
    //! \param index indicates the index (place in sequence) of the first byte in `data`
    //! \param eof the last byte of `data` will be the last byte in the entire stream
    void push_substring(const std::string &data, const uint64_t index, const bool eof);

    //! \name Access the reassembled byte stream
    //!@{
    const ByteStream &stream_out() const { return _output; }
    ByteStream &stream_out() { return _output; }
    //!@}

    //! The number of bytes in the substrings stored but not yet reassembled
    //!
    //! \note If the byte at a particular index has been pushed more than once, it
    //! should only be counted once for the purpose of this function.
    size_t unassembled_bytes() const;

    //! \brief Is the internal state empty (other than the output stream)?
    //! \returns `true` if no substrings are waiting to be assembled
    bool empty() const;
};

#endif  // SPONGE_LIBSPONGE_STREAM_REASSEMBLER_HH
