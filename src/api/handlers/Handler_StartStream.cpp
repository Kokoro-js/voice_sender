#include <plog/Log.h>

#include "Handlers.h"
#include "../../RTPManager/RTPManager.h"

void Handlers::startStreamHandler(const OMNI::StartStreamPayload *data) {
    LOGI << "已经接收到请求了" << data->info().stream_id();

    // 创建协程任务
    const auto &stream_info = data->stream_info();
    auto streamInfo = ChannelJoinedData{
        stream_info.ip(),
        stream_info.port(),
        stream_info.rtcp_port(),
        stream_info.audio_ssrc(),
        stream_info.audio_pt(),
        stream_info.bitrate(),
        stream_info.rtcp_mux()
    };
    auto stream_id = data->info().stream_id();


    // int flags = RCE_SEND_ONLY | RCE_RTCP;
    int flags = RCE_RTCP;
    if (streamInfo.rtcp_mux) flags |= RCE_RTCP_MUX;

    auto rtp_instance = RTPManager::getInstance().getRTPInstance(stream_id, streamInfo.ip);

    auto stream_ = rtp_instance->createStream(stream_id, streamInfo, RTP_FORMAT_OPUS, flags);
    if (stream_ == nullptr) {
        LOGE << "创建流失败";
        return;
    }

    auto sender = std::make_shared<AudioSender>(stream_id, rtp_instance, tp, scheduler);
    /*if (sender->is_initialized() == false) {
        LOGE << "添加流请求失败";
        return;
    }*/
    auto manager = new DownloadManager(tp, std::move(sender));

    for (const auto &order: data->order_list()) {
        LOGI << order.url();
        auto type = order.type();
        manager->addTask(order.task_name(), { .name = order.task_name(), .url = order.url(), .type = File });
    }

    const std::string &key = stream_id;
    instanceMap[key] = manager;
    // 设置移除自己的回调函数
    manager->setRemoveCallback([this](const std::string &id) {
        // 在 map 中移除该实例
        instanceMap.erase(id);
    }, key);

    cleanup_task_container_.start(manager->initAndWaitJobs());
    cleanup_task_container_.garbage_collect();
    LOGI << "成功添加流请求";
}
