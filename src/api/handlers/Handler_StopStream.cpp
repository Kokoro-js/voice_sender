#include <zmq.hpp>

#include "Handlers.h"
#include "../../RTPManager/RTPManager.h"

void Handlers::stopStreamHandler(const OMNI::RemoveStreamPayload *data) {
    auto key = data->info().stream_id();
    auto it = instanceMap.find(key);
    if (it != instanceMap.end()) {
        auto instance = it->second;
        instance->cleanupJob();
    }
    RTPManager::getInstance().removeInstance(key);
}
