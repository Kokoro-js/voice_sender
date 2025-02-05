#include "AudioSender.h"
#include <opus/opus.h>
// #include <../../../include/opusenc.h>
#include <mpg123.h>
#include <utility>
#include <fstream>
#include "../../api/handlers/Handlers.h"
#include "../../RTPManager/RTPManager.h"
#include "AudioAlignedAlloc.h"

void rtp_receive_hook(void *arg, uvgrtp::frame::rtp_frame *frame) {
    LOG(WARNING) << "Received RTP frame" << frame->payload_len;
    Handlers &handlers = Handlers::getInstance();

    // 如果你需要修改 frame 的某些字段（如 SSRC 或时间戳），可以在这里进行
    frame->header.payload = 111; // 随便设置一个
    (void) uvgrtp::frame::dealloc_frame(frame);
}


AudioSender::AudioSender(std::string stream_id, std::shared_ptr<RTPInstance> rtp_instance,
                         std::shared_ptr<coro::thread_pool> tp, std::shared_ptr<coro::io_scheduler> scheduler)
        : stream_id_(std::move(stream_id)), rtp_instance_(std::move(rtp_instance)), tp_(std::move(tp)),
          scheduler_(std::move(scheduler))/*,
      ffmpeg_decoder(&ioBufWarp)*/,
          read_output_buffer_(AlignedMem::make_aligned_unique<unsigned char>(MAX_DECODE_SIZE)),
          float_buffer_(AlignedMem::make_aligned_unique<float>(MAX_SAMPLES_COUNT)),
          resampled_buffer_(AlignedMem::make_aligned_unique<int16_t>(MAX_SAMPLES_COUNT)) {
    assert(((reinterpret_cast<uintptr_t>(read_output_buffer_.get()) % xsimd::default_arch::alignment()) == 0) &&
           "buffer is not properly aligned");
    assert(((reinterpret_cast<uintptr_t>(float_buffer_.get()) % xsimd::default_arch::alignment()) == 0) &&
           "float_buffer is not properly aligned");
    assert(((reinterpret_cast<uintptr_t>(resampled_buffer_.get()) % xsimd::default_arch::alignment()) == 0) &&
           "resampled_buffer is not properly aligned");
    // initialize_opus_file();
/*    if (rtp_instance_->main_stream_->install_receive_hook(this, rtp_receive_hook) != RTP_OK) {
        LOG(ERROR) << "Failed to install RTP reception hook";
        return;
    }*/

    int error;
    opus_encoder_ = opus_encoder_create(48000, 2, OPUS_APPLICATION_AUDIO, &error);
    if (error != OPUS_OK) {
        LOG(ERROR) << "Failed to initialize Opus encoder: " << opus_strerror(error);
        return;
    }

    opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR(1)); // 启用变码率。
    opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR_CONSTRAINT(1)); // 限制编码率波动范围。
    opus_encoder_ctl(opus_encoder_, OPUS_SET_COMPLEXITY(10)); // 复杂度 10
    opus_encoder_ctl(opus_encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC)); // Signal being encoded is music
    opus_encoder_ctl(opus_encoder_, OPUS_SET_BANDWIDTH(OPUS_AUTO)); // 让他自己根据参数设定
    opus_encoder_ctl(opus_encoder_, OPUS_SET_INBAND_FEC(0)); // 禁用FEC

    /*opus_encoder_ctl(opus_encoder_, OPUS_SET_VBR_CONSTRAINT(0));  // 禁用波动限制，可以让音质有更大的自由度（根据网络情况）
    opus_encoder_ctl(opus_encoder_, OPUS_SET_DTX(0));  // This is only applicable to the LPC layer
    opus_encoder_ctl(opus_encoder_, OPUS_SET_PACKET_LOSS_PERC(0));*/
    // opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(128 * 1000));

    mpg123_decoder.setBuffer(&data_wrapper);
    ffmpeg_decoder.setBuffer(&data_wrapper);
    using_decoder = &mpg123_decoder;

    initialized_ = true;
    LOG(INFO) << "Stream setup successfully with ID: " << stream_id_;
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

/*
void AudioSender::initialize_opus_file() {
    // 初始化 Opus 文件编码器
    comments = ope_comments_create();
    enc = ope_encoder_create_file("output.opus", comments, 48000, 2, 0, &err);
    if (err != OPE_OK) {
        LOG(ERROR) << "Failed to create Opus file encoder: " << ope_strerror(err);
    }
}

void AudioSender::finalize_opus_file() {
    // 关闭 Opus 文件编码器并释放资源
    if (enc) {
        ope_encoder_drain(enc);
        ope_encoder_destroy(enc);
    }
    if (comments) {
        ope_comments_destroy(comments);
    }
}*/

int AudioSender::setOpusBitRate(const int &kbps) {
    // 设置 Opus 比特率
    return opus_encoder_ctl(opus_encoder_, OPUS_SET_BITRATE(kbps));
}
