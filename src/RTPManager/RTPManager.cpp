#include "RTPManager.h"

std::shared_ptr<RTPInstance> RTPManager::getRTPInstance(const std::string &instance_id,
                                                        const std::string &remote_address) {
    auto it = rtpInstances.find(instance_id);
    if (it != rtpInstances.end()) {
        return it->second;
    }
    auto newInstance = std::make_shared<RTPInstance>(remote_address);
    rtpInstances[instance_id] = newInstance;
    return newInstance;
}

void RTPManager::removeInstance(const std::string &instance_id) {
    rtpInstances.erase(instance_id);
}
