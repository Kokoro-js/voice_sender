#include "DownloadManager/DownloadManager.h"
#include <coro/coro.hpp>
#include "ConfigManager.h"
#include "CurlMultiManager.h"
#include <glog/logging.h>

#include "api/EventPublisher.h"
#include "api/handlers/Handlers.h"
#include "DownloadManager/AudioSender/AudioSender.h"
#include "RTPManager/RTPManager.h"

void createStreamAndManageTasks(const std::string &stream_id, const std::string &ip, int port,
                                const std::vector<std::string> &tasks) {
    EventPublisher &publisher = EventPublisher::getInstance();
    Handlers &handlers = Handlers::getInstance();

    // Parse and get stream info
    ChannelJoinedData streamInfo = {
            .ip = ip,
            .port = port,
            .audio_pt = 111,
    };
    int flags = RCE_SEND_ONLY | RCE_RTCP;
    if (streamInfo.rtcp_mux) flags |= RCE_RTCP_MUX;

    // Get RTP instance and create stream
    auto rtp_instance = RTPManager::getInstance().getRTPInstance(stream_id, streamInfo.ip);
    auto stream_ = rtp_instance->createStream(stream_id, streamInfo, RTP_FORMAT_OPUS, flags);
    if (!stream_) {
        LOG(INFO) << "Failed to create stream for " << stream_id;
        return;
    }

    // Create AudioSender and DownloadManager
    std::unique_ptr<AudioSender> sender = std::make_unique<AudioSender>(stream_id, rtp_instance, handlers.tp,
                                                                        handlers.scheduler);
    auto manager = new DownloadManager(handlers.tp, std::move(sender));

    // Add download tasks dynamically from the task list
    int taskIndex = 1;
    for (const auto &taskUrl: tasks) {
//        auto name = "URL" + std::to_string(taskIndex++);
        auto name = "URL:" + taskUrl;
        if (taskUrl.starts_with("http://172.20.240.1:3000")) {
            manager->addTask({.name = name, .url = taskUrl, .type = TaskType::Cached});
        } else
            manager->addTask({.name = name, .url = taskUrl, .type = TaskType::File});
    }

    // Manage tasks
    handlers.instanceMap.emplace(stream_id, manager);
    handlers.cleanup_task_container_.start(manager->initAndWaitJobs());
}

int main(int argc, char *argv[]) {
#ifdef IS_RELEASE_BUILD
    std::cout << "Voice_Connector: Release Build" << std::endl;
#else
    std::cout << "Voice_Connector: Debug Build" << std::endl;
#endif

    google::InitGoogleLogging(argv[0]);
    google::SetStderrLogging(google::GLOG_INFO); // 设置标准错误输出的最低日志级别
    FLAGS_alsologtostderr = true; // 日志输出到 stderr
    FLAGS_colorlogtostderr = true; // 启用彩色日志（如果终端支持）
    FLAGS_v = 1;
    google::SetVLOGLevel("IO_*", 1);

    // 获取配置管理器实例并初始化，加载 "config.toml" 文件
    ConfigManager::getInstance().initialize(argc, argv, "config.toml");

    // 获取配置
    const Config &config = ConfigManager::getInstance().getConfig();

    // 打印配置
    ConfigManager::getInstance().printConfig();

    if (mpg123_init() != MPG123_OK) {
        // Handle initialization error
        return EXIT_FAILURE;
    }


    // 创建 DownloadManager 实例
    EventPublisher &publisher = EventPublisher::getInstance();
    Handlers &handlers = Handlers::getInstance();
    CurlMultiManager &curlManager = CurlMultiManager::getInstance();

    /*createStreamAndManageTasks("test", "172.20.240.1", 5004,
                           {"http://172.20.240.1/100-KB-MP3.mp3",
                            "http://172.20.240.1/100-KB-MP3.mp3", "http://172.20.240.1/100-KB-MP3.mp3", "http://172.20.240.1/100-KB-MP3.mp3","http://172.20.240.1/100-KB-MP3.mp3"});*/

    /*createStreamAndManageTasks("test2", "172.20.240.1", 6005,
                               {
                                       "http://172.20.240.1/500-KB-MP3.mp3",
                                       "http://172.20.240.1/500-KB-MP3.mp3",
                                       "http://172.20.240.1/Dragonflame.mp3",
                                       "http://172.20.240.1/Setsuna.mp3",
                                       "http://172.20.240.1:3000/plugin/url/NETEASE:29143062",
                                       "http://172.20.240.1:3000/plugin/url/NETEASE:1367011294",
                                       "http://172.20.240.1:3000/plugin/url/NETEASE:22710767",
                                       "http://172.20.240.1:3000/plugin/url/NETEASE:2618516962",
                                       "http://172.20.240.1:3000/plugin/url/NETEASE:2612421551",
                               });*/

    while (true) {
        publisher.handle_request_response();
    }

    mpg123_exit();
    return 0;
}
