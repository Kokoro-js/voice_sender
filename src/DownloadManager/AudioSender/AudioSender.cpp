#include "AudioSender.h"
#include <plog/Log.h>
#include <opus/opus.h>
#include <../../../include/opusenc.h>
#include <mpg123.h>
#include <utility>
#include <fstream>
#include "../../api/handlers/Handlers.h"
#include "../../RTPManager/RTPManager.h"

void rtp_receive_hook(void *arg, uvgrtp::frame::rtp_frame *frame) {
    LOGW << "Received RTP frame" << frame->payload_len;
    Handlers &handlers = Handlers::getInstance();

    // 如果你需要修改 frame 的某些字段（如 SSRC 或时间戳），可以在这里进行
    frame->header.payload = 111; // 随便设置一个
    (void) uvgrtp::frame::dealloc_frame(frame);
}


AudioSender::AudioSender(std::string stream_id, std::shared_ptr<RTPInstance> rtp_instance,
                         std::shared_ptr<coro::thread_pool> tp, std::shared_ptr<coro::io_scheduler> scheduler)
    : stream_id_(std::move(stream_id)), tp_(std::move(tp)), scheduler_(std::move(scheduler)),
      rtp_instance_(std::move(rtp_instance)), opus_encoder_(nullptr), initialized_(false),
      data_buffer(DEFAULT_DATA_BUFFER_SIZE),
      ffmpeg_decoder(&data_buffer) {
    // initialize_opus_file();
    if (rtp_instance_->main_stream_->install_receive_hook(this, rtp_receive_hook) != RTP_OK) {
        LOGE << "Failed to install RTP reception hook";
        return;
    }

    int error;
    opus_encoder_ = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK) {
        PLOG_ERROR << "Failed to initialize Opus encoder: " << opus_strerror(error);
        return;
    }

    opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR(1)); // 启用变码率。
    opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR_CONSTRAINT(1)); // 限制编码率波动范围。
    // opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(128 * 1000));

    /*if (mpg123_open_feed(mpg123_handle_) != MPG123_OK) {
        PLOG_ERROR << "Failed to open mpg123 feed.";
        cleanup();
        return;
    }*/

    /*if (setupMPG123() != MPG123_OK) {
        PLOG_ERROR << "Failed to open mpg123 handler.";
        cleanup();
        return;
    }*/

    mpg123_decoder.setBuffer(&data_buffer);
    if (mpg123_decoder.setup() != 0) {
    }
    if (ffmpeg_decoder.setup() != 0) {
    }
    // using_decoder = &mpg123_decoder;

    initialized_ = true;
    PLOG_INFO << "Stream setup successfully with ID: " << stream_id_;
}

AudioSender::~AudioSender() {
    if (opus_encoder_) {
        opus_encoder_destroy(opus_encoder_);
        opus_encoder_ = nullptr;
    }
}

bool AudioSender::is_initialized() const {
    return initialized_;
}
