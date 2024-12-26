#include <mpg123.h>

#ifndef CUSTOMIO_HPP
#define CUSTOMIO_HPP

namespace CustomIO {
    mpg123_ssize_t custom_mpg123_read(void *handle, void *buffer, size_t size);
    off_t custom_mpg123_lseek(void *handle, off_t offset, int whence);
    void custom_mpg123_cleanup(void *handle);

    mpg123_ssize_t iobuf_mpg123_read(void *handle, void *buffer, size_t size);
    off_t iobuf_mpg123_lseek(void *handle, off_t offset, int whence);
    void iobuf_mpg123_cleanup(void *handle);

    int custom_read(void *opaque, uint8_t *buf, int buf_size);
    int64_t custom_seek(void *opaque, int64_t offset, int whence);
}


#endif
