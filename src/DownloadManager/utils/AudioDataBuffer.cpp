#include "AudioDataBuffer.h"
#include <cstring>  // for memcpy
#include <cassert>

FixedCapacityBuffer::FixedCapacityBuffer(size_t capacity)
    : capacity_(capacity), size_(0), buffer_(std::make_unique<unsigned char[]>(capacity)) {}

bool FixedCapacityBuffer::insert(const unsigned char* data, size_t size) {
    if (size_ + size > capacity_) {
        return false;
    }
    std::memcpy(buffer_.get() + size_, data, size);
    size_ += size;
    return true;
}

size_t FixedCapacityBuffer::size() const { return size_; }

size_t FixedCapacityBuffer::capacity() const { return capacity_; }

size_t FixedCapacityBuffer::remaining_capacity() const {
    return capacity_ - size_;
}

bool FixedCapacityBuffer::empty() const { return size_ == 0; }

void FixedCapacityBuffer::clear() { size_ = 0; }

// New function to set a new capacity
bool FixedCapacityBuffer::set_capacity(size_t new_capacity) {
    if (new_capacity < size_) {
        return false;  // Cannot reduce capacity below current size
    }

    std::unique_ptr<unsigned char[]> new_buffer = std::make_unique<unsigned char[]>(new_capacity);
    std::memcpy(new_buffer.get(), buffer_.get(), size_);
    buffer_ = std::move(new_buffer);
    capacity_ = new_capacity;

    return true;
}

// Function to get raw pointer to buffer
unsigned char* FixedCapacityBuffer::data() {
    return buffer_.get();
}

const unsigned char* FixedCapacityBuffer::data() const {
    return buffer_.get();
}

unsigned char& FixedCapacityBuffer::operator[](size_t index) {
    assert(index < size_);
    return buffer_[index];
}

const unsigned char& FixedCapacityBuffer::operator[](size_t index) const {
    assert(index < size_);
    return buffer_[index];
}
