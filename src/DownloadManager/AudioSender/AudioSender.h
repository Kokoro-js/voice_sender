// AudioSender.h
#pragma once

#ifndef AUDIOSENDER_H
#define AUDIOSENDER_H

// #include <opusenc.h>
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
#include <vector>
#include <random>
#include "../../RTPManager/RTPInstance.h" // 更新相对路径
#include "../utils/ExtendedTaskItem.h" // 包含 ExtendedTaskItem
#include "AudioAlignedAlloc.h"

// Forward declarations
class ExtendedTaskItem;
// class AudioDataBuffer;

enum PlayState {
    PLAYING,
    PAUSE
};

struct AudioProps {
    const AVInputFormat *detectedFormat = nullptr;

    PlayState play_state = PLAYING;
    bool info_found = false;
    long rate = 44100;
    int channels = 2;
    int encoding = MPG123_ENC_SIGNED_16;
    int bytes_per_sample = 2;
    int bits_per_samples = 16;

    int current_samples = 0;
    int total_samples = 0;
    // 音量因子，1.0 表示原始音量，0.5 表示减半音量，2.0 表示加倍音量
    float volume = 1.0f;

    bool do_empty_ring_buffer = false;

    void reset() {
        info_found = false;
        detectedFormat = nullptr;
        current_samples = 0;
        total_samples = 0;
    }
};

struct OpusTempBuffer {
    std::vector<int16_t> temp_buffer;
    size_t temp_samples = 0;

    explicit OpusTempBuffer(size_t temp_buffer_length) : temp_buffer(temp_buffer_length, 0) {}
};

class AudioSender {
public:
    AudioSender(std::string stream_id, std::shared_ptr<RTPInstance> rtp_instance, std::shared_ptr<coro::thread_pool> tp,
                std::shared_ptr<coro::io_scheduler> scheduler);

    ~AudioSender();

    [[nodiscard]] bool is_initialized() const;

    std::string stream_id_;

    coro::task<void> start_producer(const std::shared_ptr<ExtendedTaskItem> *ptr, const bool &isStopped);

    coro::task<void> start_consumer(const bool &isStopped);

    coro::task<void> start_sender(const bool &isStopped);

    bool doSkip();

    void clean_up();

    bool switchPlayState(PlayState state);

    bool setVolume(float volume);

    bool seekSecond(int seconds);

    coro::event EventNewDownload;
    coro::event EventReadFinshed;
    coro::event EventFeedDecoder;
    coro::event EventStateUpdate;

    std::shared_ptr<ExtendedTaskItem> task;
    DataVariant data_wrapper = BufferWarp();

    AudioDecoder *using_decoder = nullptr;
    Mpg123Decoder mpg123_decoder;
    FfmpegDecoder ffmpeg_decoder;

    AudioProps audio_props;

    int setOpusBitRate(const int &kbps);

private:
    std::shared_ptr<RTPInstance> rtp_instance_;

    bool initialized_ = false;
    OpusEncoder *opus_encoder_ = nullptr;

    std::shared_ptr<coro::thread_pool> tp_;
    std::shared_ptr<coro::io_scheduler> scheduler_;
    coro::ring_buffer<std::vector<uint8_t>, 25> rb;

    static constexpr int TARGET_SAMPLE_RATE = 48000;
    static constexpr int OPUS_DELAY = 40;
    static constexpr int OPUS_FRAMESIZE = static_cast<int>(TARGET_SAMPLE_RATE * OPUS_DELAY * 0.001);

/*    OggOpusEnc *enc{};
    OggOpusComments *comments{};*/
    int err{};

/*    void initialize_opus_file();

    void finalize_opus_file();*/

    coro::task<int> encode_opus_frame(OpusEncoder *encoder, int16_t *pcm_data, size_t total_samples,
                                      OpusTempBuffer &temp_buffer, int max_data_bytes);

    static constexpr int MAX_DECODE_SIZE = 73728;
    static constexpr int MAX_PCM_SIZE = 131072;
    static constexpr int MAX_SAMPLES_COUNT = MAX_PCM_SIZE / sizeof(int16_t);
    AlignedMem::AlignedUniquePtr<unsigned char> read_output_buffer_;
    AlignedMem::AlignedUniquePtr<float> float_buffer_;
    AlignedMem::AlignedUniquePtr<int16_t> resampled_buffer_;

    bool resample_audio(int &totalSamples, int channelCount, long rate, float volume);

    bool
    process_audio_frame(int &totalSamples, int channelCount, bool need_resample, bool applyVolume, const void *raw_data,
                        int encoding, int16_t *&pcm_data);
};

#endif // AUDIOSENDER_H
