#include "AudioSender.h"

constexpr const char* mp3_format = "mp3";
const AVInputFormat* detect_format(std::vector<char>& audio_data) {
    AVProbeData probeData = {};

    // 如果数据长度小于 4096 则使用实际大小，否则只探测前 4096 字节
    probeData.buf_size = audio_data.size() < 4096 ? audio_data.size() : 4096;

    // 设置虚拟文件名为 "stream"（可为空，但加上增强可读性）
    probeData.filename = "stream";

    // FFmpeg 要求缓冲区必须有 AVPROBE_PADDING_SIZE 的零填充，以防止溢出读取
    // 使用 av_mallocz 分配内存，自动分配 buf_size + AVPROBE_PADDING_SIZE 大小并填充 0
    probeData.buf = static_cast<unsigned char *>(av_mallocz(probeData.buf_size + AVPROBE_PADDING_SIZE));

    // 将音频数据拷贝到新分配的缓冲区中，注意仅拷贝实际大小的数据部分
    memcpy(probeData.buf, audio_data.data(), probeData.buf_size);

    // 调用 FFmpeg 进行格式探测，返回可能的格式
    const AVInputFormat* fmt = av_probe_input_format(&probeData, 1);

    // 检查返回的格式是否为空
    if (!fmt) {
        // 如果无法识别格式，输出错误日志
        LOGE << "无法识别格式";
    }

    // 释放 av_mallocz 分配的内存
    av_free(probeData.buf);

    return fmt;
}

// 最好搭配 { co_await mutex }
void AudioSender::cleanupCurrentAudio() {
    mp3FeedEvent.reset();
    audio_props.detectedFormat = nullptr;
    data_buffer.clear();
    using_decoder->reset();

    audio_props.info_found = false;
    audio_props.current_samples = 0;
    audio_props.total_samples = 0;
    audio_props.total_bytes = 0;
    audio_props.doDownloadNext = false;
}

coro::task<void> AudioSender::start_producer(moodycamel::ReaderWriterQueue<std::vector<char> > &download_chunk_queues_,
                                             coro::event &dataEnqueue, const bool &isStopped) {
    std::vector<char> audio_data;
    audio_data.reserve(16384);
    int retries = 0;

    while (!isStopped) {
        if (audio_props.doSkip) {
            audio_props.doSkip = false;

            // 在没写完前都不会触发下一个任务的预载，放心排空
            if (audio_props.state < AudioCurrentState::WriteFinished) {
                while (download_chunk_queues_.try_dequeue(audio_data)) {}
                audio_data.resize(0);
            }

            {
                co_await mutex_buffer.lock();
                cleanupCurrentAudio();
            }

            startDownloadNextEvent.set();
        }

        if (!download_chunk_queues_.try_dequeue(audio_data)) {
            LOGI << "No data available, waiting...";
            // 必须有，try_dequeue 频次过高会崩溃，不能放外边阻塞写入
            co_await tp_->yield();
            // 不是最佳设计，临时解决方案
            while (download_chunk_queues_.size_approx() == 0) {
                co_await scheduler_->yield_for(std::chrono::milliseconds(50));
                retries++;
                if (retries > 3) {
                    // co_await tp_->schedule();
                    dataEnqueue.reset();
                    co_await dataEnqueue;
                }
            }
            retries = 0;
            audio_data.resize(0);
            continue;
        }

        if (audio_props.detectedFormat == nullptr) {
            // using_decoder = &ffmpeg_decoder;
            audio_props.detectedFormat = detect_format(audio_data);
            if (audio_props.detectedFormat == nullptr) {
                LOGE << "未知格式！";
                continue;
            }

            if (strcmp(audio_props.detectedFormat->name, mp3_format) == 0) {
                using_decoder = &mpg123_decoder;
            } else {
                LOGI << "格式" << audio_props.detectedFormat->name;
                using_decoder = &ffmpeg_decoder;
            }
        }

        {
            auto scoped_lock = co_await mutex_buffer.lock();

            if (!data_buffer.insert(reinterpret_cast<const unsigned char *>(audio_data.data()), audio_data.size())) {
                LOG_ERROR << "Feed error at data_buffer";
                // 喂食失败只有可能是缓冲区满了，进行处理
                LOGE << "目前任务 " << audio_props.name << " 状态 " << int(audio_props.state) << " 已经写入 " << data_buffer.size() << " 准备写入" << audio_data.size();
            };


            // 数据量不足时不要开始解码！基础保险。
            if (audio_props.state != AudioCurrentState::WriteFinished && data_buffer.size() < 16384 * 30) continue;

            // 直至编码器能正常获取音频参数
            if (audio_props.info_found == false) {
                auto info = using_decoder->getAudioFormat();
                LOGD << info.channels << info.sample_rate;
                if (info.channels == 0) {
                    LOGE << "找不到音频信息";
                    continue;
                }
                audio_props.channels = info.channels;
                audio_props.rate = info.sample_rate;
                audio_props.bytes_per_sample = info.bytes_per_sample;
                audio_props.bits_per_samples = info.bits_per_samples;
                if (info.encoding != -1) {
                    audio_props.encoding = info.encoding;
                }

                audio_props.info_found = true;
            }

            mp3FeedEvent.set();
        }

        // 每次写入成功后检查是否在下载完成的基础上完全写入。
        if (audio_props.state == AudioCurrentState::DownloadFinished
            && data_buffer.size() == audio_props.total_bytes) {
            // 更新状态，此时可以安全的获取总长度
            audio_props.state = AudioCurrentState::WriteFinished;
            audio_props.total_samples = using_decoder->getTotalSamples();

            // startDownloadNextEvent.set();

            // 携程等待读取完成时触发写入下个任务。
            LOGD << "等待" << audio_props.name;
            co_await startWriteNextEvent;
            startWriteNextEvent.reset();
            LOGD << "等待读取完成" << audio_props.name;
            // 清理任务涉及对 data_buffer_ 操作，但 clear 一般只是更改 size_。这里只是在 drainFinish 达成触发事件后继续响应逻辑。
            {
                co_await mutex_buffer.lock();
                cleanupCurrentAudio();
            }
        }
    }
}
