#ifndef RTP_MANAGER_H
#define RTP_MANAGER_H

#include "RTPInstance.h"
#include <unordered_map>
#include <string>
#include <memory>

class RTPManager {
public:
    // 获取全局唯一的 RTPManager 实例
    static RTPManager &getInstance() {
        static RTPManager instance;
        return instance;
    }

    // 禁止拷贝构造和赋值操作符
    RTPManager(const RTPManager &) = delete;

    RTPManager &operator=(const RTPManager &) = delete;

    // 获取 RTP 实例，通过标识字符串
    std::shared_ptr<RTPInstance> getRTPInstance(const std::string &instance_id, const std::string &remote_address);

    // 移除实例
    void removeInstance(const std::string &instance_id);

private:
    // 构造函数设为私有，确保只能通过 getInstance 获取实例
    RTPManager() = default;

    std::unordered_map<std::string, std::shared_ptr<RTPInstance> > rtpInstances;
};

#endif // RTP_MANAGER_H
