#include "RTPInstance.h"
#include <glog/logging.h>

RTPInstance::RTPInstance(const std::string &remote_address) {
    this->remote_address = remote_address;
    session = ctx.create_session(remote_address); // 每个实例有自己的 session
    main_stream_ = nullptr; // 初始化主流为空
}

RTPInstance::~RTPInstance() {
    for (auto &[id, stream]: streams_) {
        session->destroy_stream(stream); // 销毁所有流
    }
    if (main_stream_) {
        session->destroy_stream(main_stream_); // 销毁主流
    }
    ctx.destroy_session(session);
}

uvgrtp::media_stream *
RTPInstance::createStream(const std::string &stream_id, const ChannelJoinedData &streamInfo, RTP_FORMAT format,
                          int flags) {
    if (streamInfo.rtcp_mux) flags |= RCE_RTCP_MUX;

    auto stream_ = session->create_stream(streamInfo.port, format, flags);
    if (!stream_) {
        LOG(ERROR) << "Failed to create stream for ID: " << stream_id << " IP " << remote_address << " Port "
                   << streamInfo.port;
        return nullptr;
    }

    stream_->configure_ctx(RCC_SSRC, streamInfo.audio_ssrc);
    stream_->configure_ctx(RCC_DYN_PAYLOAD_TYPE, streamInfo.audio_pt);
    stream_->configure_ctx(RCC_CLOCK_RATE, 48000);
    stream_->configure_ctx(RCC_MTU_SIZE, 1200);
    stream_->configure_ctx(RCE_FRAGMENT_GENERIC, 1); // 启用分片以支持超过 MTU 的包
    if (!main_stream_) {
        main_stream_ = stream_;
        main_stream_id_ = stream_id;
    }

    // 如果主流已经存在，并且请求创建新的流，直接将新流放入 map 中
    streams_[stream_id] = stream_;

    stream_timestamps[stream_] = generate_initial_timestamp();

    return stream_;
}

uvgrtp::media_stream *RTPInstance::getStream(const std::string &stream_id) {
    if (stream_id == main_stream_id_) {
        return main_stream_;  // 始终返回主流作为主要流
    }

    // 查找额外流
    auto it = streams_.find(stream_id);
    return (it != streams_.end()) ? it->second : nullptr;
}

bool RTPInstance::destoryStream(const std::string &stream_id) {
    auto target = getStream(stream_id);
    if (target == nullptr) {
        return false;
    }
    stream_timestamps.erase(target);
    streams_.erase(stream_id);
    session->destroy_stream(target);
    return true;
}

bool RTPInstance::destoryStream(uvgrtp::media_stream *stream) {
    if (stream == nullptr) {
        return false;
    }
    stream_timestamps.erase(stream);

    bool found = false;
    auto it = streams_.begin();
    while (it != streams_.end()) {
        if (it->second == stream) {
            // Erase the element and break (or continue if there might be multiple matching values)
            streams_.erase(it);
            found = true;
            break; // Remove this if you want to delete multiple occurrences
        } else {
            ++it; // Move to the next element
        }
    }

    if (found) session->destroy_stream(stream);
    return found;
}

