#include "RTPInstance.h"
#include <glog/logging.h>

RTPInstance::RTPInstance(const std::string &remote_address)
        : remote_address(remote_address),
          session(ctx.create_session(remote_address)),
          main_stream_(nullptr),
          main_stream_timestamp_(generate_initial_timestamp()) {
    if (!session) {
        LOG(ERROR) << "Failed to create RTP session for remote address: " << remote_address;
    }
}

RTPInstance::~RTPInstance() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &[id, stream]: streams_) {
        if (stream) {
            session->destroy_stream(stream.get());
        }
    }
    streams_.clear();

    if (main_stream_) {
        session->destroy_stream(main_stream_.get());
    }
    ctx.destroy_session(session);
}

std::shared_ptr<uvgrtp::media_stream> RTPInstance::createStream(const std::string &stream_id,
                                                                const ChannelJoinedData &streamInfo,
                                                                RTP_FORMAT format, int flags) {
    std::lock_guard<std::mutex> lock(mutex_);

    int final_flags = flags;
    /*if (streamInfo.rtcp_port) {
     * uvgRTP 目前不支持自定义 RTCP 端口，当 MUX 启用时再用 RTCP
    }*/
    if (streamInfo.rtcp_mux) {
        final_flags |= RCE_RTCP;
        final_flags |= RCE_RTCP_MUX;
    }
    // 事实证明 KOOK 并不能处理分包。
    /*if (streamInfo.bitrate >= 256000) {
        final_flags |= RCE_FRAGMENT_GENERIC;
    }*/

    auto stream = std::shared_ptr<uvgrtp::media_stream>(session->create_stream(streamInfo.port, format, final_flags),
                                                        [this](uvgrtp::media_stream *s) {
                                                            this->destroyStream(std::shared_ptr<uvgrtp::media_stream>(s,
                                                                                                                      [](uvgrtp::media_stream *) {}));
                                                        });

    if (!stream) {
        LOG(ERROR) << "Failed to create stream for ID: " << stream_id << " IP " << remote_address
                   << " Port " << streamInfo.port;
        return nullptr;
    }

    stream->configure_ctx(RCC_SSRC, streamInfo.audio_ssrc);
    stream->configure_ctx(RCC_DYN_PAYLOAD_TYPE, streamInfo.audio_pt);
    stream->configure_ctx(RCC_CLOCK_RATE, 48000);
    stream->configure_ctx(RCC_MTU_SIZE, 1408); // KOOK 只支持到 1500

    if (!main_stream_) {
        main_stream_ = stream;
        main_stream_id_ = stream_id;
    }

    streams_[stream_id] = stream;
    stream_timestamps_[stream] = generate_initial_timestamp();

    return stream;
}

std::shared_ptr<uvgrtp::media_stream> RTPInstance::getStream(const std::string &stream_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stream_id == main_stream_id_) {
        return main_stream_;
    }

    auto it = streams_.find(stream_id);
    return (it != streams_.end()) ? it->second : nullptr;
}

void RTPInstance::destroyStream(const std::string &stream_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = streams_.find(stream_id);
    if (it != streams_.end()) {
        auto stream = it->second;
        session->destroy_stream(stream.get());
        stream_timestamps_.erase(stream);
        streams_.erase(it);
        if (main_stream_ == stream) {
            main_stream_.reset();
        }
    }
}

void RTPInstance::destroyStream(std::shared_ptr<uvgrtp::media_stream> stream) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!stream) return;

    stream_timestamps_.erase(stream);
    for (auto it = streams_.begin(); it != streams_.end(); ++it) {
        if (it->second == stream) {
            session->destroy_stream(stream.get());
            if (main_stream_ == stream) {
                main_stream_.reset();
            }
            streams_.erase(it);
            break;
        }
    }
}

uint32_t RTPInstance::generate_initial_timestamp() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
    return dis(gen);
}

// Getter 方法实现
std::shared_ptr<uvgrtp::media_stream> RTPInstance::getMainStream() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return main_stream_;
}

uint32_t RTPInstance::getMainStreamTimestamp() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return main_stream_timestamp_;
}
