#pragma once
#include <memory>

class FixedCapacityBuffer {
public:
    explicit FixedCapacityBuffer(size_t capacity);

    bool insert(const unsigned char* data, size_t size);
    [[nodiscard]] size_t size() const;
    [[nodiscard]] size_t capacity() const;
    [[nodiscard]] size_t remaining_capacity() const;
    [[nodiscard]] bool empty() const;
    void clear();

    // New function to adjust capacity
    bool set_capacity(size_t new_capacity);

    // Function to get raw pointer to buffer
    unsigned char* data();
    const unsigned char* data() const;

    unsigned char& operator[](size_t index);
    const unsigned char& operator[](size_t index) const;

private:
    size_t capacity_;
    size_t size_;  // Tracks how much of the buffer is used
    std::unique_ptr<unsigned char[]> buffer_;
};

/*class AudioDataBuffer : public FixedCapacityBuffer {
public:
    explicit AudioDataBuffer(const size_t capacity) : FixedCapacityBuffer(capacity) {}

    size_t current_pos = 0;
};*/
