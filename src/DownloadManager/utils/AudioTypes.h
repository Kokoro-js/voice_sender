// AudioTypes.h
#pragma once

#include <folly/io/IOBufQueue.h>
#include "AudioDataBuffer.h"
#include <variant>

// 定义音频当前状态枚举
enum class AudioCurrentState {
    Downloading,
    DownloadAndWriteFinished,
    DrainFinished,
};

// 定义音频格式信息结构体
struct AudioFormatInfo {
    int sample_rate = 0;
    int channels = 0;
    int encoding = -1;
    int bytes_per_sample = 0;
    int bits_per_samples = 0;
};

struct IDataWrapper {
    enum class Type {
        Buffer,
        IOBuf
    };
    Type type_;
    [[nodiscard]] Type getType() const { return type_; }

    explicit IDataWrapper(Type type) : type_(type) {}

    size_t pos_ = 0;                     // 已读取的总偏移量
    bool is_eof = false;                       // 标识缓冲区是否已经结束

    [[nodiscard]] virtual size_t size() const { return 0; };
    // virtual void setup();
    virtual void readFront(std::vector<char> &audio_data,  size_t bytesToRead) {};
};

struct BufferWarp : IDataWrapper {
    FixedCapacityBuffer* buffer = nullptr;

    explicit BufferWarp() : IDataWrapper(Type::Buffer) {};
    explicit BufferWarp(FixedCapacityBuffer* fixed_capacity_buffer) : IDataWrapper(Type::Buffer), buffer(fixed_capacity_buffer) {}

    [[nodiscard]] size_t size() const override  {
        return buffer->size();
    }

    void readFront(std::vector<char> &audio_data, size_t bytesToRead) override {
        auto bytes = std::min(bytesToRead, buffer->size());
        audio_data.insert(audio_data.end(), buffer->data(), buffer->data() + bytes);
    };

    void setup(FixedCapacityBuffer* fixed_capacity_buffer) {
        buffer = fixed_capacity_buffer;

        is_eof = false;
        pos_ = 0;
    }
};

// 定义 IOBufWarp 结构体
struct IOBufWarp : IDataWrapper {
    folly::IOBufQueue* io_buf_queue = nullptr;
    const folly::IOBuf* current_ = nullptr;    // 当前读取的 IOBuf 节点
    size_t offset_ = 0;                        // 当前节点的读取偏移量

    explicit IOBufWarp() : IDataWrapper(Type::IOBuf) {};
    explicit IOBufWarp(folly::IOBufQueue* iobuf) : IDataWrapper(Type::IOBuf), io_buf_queue(iobuf) {}

    void readFront(std::vector<char> &audio_data, size_t bytesToRead) override {
        auto chain = io_buf_queue->front();
        size_t bytesRead = 0;

        // 确保 vector 有足够的空间
        audio_data.reserve(bytesToRead);

        // 遍历整个链表，直到读取足够的字节或链表耗尽
        for (auto iter = chain; iter && bytesRead < bytesToRead; iter = iter->next()) {
            auto data = iter->data();
            auto len = iter->length();
            size_t remainingBytes = bytesToRead - bytesRead;
            size_t toRead = std::min(len, remainingBytes);

            // 复制数据到 audio_data 中
            audio_data.insert(audio_data.end(), data, data + toRead);
            bytesRead += toRead;
        }
    };

    void setup(folly::IOBufQueue* iobuf_buf_queue_) {
        io_buf_queue = iobuf_buf_queue_;
        current_ = io_buf_queue->front();
        offset_ = 0;

        is_eof = false;
        pos_ = 0;
    }

    [[nodiscard]] size_t size() const override  {
        return io_buf_queue->chainLength();
    }

    // 更新 current_ 和 offset_，使其与 total_pos_ 对应
    void updateCurrentIOBuf() {
        size_t pos = pos_;
        size_t accumulated = 0;
        current_ = io_buf_queue->front();

        while (current_) {
            size_t buf_length = current_->length();
            if (accumulated + buf_length > pos) {
                offset_ = pos - accumulated;
                return;
            } else {
                accumulated += buf_length;
                current_ = current_->next();
            }
        }
        // 如果未找到，设置 current_ 为 nullptr，表示没有更多数据
        current_ = nullptr;
        offset_ = 0;
    }
};

using DataVariant = std::variant<BufferWarp, IOBufWarp>;

inline IDataWrapper* getBasePtr(DataVariant& var) {
    return std::visit([](auto& derived) -> IDataWrapper* {
        return &derived;  // 返回指向具体派生类的基类指针
    }, var);
}
