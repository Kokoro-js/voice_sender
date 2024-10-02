#include "AudioSender.h"
#include <chrono>
#include <algorithm>
#include <vector>
#include <queue>

// 定义类型简化
using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

coro::task<void> AudioSender::start_sender(const bool &isStopped) {
    co_await scheduler_->schedule();
    co_await scheduler_->yield_for(std::chrono::milliseconds{1000});

    auto rtpInstance = rtp_instance_.get();
    auto &main_stream = rtpInstance->main_stream_;
    auto &timestamp = rtpInstance->main_stream_timestamp_;

    constexpr int OPUS_DELAY_MS = OPUS_DELAY; // 每帧延迟（毫秒）
    constexpr int OPUS_RTP_SAMPLE_RATE = 48000; // RTP 时间戳单位
    constexpr int OPUS_FRAMESIZE = OPUS_RTP_SAMPLE_RATE * OPUS_DELAY_MS / 1000; // 每帧时间戳增加量

    constexpr int MAX_ADVANCE_FRAMES = 4; // 最大提前发送帧数（例如，10帧 = 200ms）
    constexpr int MIN_ADVANCE_FRAMES = 2;  // 最小提前发送帧数（例如，2帧 = 100ms）
    constexpr int ADJUSTMENT_STEP_FRAMES = 1; // 每次调整步长（帧）
    constexpr int MOVING_AVERAGE_SIZE = 5;

    // 初始提前发送帧数
    int current_advance_frames = MIN_ADVANCE_FRAMES;

    // 记录发送开始时间
    TimePoint start_time = Clock::now();
    int frame_index = 0;

    // 移动平均变量
    std::queue<int> send_durations;
    int total_send_duration_us = 0;

    while (!isStopped) {
        // 计算下一批帧的发送时间，考虑当前的提前发送帧数
        TimePoint next_send_time = start_time +
                                   std::chrono::milliseconds(OPUS_DELAY_MS * frame_index) -
                                   std::chrono::milliseconds(OPUS_DELAY_MS * current_advance_frames);

        TimePoint now = Clock::now();

        if (now < next_send_time) {
            co_await scheduler_->yield_until(next_send_time);
        } else {
            // 如果已经超过了发送时间，记录警告
            auto delay = std::chrono::duration_cast<std::chrono::milliseconds>(now - next_send_time).count();
            LOGW << "Batch starting at frame " << frame_index << " is late by " << delay << "ms";
        }

        // 批量发送帧
        std::vector<std::vector<unsigned char>> frames_to_send;
        for(int i = 0; i < current_advance_frames && !isStopped; ++i) {
            auto expected = co_await rb.consume();
            if (!expected) {
                LOGE << "消费者关闭。";
                co_return;
            }
            frames_to_send.emplace_back(std::move(*expected));
        }

        auto batch_send_start = Clock::now();

        for(auto &frame : frames_to_send) {
            int result = main_stream->push_frame(frame.data(), frame.size(), timestamp, RTP_NO_FLAGS);
            if (result != RTP_OK) {
                LOGE << "发送遇到错误";
                continue;
            }
            timestamp += OPUS_FRAMESIZE;
            frame_index += 1;
        }

        auto batch_send_end = Clock::now();
        auto batch_send_duration_us = std::chrono::duration_cast<std::chrono::microseconds>(batch_send_end - batch_send_start).count();

        // 更新移动平均
        send_durations.push(batch_send_duration_us);
        total_send_duration_us += batch_send_duration_us;
        if (send_durations.size() > MOVING_AVERAGE_SIZE) {
            total_send_duration_us -= send_durations.front();
            send_durations.pop();
        }
        double average_send_duration_us = static_cast<double>(total_send_duration_us) / send_durations.size();

        // 使用移动平均进行调整
        if (average_send_duration_us > (OPUS_DELAY_MS * 1000 * current_advance_frames)) {
            // 如果发送耗时超过预期，减少提前发送帧数
            current_advance_frames = std::max(current_advance_frames - ADJUSTMENT_STEP_FRAMES, MIN_ADVANCE_FRAMES);
            LOGI << "减少提前发送帧数到 " << current_advance_frames << "帧";
        } else if (average_send_duration_us < ((OPUS_DELAY_MS * (current_advance_frames - ADJUSTMENT_STEP_FRAMES)) * 1000)) {
            // 如果发送耗时小于调整后的提前发送帧数，增加提前发送帧数
            current_advance_frames = std::min(current_advance_frames + ADJUSTMENT_STEP_FRAMES, MAX_ADVANCE_FRAMES);
            LOGI << "增加提前发送帧数到 " << current_advance_frames << "帧";
        }

        // 确保提前发送帧数在合理范围内
        current_advance_frames = std::clamp(current_advance_frames, MIN_ADVANCE_FRAMES, MAX_ADVANCE_FRAMES);

        // 记录详细的发送信息
        LOGD << "Batch starting at frame " << frame_index - current_advance_frames
             << " sent with advance " << (current_advance_frames * OPUS_DELAY_MS)
             << "ms and average send duration " << average_send_duration_us << "us";
    }
    co_return;
}
