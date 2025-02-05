#include "Handlers.h"
#include "../../RTPManager/RTPManager.h"

void Handlers::startStreamHandler(const Instance::StartStreamPayload *data, OMNI::Response &res) {
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
    auto stream_id = res.stream_id();


    int flags = RCE_SEND_ONLY;

    auto rtp_instance = RTPManager::getInstance().getRTPInstance(stream_id, streamInfo.ip);

    auto stream_ = rtp_instance->createStream(stream_id, streamInfo, RTP_FORMAT_OPUS, flags);
    if (stream_ == nullptr) {
        LOG(ERROR) << "创建流失败";
        res.set_code(OMNI::ERROR);
        res.set_message("创建流失败。");
        return;
    }

    auto sender = std::make_shared<AudioSender>(stream_id, rtp_instance, tp, scheduler);
    sender->setOpusBitRate(streamInfo.bitrate);
    /*if (sender->is_initialized() == false) {
        LOG(ERROR) << "添加流请求失败";
        return;
    }*/
    auto manager = new DownloadManager(tp, std::move(sender));

    std::vector<TaskItem> newTasks;
    std::vector<std::string> newOrder;
    for (const auto &order: data->order_list()) {
        LOG(INFO) << order.url();
        TaskItem task{
                .name = order.task_id(), .url = order.url(), .type = static_cast<TaskType>(order.type()),
                .use_stream = order.use_stream()
        };
        newTasks.push_back(task);
        newOrder.push_back(task.name);
    }

    manager->updateTasks(newTasks, newOrder);

    const std::string &key = stream_id;
    instanceMap[key] = manager;
    // 设置移除自己的回调函数
    manager->setRemoveCallback([this](const std::string &id) {
        // 在 map 中移除该实例
        instanceMap.erase(id);
    }, key);

    cleanup_task_container_.start(manager->initAndWaitJobs());
    cleanup_task_container_.garbage_collect();
    LOG(INFO) << "成功添加流请求";
}
