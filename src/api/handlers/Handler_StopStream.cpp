#include <zmq.hpp>

#include "Handlers.h"
#include "../../RTPManager/RTPManager.h"

void Handlers::stopStreamHandler(const Instance::RemoveStreamPayload *data, OMNI::Response &res) {
    auto streamId = res.stream_id();
    auto targetOpt = findById(streamId);
    if (!targetOpt.has_value()) {
        res.set_code(OMNI::NOT_FOUND);
        res.set_message("StopSteam: 未找到对应 ID 的流");
        return;
    }
    auto target = targetOpt.value();

    target->cleanupJob();
    RTPManager::getInstance().removeInstance(streamId);
}
