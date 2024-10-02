#include <mpg123.h>
#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdlib>
#include <vector>

// 自定义 buffer 结构
struct BufferData {
    const unsigned char *buffer;
    size_t buffer_size;
    size_t current_pos;
};

// 自定义读取函数
mpg123_ssize_t custom_read(void *handle, void *buffer, size_t size) {
    auto *data = (BufferData *)handle;
    if (data->current_pos + size > data->buffer_size) {
        size = data->buffer_size - data->current_pos; // 防止读取超出范围
    }

    memcpy(buffer, data->buffer + data->current_pos, size);
    data->current_pos += size;

    return size;
}

// 自定义寻址函数
off_t custom_lseek(void *handle, off_t offset, int whence) {
    BufferData *data = (BufferData *)handle;
    off_t new_pos = 0;

    switch (whence) {
        case SEEK_SET:
            new_pos = offset;
            break;
        case SEEK_CUR:
            new_pos = data->current_pos + offset;
            break;
        case SEEK_END:
            new_pos = data->buffer_size + offset;
            break;
        default:
            return -1; // 无效的 whence
    }

    if (new_pos < 0 || new_pos > data->buffer_size) {
        return -1; // 超出范围
    }

    data->current_pos = new_pos;
    return new_pos;
}

// 自定义清理函数
void custom_cleanup(void *handle) {
    // 清理操作，如有需要
}

// 读取文件到 buffer
unsigned char* read_file_to_buffer(const char* filename, size_t &file_size) {
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Failed to open MP3 file!" << std::endl;
        return nullptr;
    }

    file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    unsigned char *buffer = (unsigned char*)malloc(file_size);
    if (!buffer) {
        std::cerr << "Failed to allocate memory!" << std::endl;
        return nullptr;
    }

    file.read((char*)buffer, file_size);
    file.close();
    return buffer;
}

// 读取 mpg123 数据到 std::vector 中
void read_mpg123_output_to_vector(mpg123_handle *mh, std::vector<unsigned char> &output) {
    unsigned char *audio = new unsigned char[1024]; // 分配音频缓冲区
    size_t bytes;
    int err;
    do {
        err = mpg123_read(mh, audio, 1024, &bytes);
        if (err == MPG123_OK) {
            output.insert(output.end(), audio, audio + bytes); // 将读取的数据添加到输出向量
        } else if (err != MPG123_DONE && err != MPG123_NEED_MORE) {
            std::cerr << "Error reading MP3 file: " << mpg123_strerror(mh) << " (code " << err << ")" << std::endl;
        }
    } while (err != MPG123_DONE);

    delete[] audio; // 释放缓冲区
}

// 比较两个 vector 中的数据
bool compare_buffers(const std::vector<unsigned char> &vec1, const std::vector<unsigned char> &vec2) {
    if (vec1.size() == 0 || vec2.size() == 0) {
        std::cerr << "One or both buffers are empty!" << std::endl;
        return false;
    }

    if (vec1.size() != vec2.size()) {
        std::cerr << "Buffers are of different sizes!" << std::endl;
        return false;
    }

    for (size_t i = 0; i < vec1.size(); ++i) {
        if (vec1[i] != vec2[i]) {
            std::cerr << "Buffers differ at byte " << i << std::endl;
            return false;
        }
    }

    return true;
}

int main() {
    const char* mp3_filename = "500-KB-MP3.mp3";

    // 初始化 mpg123
    mpg123_init();

    // 创建第一个句柄用于传统文件读取
    mpg123_handle *mh1 = mpg123_new(NULL, NULL);
    if (!mh1) {
        std::cerr << "Failed to create mpg123 handle for file reading!" << std::endl;
        return -1;
    }

    // 使用传统方法读取 MP3 文件
    if (mpg123_open(mh1, mp3_filename) != MPG123_OK) {
        std::cerr << "Failed to open MP3 file!" << std::endl;
        mpg123_delete(mh1);
        mpg123_exit();
        return -1;
    }

    std::vector<unsigned char> traditional_output;
    read_mpg123_output_to_vector(mh1, traditional_output);
    mpg123_close(mh1);
    mpg123_delete(mh1);

    // 将文件读取到内存
    size_t file_size;
    unsigned char *buffer = read_file_to_buffer(mp3_filename, file_size);
    if (!buffer) {
        mpg123_exit();
        return -1;
    }

    // 创建第二个句柄用于自定义内存读取
    mpg123_handle *mh2 = mpg123_new(NULL, NULL);
    if (!mh2) {
        std::cerr << "Failed to create mpg123 handle for memory buffer reading!" << std::endl;
        free(buffer);
        mpg123_exit();
        return -1;
    }

    // 设置自定义读取器
    BufferData data = { buffer, file_size, 0 };
    mpg123_replace_reader_handle(mh2, custom_read, custom_lseek, custom_cleanup);
    if (mpg123_open_handle(mh2, &data) != MPG123_OK) {
        std::cerr << "Failed to open MP3 from memory buffer!" << std::endl;
        free(buffer);
        mpg123_delete(mh2);
        mpg123_exit();
        return -1;
    }

    std::vector<unsigned char> custom_output;
    read_mpg123_output_to_vector(mh2, custom_output);

    // 比较两个输出
    if (compare_buffers(traditional_output, custom_output)) {
        std::cout << "Buffers are identical!" << std::endl;
    } else {
        std::cout << "Buffers are different!" << std::endl;
    }

    // 清理
    free(buffer);
    mpg123_close(mh2);
    mpg123_delete(mh2);
    mpg123_exit();

    return 0;
}
