#include "DownloadManager/DownloadManager.h"
#include <coro/coro.hpp>

#include "plog/Log.h"
#include <plog/Init.h>
#include <plog/Appenders/ColorConsoleAppender.h>
#include <plog/Appenders/RollingFileAppender.h>
#include <plog/Formatters/TxtFormatter.h>

#include "api/EventPublisher.h"
#include "api/handlers/Handlers.h"
#include "DownloadManager/AudioSender/AudioSender.h"
#include "RTPManager/RTPManager.h"

void createStreamAndManageTasks(const std::string &stream_id, const std::string &ip, int port,
                                const std::vector<std::string> &tasks) {
    EventPublisher &publisher = EventPublisher::getInstance();
    Handlers &handlers = Handlers::getInstance();

    // Parse and get stream info
    ChannelJoinedData streamInfo = { .ip = ip, .port = port, .audio_pt = 111};
    int flags = RCE_SEND_ONLY | RCE_RTCP;
    if (streamInfo.rtcp_mux) flags |= RCE_RTCP_MUX;

    // Get RTP instance and create stream
    auto rtp_instance = RTPManager::getInstance().getRTPInstance(stream_id, streamInfo.ip);
    auto stream_ = rtp_instance->createStream(stream_id, streamInfo, RTP_FORMAT_OPUS, flags);
    if (!stream_) {
        LOGE << "Failed to create stream for " << stream_id;
        return;
    }

    // Create AudioSender and DownloadManager
    std::unique_ptr<AudioSender> sender = std::make_unique<AudioSender>(stream_id, rtp_instance, handlers.tp,
                                                                        handlers.scheduler);
    auto manager = new DownloadManager(handlers.tp, std::move(sender));

    // Add download tasks dynamically from the task list
    int taskIndex = 1;
    for (const auto &taskUrl: tasks) {
        auto name = "task" + std::to_string(taskIndex++);
        manager->addTask(name, { .name = name, .url = taskUrl, .type = File});
        auto item = manager->task_order.at(0);
        LOGI << item;
    }

    // Manage tasks
    handlers.instanceMap.emplace(stream_id, manager);
    handlers.cleanup_task_container_.start(manager->initAndWaitJobs());
}

int main() {
    static plog::ColorConsoleAppender<plog::TxtFormatter> consoleAppender;
    static plog::RollingFileAppender<plog::TxtFormatter> fileAppender("Hello.txt", 1000000, 3);
    plog::init(plog::debug, &consoleAppender).addAppender(&fileAppender);

    if (mpg123_init() != MPG123_OK) {
        // Handle initialization error
        return EXIT_FAILURE;
    }

    // 创建 DownloadManager 实例
    EventPublisher &publisher = EventPublisher::getInstance();
    Handlers &handlers = Handlers::getInstance();

    createStreamAndManageTasks("test2", "172.20.240.1", 6005,
                               {
                                   // "http://172.20.240.1/100-KB-MP3.mp3",
                                   "http://172.20.240.1/Sample_BeeMoved_48kHz16bit.m4a",
                                   /*"http://172.20.240.1/test.flac",
                                   "http://172.20.240.1/test.flac",*/
                                   // "http://172.20.240.1/500-KB-MP3.mp3",
                                   // "http://172.20.240.1/Rig%c3%abl%20Theatre%20-%20SP%c3%8fKA.mp3",
                                   // "http://172.20.240.1/%e3%81%8b%e3%81%ad%e3%81%93%e3%81%a1%e3%81%af%e3%82%8b%20-%20amethyst.mp3",
                                   "http://172.20.240.1/Dragonflame.mp3",
                                   "http://172.20.240.1/Setsuna.mp3"});

    while (true) {
        publisher.handle_request_response();
    }

    mpg123_exit();
    return 0;
}
