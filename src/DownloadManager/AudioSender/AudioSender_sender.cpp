#include "AudioSender.h"
#include <chrono>
#include <algorithm>
#include <vector>
#include <array>
#include <optional>
#include <glog/logging.h> // 确保包含 glog 头文件

using Clock = std::chrono::steady_clock;
using TimePoint = std::chrono::time_point<Clock>;

/**
 * AudioSender::start_sender
 *
 * 协程函数：持续从环形缓冲区 rb 中获取音频帧并发送到 RTP 流。
 *
 * @param isStopped 标志是否停止采集/生产帧。如果为 true 且缓冲区空，则发送协程退出。
 *
 * 主要流程：
 * 1. 等待并处理暂停 (pause) 以及清空缓冲的请求。
 * 2. 根据当前需要的“提前发送帧数”（current_advance_frames），从环形缓冲中批量获取若干帧并发送。
 * 3. 如果缓冲为空，则先等待获取到至少一帧立即发送（避免 std::min(...) 为 0 的情况）。
 * 4. 使用移动平均动态调整 current_advance_frames，以控制发送速率与延迟。
 * 5. 检测到停止条件 (isStopped && 缓冲为空) 后安全退出协程。
 */
coro::task<void> AudioSender::start_sender(const bool &isStopped) {
    // 先让协程挂起一帧时间，方便初始化
    co_await scheduler_->schedule();
    co_await scheduler_->yield_for(std::chrono::milliseconds{1000});

    // RTP 相关信息
    auto rtpInstance = rtp_instance_.get();
    auto main_stream = rtpInstance->getMainStream();
    auto timestamp = rtpInstance->getMainStreamTimestamp();

    // OPUS 相关常量
    constexpr int OPUS_DELAY_MS = OPUS_DELAY;       // 每帧时长 (毫秒)
    constexpr int OPUS_DELAY_US = OPUS_DELAY_MS * 1000;
    constexpr int OPUS_RTP_FRAMESIZE = OPUS_FRAMESIZE;   // RTP 时间戳增量

    // 动态提前发送帧数相关
    constexpr int MAX_ADVANCE_FRAMES = 4;  // 最大提前发送帧数
    constexpr int MIN_ADVANCE_FRAMES = 2;  // 最小提前发送帧数
    constexpr int ADJUSTMENT_STEP_FRAMES = 1;  // 调整步长
    constexpr int MOVING_AVERAGE_SIZE = 5;  // 移动平均窗口大小

    // 初始提前发送帧数
    int current_advance_frames = MIN_ADVANCE_FRAMES;

    // 记录发送开始时间，用于计算“理想发送时刻”
    TimePoint start_time = Clock::now();

    // 记录已发送的帧索引（可用于调试或日志）
    int frame_index = 0;

    // 移动平均相关数据结构
    std::array<int, MOVING_AVERAGE_SIZE> send_durations = {0};
    int send_index = 0;            // 环状数组指针
    int total_send_duration_us = 0; // 移动平均的总和
    int count = 0;                 // 已累积的样本数

    while (true) {
        // ========== (1) 处理暂停与清空缓冲的请求 ==========
        while (audio_props.play_state == PAUSE) {
            // 若暂停，则等待事件唤醒
            co_await EventStateUpdate;
            EventStateUpdate.reset();
        }
        if (audio_props.do_empty_ring_buffer) {
            // 需要清空环形缓冲
            while (!rb.empty()) {
                co_await rb.consume();
            }
            audio_props.do_empty_ring_buffer = false;
        }

        // ========== (2) 计算“期望的发送时间”，并根据是否落后做补偿 ==========
        // 理论发送时间 = start_time + (OPUS_DELAY_MS * frame_index) - (提前时间)
        // 提前时间 = OPUS_DELAY_MS * current_advance_frames
        TimePoint expected_send_time =
                start_time
                + std::chrono::milliseconds(OPUS_DELAY_MS * frame_index)
                - std::chrono::milliseconds(OPUS_DELAY_MS * current_advance_frames);

        TimePoint now = Clock::now();

        if (now < expected_send_time) {
            // 还未到发送时间，等待
            co_await scheduler_->yield_until(expected_send_time);
        } else {
            // 说明已经比期望时间晚了，需要计算落后了多少帧
            auto delay_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - expected_send_time).count();
            int frames_late = delay_ms / OPUS_DELAY_MS;
            if (frames_late > 0) {
                // 如果落后了 frames_late 帧，则跳过相应的帧计数与 RTP 时间戳
                frame_index += frames_late;
                timestamp += frames_late * OPUS_RTP_FRAMESIZE;
            }
        }

        // ========== (3) 动态等待与批量发送 ==========
        size_t available_frames = rb.size();
        if (available_frames == 0) {
            // 缓冲区为空：先等待至少一帧，避免 std::min(...) 为 0
            auto maybe_frame = co_await rb.consume();
            if (!maybe_frame) {
                // 消费者被关闭（极端情况）
                LOG(ERROR) << "消费者关闭，无法获取更多音频帧，退出协程。";
                co_return;
            }

            // 成功拿到一帧后，检查是否需要退出
            if (isStopped && rb.empty()) {
                LOG(INFO) << "生产者已停止且缓冲区为空，退出发送器。";
                co_return;
            }

            // 仅发送这一帧
            std::vector<unsigned char> single_frame = std::move(*maybe_frame);

            // 记录发送开始时间
            auto batch_send_start = Clock::now();
            // 发送
            int result = main_stream->push_frame(single_frame.data(),
                                                 single_frame.size(),
                                                 timestamp,
                                                 RTP_NO_FLAGS);
            if (result != RTP_OK) {
                LOG(ERROR) << "发送遇到错误(单帧)";
            }

            // 更新时间戳 & 帧计数
            timestamp += OPUS_RTP_FRAMESIZE;
            frame_index += 1;

            // 统计本次发送耗时（只有 1 帧的情况）
            auto batch_send_end = Clock::now();
            auto batch_send_duration_us = std::chrono::duration_cast<
                    std::chrono::microseconds>(batch_send_end - batch_send_start).count();

            // 更新移动平均
            if (count < MOVING_AVERAGE_SIZE) {
                // 还没满窗口，直接插入
                send_durations[send_index] = batch_send_duration_us;
                total_send_duration_us += batch_send_duration_us;
                send_index = (send_index + 1) % MOVING_AVERAGE_SIZE;
                count++;
            } else {
                // 移除最旧的，再加入最新的
                total_send_duration_us -= send_durations[send_index];
                send_durations[send_index] = batch_send_duration_us;
                total_send_duration_us += batch_send_duration_us;
                send_index = (send_index + 1) % MOVING_AVERAGE_SIZE;
            }
            double average_send_duration_us =
                    static_cast<double>(total_send_duration_us) / count;

            // 根据移动平均值调整提前发送帧数
            if (average_send_duration_us >
                (OPUS_DELAY_US * current_advance_frames)) {
                // 发送耗时过大 → 减少提前发送
                current_advance_frames = std::max(
                        current_advance_frames - ADJUSTMENT_STEP_FRAMES,
                        MIN_ADVANCE_FRAMES
                );
                VLOG(2) << "减少提前发送帧数到 " << current_advance_frames << "帧 (单帧场景)";
            } else if (average_send_duration_us <
                       (OPUS_DELAY_US * (current_advance_frames - ADJUSTMENT_STEP_FRAMES))) {
                // 发送耗时足够小 → 增加提前发送
                current_advance_frames = std::min(
                        current_advance_frames + ADJUSTMENT_STEP_FRAMES,
                        MAX_ADVANCE_FRAMES
                );
                VLOG(2) << "增加提前发送帧数到 " << current_advance_frames << "帧 (单帧场景)";
            }

            // 下一轮循环继续
            continue;
        }

        // 如果缓冲区中有帧，可以批量发送
        // 确定本次要发送的帧数：取 current_advance_frames 和 available_frames 的最小值
        int batch_frames = std::min<int>(current_advance_frames, static_cast<int>(available_frames));

        // 预分配空间，减少内存开销
        std::vector<std::vector<unsigned char>> frames_to_send;
        frames_to_send.reserve(batch_frames);

        // 批量消费
        for (int i = 0; i < batch_frames; ++i) {
            // 如果停止且缓冲区空，就退出
            if (isStopped && rb.empty()) {
                LOG(INFO) << "生产者已停止且缓冲区为空，退出发送器。";
                co_return;
            }
            auto maybe_frame = co_await rb.consume();
            if (!maybe_frame) {
                // 消费者关闭
                LOG(ERROR) << "消费者关闭，无法再获取帧，退出协程。";
                co_return;
            }
            frames_to_send.emplace_back(std::move(*maybe_frame));
        }

        // ========== (4) 发送批量帧，统计发送耗时 ==========
        auto batch_send_start = Clock::now();
        for (auto &frame: frames_to_send) {
            int result = main_stream->push_frame(
                    frame.data(),
                    frame.size(),
                    timestamp,
                    RTP_NO_FLAGS
            );
            if (result != RTP_OK) {
                LOG(ERROR) << "发送遇到错误(批量帧)";
                // 可以考虑是否要 continue，或根据需求 break
                continue;
            }
            timestamp += OPUS_RTP_FRAMESIZE;
            frame_index++;
        }
        auto batch_send_end = Clock::now();

        // 计算批量发送时长（微秒）
        auto batch_send_duration_us = std::chrono::duration_cast<
                std::chrono::microseconds>(batch_send_end - batch_send_start).count();

        // ========== (5) 更新移动平均 & 动态调整提前发送帧数 ==========
        if (count < MOVING_AVERAGE_SIZE) {
            // 移动平均还没满窗口，直接插入
            send_durations[send_index] = batch_send_duration_us;
            total_send_duration_us += batch_send_duration_us;
            send_index = (send_index + 1) % MOVING_AVERAGE_SIZE;
            count++;
        } else {
            // 移除最旧，加入最新
            total_send_duration_us -= send_durations[send_index];
            send_durations[send_index] = batch_send_duration_us;
            total_send_duration_us += batch_send_duration_us;
            send_index = (send_index + 1) % MOVING_AVERAGE_SIZE;
        }

        double average_send_duration_us =
                static_cast<double>(total_send_duration_us) / count;

        // 与期望时长对比：期望时长 ~ OPUS_DELAY_US * current_advance_frames
        if (average_send_duration_us > (OPUS_DELAY_US * current_advance_frames)) {
            // 如果批量发送平均耗时 > 期望时长，说明发送慢，减少提前发送帧数
            current_advance_frames = std::max(
                    current_advance_frames - ADJUSTMENT_STEP_FRAMES,
                    MIN_ADVANCE_FRAMES
            );
            VLOG(2) << "减少提前发送帧数到 " << current_advance_frames << "帧";
        } else if (average_send_duration_us <
                   (OPUS_DELAY_US * (current_advance_frames - ADJUSTMENT_STEP_FRAMES))) {
            // 如果批量发送平均耗时 < (期望时长 - 1帧)，说明发送快，增加提前发送帧数
            current_advance_frames = std::min(
                    current_advance_frames + ADJUSTMENT_STEP_FRAMES,
                    MAX_ADVANCE_FRAMES
            );
            VLOG(2) << "增加提前发送帧数到 " << current_advance_frames << "帧";
        }

        // 再次确保提前发送帧数在 [MIN_ADVANCE_FRAMES, MAX_ADVANCE_FRAMES] 范围内
        current_advance_frames = std::clamp(current_advance_frames,
                                            MIN_ADVANCE_FRAMES,
                                            MAX_ADVANCE_FRAMES);

        // 记录日志，可根据需要调整 VLOG 级别或使用其他日志方式
        VLOG(2) << "批次开始帧 " << (frame_index - frames_to_send.size())
                << " ，发送了 " << frames_to_send.size() << " 帧"
                << " ，当前提前 " << current_advance_frames * OPUS_DELAY_MS << "ms"
                << " ，平均批量发送耗时 " << average_send_duration_us << "us";
    } // while(true)

    // 理论上不会走到这里
    co_return;
}
