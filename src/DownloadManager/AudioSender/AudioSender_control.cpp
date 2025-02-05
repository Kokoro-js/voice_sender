#include "AudioSender.h"

constexpr const char *mp3_format = "mp3";
constexpr const char *mov_format = "mov,mp4,m4a,3gp,3g2,mj2";

const AVInputFormat *detect_format(std::vector<char> &audio_data) {
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
    const AVInputFormat *fmt = av_probe_input_format(&probeData, 1);

    // 检查返回的格式是否为空
    if (!fmt) {
        // 如果无法识别格式，输出错误日志
        LOG(ERROR) << "无法识别格式";
    }

    // 释放 av_mallocz 分配的内存
    av_free(probeData.buf);

    return fmt;
}

#include "../../api/EventPublisher.h"

// 第一个参数是指针的指针
coro::task<void> AudioSender::start_producer(const std::shared_ptr<ExtendedTaskItem> *ptr, const bool &isStopped) {
    std::vector<char> audio_data;
    audio_data.reserve(4096);

    while (true) {
        if (isStopped) {
            VLOG(1) << "控制任务已退出。";
            co_return;
        }

        if (task != nullptr) {
            VLOG(2) << task.use_count();
        }

        const auto &download_task = *ptr; // & 只是引用，不增加引用计数

        // 如果远端指针为空或等于我们目前类内存储的智能指针，则跳过
        if (!download_task || download_task == task) {
            co_await EventNewDownload;
            EventNewDownload.reset();
            continue;
        }

        // 更新任务（引用计数 +1）
        task = download_task;

        auto current_task = task.get();
        auto data = &current_task->data;
        if (auto *fixed_buffer = std::get_if<FixedCapacityBuffer>(data)) {
            data_wrapper = BufferWarp(fixed_buffer);
        } else if (auto *iobuf = std::get_if<folly::IOBufQueue>(data)) {
            data_wrapper = IOBufWarp(iobuf);
        }

        auto data_interface = getBasePtr(data_wrapper);

        if (audio_props.detectedFormat == nullptr) {
            while (current_task->state == AudioCurrentState::Downloading && data_interface->size() < 4096) {
                // 不再允许 Curl 通知数据入列，在别处触发事件会导致逻辑跑到那个线程上，使用 yield 排队。
                co_await tp_->yield();
            }

            data_interface->readFront(audio_data, 4096);
            audio_props.detectedFormat = detect_format(audio_data);

            if (audio_props.detectedFormat == nullptr) {
                LOG(ERROR) << "未知格式！任务" << current_task->item.name << "(" << current_task->item.url << ")";
                current_task->set_read_error(ReaderErrorCode::InvalidFormat, "未知格式");
                continue;
            }

            if (strcmp(audio_props.detectedFormat->name, mp3_format) == 0) {
                using_decoder = &mpg123_decoder;
                LOG(INFO) << "格式" << audio_props.detectedFormat->name;
            } else if (strcmp(audio_props.detectedFormat->name, mov_format) == 0) {
                // 必须等下载完才能正确解析该类型格式。
                co_await current_task->EventDownloadFinished;
                using_decoder = &ffmpeg_decoder;
            } else {
                LOG(INFO) << "格式" << audio_props.detectedFormat->name;
                using_decoder = &ffmpeg_decoder;
            }
            using_decoder->setup();
        }

        audio_data.clear();

        // 数据量不足时不要开始解码！基础保险。
        while (current_task->state == AudioCurrentState::Downloading && data_interface->size() < 16384 * 30) {
            co_await tp_->yield();
        }

        {
            // co_await current_task->mutex_data.lock();
            int err_count = 0;
            int max_err_count = 3;
            while (!audio_props.info_found) {
                if (err_count > max_err_count - 1) {
                    co_await current_task->EventDownloadFinished;
                }
                auto info = using_decoder->getAudioFormat();
                if (info.channels == 0) {
                    LOG(ERROR) << "找不到音频信息" << current_task->item.name;
                    err_count++;
                    if (err_count > max_err_count) {
                        break;
                    } else {
                        continue;
                    }
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
        }

        // 此处标志正式开始解码
        EventFeedDecoder.set();
        EventPublisher::getInstance().handle_event_publish(stream_id_, false);

        if (current_task->state < AudioCurrentState::DownloadAndWriteFinished) {
            while (current_task->item.use_stream) {
                co_await scheduler_->yield_for(std::chrono::milliseconds(2000));
                if (current_task->EventDownloadFinished.is_set()) {
                    break;
                }
                EventFeedDecoder.set();
            }
            co_await current_task->EventDownloadFinished;
            EventFeedDecoder.set();
        }

        // 此处表明下载写入完成，is_eof 控制着 io，只有 is_eof 为 true 时解码侧才有可能 EventReadFinshed.set()
        data_interface->is_eof = true;
        audio_props.total_samples = using_decoder->getTotalSamples();
        EventPublisher::getInstance().handle_event_publish(stream_id_, false);

        VLOG(1) << "等待" << current_task->item.name;
        co_await EventReadFinshed;
        EventReadFinshed.reset();
        VLOG(1) << "等待读取完成" << current_task->item.name;

        // 此处表明读取完成
        current_task->EventReadFinished.set();
        current_task->state = AudioCurrentState::DrainFinished;
        audio_props.reset();
        using_decoder->reset();
    }
}

// 该函数负责单个 Control 周期顺利通过，外部无需担心 current_task 的设置问题。
bool AudioSender::doSkip() {
    if (task == nullptr) {
        return false;
    }

    // 只需要保证 producer 畅通无阻的运行到末尾，不要在这里设置 startNextDownload。
    auto current_task = task.get();
    LOG(INFO) << "跳过被调用于任务" << current_task->item.name;
    current_task->state = AudioCurrentState::DownloadAndWriteFinished;
    EventReadFinshed.set();
    EventFeedDecoder.reset();
    return true;
}

void AudioSender::clean_up() {
    EventReadFinshed.set();
    EventNewDownload.set();
    EventFeedDecoder.set();
    audio_props.play_state = PLAYING;
    EventStateUpdate.set();
    rb.notify_waiters();
}

bool AudioSender::switchPlayState(::PlayState state) {
    if (task == nullptr) {
        return false;
    }

    audio_props.play_state = state;
    EventStateUpdate.set();
    return true;
}

bool AudioSender::setVolume(float volume) {
    if (task == nullptr) {
        return false;
    }

    // 四舍五入到两位小数
    audio_props.volume = std::round(volume * 100.0f) / 100.0f;
    return true;
}

bool AudioSender::seekSecond(int seconds) {
    if (task == nullptr) {
        return false;
    }

    using_decoder->seek(seconds);
    audio_props.current_samples = using_decoder->getCurrentSamples();
    audio_props.do_empty_ring_buffer = true;
    return true;
}
