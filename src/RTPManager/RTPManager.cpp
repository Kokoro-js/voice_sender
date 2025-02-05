#include "RTPManager.h"

std::shared_ptr<RTPInstance> RTPManager::getRTPInstance(const std::string &instance_id,
                                                        const std::string &remote_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rtpInstances.find(instance_id);
    if (it != rtpInstances.end()) {
        if (auto existingInstance = it->second.lock()) {
            return existingInstance;
        }
    }

    // 创建新的实例
    auto newInstance = std::make_shared<RTPInstance>(remote_address);
    rtpInstances[instance_id] = newInstance;
    return newInstance;
}

void RTPManager::removeInstance(const std::string &instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    rtpInstances.erase(instance_id);
}
