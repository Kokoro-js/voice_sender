#include <mpg123.h>
#include "CustomIO.hpp"
#include "../../utils/AudioTypes.h"

// 自定义读取函数
mpg123_ssize_t CustomIO::iobuf_mpg123_read(void *handle, void *buffer, size_t size) {
    auto *src_buffer = static_cast<IOBufWarp *>(handle);
    size_t total_copied = 0;

    while (total_copied < size) {
        size_t current_size = src_buffer->size();

        if (src_buffer->pos_ >= current_size) {
            if (src_buffer->is_eof) {
                // 缓冲区已结束，返回 0 表示 EOF
                VLOG(2) << "No more data to read. total_pos_: " << src_buffer->pos_
                        << ", buffer size: " << current_size;
                break;
            } else {
                // 缓冲区可能还会增长，暂时没有更多数据可读，返回 MPG123_NEED_MORE
                VLOG(2) << "No more data available now, but buffer may grow.";
                return MPG123_NEED_MORE;
            }
        }

        // 更新 current_ 和 offset_
        src_buffer->updateCurrentIOBuf();

        if (!src_buffer->current_) {
            // 理论上不应该到这里，防止意外
            VLOG(1) << "No current IOBuf available.";
            break;
        }

        size_t buf_available = src_buffer->current_->length() - src_buffer->offset_;
        size_t to_copy = std::min(size - total_copied, buf_available);

        VLOG(2) << "Reading " << to_copy << " bytes. total_copied: " << total_copied
                << ", total_pos_: " << src_buffer->pos_
                << ", buf_available: " << buf_available
                << ", requested size: " << size;

        memcpy(static_cast<char *>(buffer) + total_copied,
               src_buffer->current_->data() + src_buffer->offset_,
               to_copy);

        total_copied += to_copy;
        src_buffer->pos_ += to_copy;
        src_buffer->offset_ += to_copy;

        if (src_buffer->offset_ >= src_buffer->current_->length()) {
            // 移动到下一个 IOBuf
            src_buffer->current_ = src_buffer->current_->next();
            src_buffer->offset_ = 0;
        }
    }

    VLOG(2) << "Read total_copied: " << total_copied;

    // 返回读取的字节数；如果为 0，表示 EOF
    return total_copied > 0 ? static_cast<mpg123_ssize_t>(total_copied) : 0;
}

off_t CustomIO::iobuf_mpg123_lseek(void *handle, off_t offset, int whence) {
    auto *src_buffer = static_cast<IOBufWarp *>(handle);
    off_t new_pos = 0;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = src_buffer->pos_ + offset;
            break;
        case SEEK_END: {
            size_t current_size = src_buffer->size();
            new_pos = current_size + offset;
            break;
        }
        default:
            LOG(ERROR) << "Invalid 'whence': " << whence;
            return -1;
    }

    if (new_pos < 0 || static_cast<size_t>(new_pos) > src_buffer->size()) {
        LOG(ERROR) << "Seek out of range. Position: " << new_pos;
        return -1;
    }

    src_buffer->pos_ = static_cast<size_t>(new_pos);
    // 更新 current_ 和 offset_
    src_buffer->updateCurrentIOBuf();

    VLOG(2) << "Seeking to position: " << new_pos;
    return new_pos;
}
