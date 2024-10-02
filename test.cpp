#include <mpg123.h>
#include <iostream>
#include <vector>
#include <cstdint>

int main() {
    // 初始化 mpg123
    mpg123_init();
    mpg123_handle *mh = mpg123_new(nullptr, nullptr);

    // 打开 MP3 文件
    const char* mp3_filename = "output.mp3";  // 替换为你的 MP3 文件路径
    if (mpg123_open(mh, mp3_filename) != MPG123_OK) {
        std::cerr << "Failed to open MP3 file!" << std::endl;
        return -1;
    }

    // 获取音频格式信息
    long rate;
    int channels, encoding;
    mpg123_getformat(mh, &rate, &channels, &encoding);

    std::cout << rate << ' ' << channels << ' ' << encoding;
    // 创建一个缓冲区来存储解码后的 PCM 数据
    std::vector<int16_t> pcm_data;

    // 定义缓冲区用于存储解码数据
    size_t buffer_size = mpg123_outblock(mh);
    unsigned char* buffer = new unsigned char[buffer_size];
    size_t done = 0;
    int err = MPG123_OK;

    // 持续读取和解码 MP3 流
    while ((err = mpg123_read(mh, buffer, buffer_size, &done)) == MPG123_OK) {
        // 将解码后的 PCM 数据添加到缓冲区
        int16_t* pcm_buffer = reinterpret_cast<int16_t*>(buffer);
        size_t sample_count = done / sizeof(int16_t);
        pcm_data.insert(pcm_data.end(), pcm_buffer, pcm_buffer + sample_count);
    }

    if (err != MPG123_DONE) {
        std::cerr << "Decoding error: " << mpg123_strerror(mh) << std::endl;
    }

    // 计算样本数
    size_t total_samples = pcm_data.size() / channels;

    // 输出结果
    std::cout << "Decoded PCM sample count: " << total_samples << std::endl;

    // 获取 MP3 文件的总样本数
    off_t mp3_samples = mpg123_length(mh);
    std::cout << "MP3 file reported sample count: " << mp3_samples << std::endl;

    // 使用 int64_t 计算差异，防止溢出
    int64_t sample_difference = static_cast<int64_t>(total_samples) - static_cast<int64_t>(mp3_samples);
    std::cout << "Sample counts difference: " << sample_difference << std::endl;

    if (sample_difference == 0) {
        std::cout << "Sample counts match!" << std::endl;
    } else {
        std::cout << "Sample counts do not match!" << std::endl;
    }

    // 清理
    delete[] buffer;
    mpg123_close(mh);
    mpg123_delete(mh);
    mpg123_exit();

    return 0;
}
