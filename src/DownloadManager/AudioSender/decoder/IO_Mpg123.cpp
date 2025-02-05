#include <mpg123.h>
#include <plog/Log.h>
#include "CustomIO.hpp"

#include "../../utils/AudioTypes.h"

// 自定义读取函数
mpg123_ssize_t CustomIO::custom_mpg123_read(void *handle, void *buffer, size_t size) {
    auto *src_buffer = static_cast<BufferWarp *>(handle);
    if (src_buffer->pos_ + size > src_buffer->size()) {
        size = src_buffer->size() - src_buffer->pos_; // 防止读取超出范围
    }

    memcpy(buffer, src_buffer->buffer->data() + src_buffer->pos_, size);
    src_buffer->pos_ += size;

    return size;
}

// 自定义寻址函数
off_t CustomIO::custom_mpg123_lseek(void *handle, off_t offset, int whence) {
    auto *src_buffer = static_cast<BufferWarp *>(handle);
    off_t new_pos = 0;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = src_buffer->pos_ + offset;
            break;
        case SEEK_END:
            new_pos = src_buffer->size() + offset;
            break;
        default:
            LOG(ERROR) << "Invalid 'whence': " << whence;
            return MPG123_ERR;
    }

    if (new_pos < 0 || static_cast<size_t>(new_pos) > src_buffer->size()) {
        LOG(ERROR) << "Seek out of range. Position: " << new_pos;
        return MPG123_ERR;
    }

    src_buffer->pos_ = static_cast<size_t>(new_pos);

    VLOG(2) << "Custom lseek called: offset = " << offset
            << ", whence = " << whence
            << ", new position = " << new_pos
            << ", buffer size = " << src_buffer->size();

    return new_pos;
}

// 自定义清理函数
void CustomIO::custom_mpg123_cleanup(void *handle) {
    // 清理操作，如有需要
}