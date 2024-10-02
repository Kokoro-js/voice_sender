#ifndef AUDIOSENDER_H
#define AUDIOSENDER_H
#include <opusenc.h>

#include "decoder/AudioDecoder.h"
#include "decoder/AudioDecoder_Mpg123.h"
#include "decoder/AudioDecoder_FFmpeg.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
}

#include <mpg123.h>

#include <string>
#include <coro/coro.hpp>
#include <uvgrtp/lib.hh>
#include <opus.h>
#include <readerwriterqueue.h>
#include <vector>
#include <random>
#include <plog/Log.h>

#include "../TaskManager.h"
#include "../../RTPManager/RTPInstance.h"


enum class AudioCurrentState {
    Downloading,
    DownloadFinished,
    WriteFinished,
    drainFinished,
};

struct AudioProps : public TaskItem {
    explicit AudioProps() {
        name = "";
        url = "";
        type = File;
    };
    explicit AudioProps(const TaskItem& task_item) : TaskItem(task_item) {}
    void setTaskItem(const TaskItem& task_item) {
        name = task_item.name;
        url = task_item.url;
        type = task_item.type;
    }

    const AVInputFormat* detectedFormat = nullptr;

    bool info_found = false;
    long rate = 44100;
    int channels = 2;
    int encoding = MPG123_ENC_SIGNED_16;
    int bytes_per_sample = 2;
    int bits_per_samples = 16;

    AudioCurrentState state = AudioCurrentState::Downloading;
    int current_samples = 0; // 当前解码到的样本量
    int total_samples = 0; // 当前任务总样本量
    size_t total_bytes = 0; // 该值只在文件真正下载完成时被设置。
    bool doDownloadNext = false; // 用于指示是否已开启下个任务的下载。
    bool doSkip = false; // 用于跳过当前任务。
};

struct OpusTempBuffer {
    std::vector<int16_t> temp_buffer;
    size_t temp_samples = 0;

    explicit OpusTempBuffer(size_t temp_buffer_length) : temp_buffer(temp_buffer_length, 0) {
    } // 双声道初始化
};

class AudioSender {
public:
    AudioSender(std::string stream_id, std::shared_ptr<RTPInstance> rtp_instance, std::shared_ptr<coro::thread_pool> tp,
                std::shared_ptr<coro::io_scheduler> scheduler);

    ~AudioSender();

    [[nodiscard]] bool is_initialized() const;

    coro::task<void> start_producer(moodycamel::ReaderWriterQueue<std::vector<char> > &download_chunk_queues_,
                                    coro::event &dataEnqueue, const bool &isStopped);

    coro::task<void> start_consumer(const bool &isStopped);

    coro::task<void> start_sender(const bool &isStopped);

    // 该事件应该只在流需要开始下次下载周期时触发。
    coro::event startDownloadNextEvent;
    // 该事件应该只在流需要开始下次写入周期时触发。
    coro::event startWriteNextEvent;

    coro::event mp3FeedEvent;

    AudioDataBuffer data_buffer;
    coro::mutex mutex_buffer;

    AudioDecoder* using_decoder = nullptr;
    Mpg123Decoder mpg123_decoder;
    FfmpegDecoder ffmpeg_decoder;

    AudioProps audio_props;

    int setOpusBitRate(const int &kbps);

private:
    std::string stream_id_;
    std::shared_ptr<RTPInstance> rtp_instance_;

    bool initialized_;
    OpusEncoder *opus_encoder_;

    std::shared_ptr<coro::thread_pool> tp_;
    std::shared_ptr<coro::io_scheduler> scheduler_;
    coro::ring_buffer<std::vector<uint8_t>, 25> rb;

    static constexpr int DEFAULT_DATA_BUFFER_SIZE = 16 * 1024 * 1024;
    static constexpr int TARGET_SAMPLE_RATE = 48000;
    static constexpr int OPUS_DELAY = 40;
    static constexpr int OPUS_FRAMESIZE = TARGET_SAMPLE_RATE * OPUS_DELAY * 0.001;

    OggOpusEnc *enc{};
    OggOpusComments *comments{};
    int err{};

    void initialize_opus_file();

    void finalize_opus_file();

    void cleanupCurrentAudio();

    coro::task<int> encode_opus_frame(OpusEncoder *encoder, int16_t *pcm_data, size_t total_samples,
                                      OpusTempBuffer &temp_buffer, unsigned char *output_buffer, int max_data_bytes);
};

#endif // AUDIOSENDER_H
