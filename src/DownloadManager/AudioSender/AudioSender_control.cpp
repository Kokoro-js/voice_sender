#include "AudioSender.h"

constexpr const char *mp3_format = "mp3";

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

coro::task<void> AudioSender::start_producer(const std::shared_ptr<ExtendedTaskItem> *ptr, const bool &isStopped) {
    std::vector<char> audio_data;
    audio_data.reserve(4096);

    while (!isStopped) {
        if (task != nullptr) {
            VLOG(2) << task.use_count();
        }

        if (*ptr == nullptr || *ptr == task) {
            co_await EventNewDownload;
            EventNewDownload.reset();
            continue;
        }

        // 解引用，此处引用计数 +1
        task = (*ptr);
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
                // 对于无法读取的格式我们等待下一首到达后
                co_await current_task->EventDownloadFinished;
                continue;
            }

            if (strcmp(audio_props.detectedFormat->name, mp3_format) == 0) {
                using_decoder = &mpg123_decoder;
                LOG(INFO) << "格式" << audio_props.detectedFormat->name;
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
            co_await mutex_buffer.lock();

            while (!audio_props.info_found) {
                auto info = using_decoder->getAudioFormat();;
                if (info.channels == 0) {
                    LOG(ERROR) << "找不到音频信息" << current_task->item.name;
                    co_await tp_->yield();
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
        }

        // 此处标志正式开始解码e
        EventFeedDecoder.set();
        EventPublisher::getInstance().handle_event_publish(stream_id_, false);

        if (current_task->state < AudioCurrentState::DownloadAndWriteFinished) {
            co_await current_task->EventDownloadFinished;
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

    VLOG(1) << "控制任务已退出。";
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

bool AudioSender::switchPlayState(::PlayState state) {
    if (task == nullptr) {
        return false;
    }

    audio_props.play_state = state;
    EventStateUpdate.set();
    return true;
}

