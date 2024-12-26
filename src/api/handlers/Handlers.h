#ifndef HANDLERS_H
#define HANDLERS_H

#include "HandlersBase.h"
#include "../../DownloadManager/DownloadManager.h"
#include <unordered_map>
#include "Request.pb.h"
#include "Response.pb.h"

using namespace OMNI;

class Handlers : public HandlersBase {
public:
    // 获取单例实例的静态方法
    static Handlers &getInstance() {
        static Handlers instance;
        return instance;
    }

    Handlers(const Handlers &) = delete;

    Handlers &operator=(const Handlers &) = delete;

    // 实现具体的处理函数
    void startStreamHandler(const Instance::StartStreamPayload *data, OMNI::Response &res);

    void stopStreamHandler(const Instance::RemoveStreamPayload *data, OMNI::Response &res);

    void getStreamHandler(const Instance::GetStreamPayload *data, OMNI::Response &res);

    void updateStreamHandler(const Instance::UpdateStreamPayload *data, OMNI::Response &res);

    void getPlayListHandler(const Instance::GetPlayListPayload *data, OMNI::Response &res);

    void updatePlayListHandler(const Instance::UpdatePlayListPayload *data, OMNI::Response &res);

    std::optional<DownloadManager *> findById(const std::string &id) {
        auto it = instanceMap.find(id);
        if (it != instanceMap.end()) {
            return it->second;
        }
        return {};
    }

    static OMNI::Response get_res(const std::string &req_id, const std::string &stream_id) {
        OMNI::Response res;
        res.set_code(SUCCESS);
        res.set_message("请求成功。");

        // 设置请求 ID：为空时自动生成
        if (req_id.empty()) {
            res.set_id(generateBinaryId());
        } else {
            res.set_id(req_id); // 使用传入 ID
        }

        // 设置流 ID
        res.set_stream_id(stream_id);

        return res;
    }

    static OrderItem_OrderType Task2OrderType(TaskType type) {
        switch (type) {
            case TaskType::File:
                return OrderItem_OrderType_FILE;
            case TaskType::Cached:
                return OrderItem_OrderType_CACHED;
        }
    }

    std::unordered_map<std::string, DownloadManager *> instanceMap;

private:
    Handlers() = default;

    ~Handlers() = default;

    /**
     * 生成 16 字节的随机二进制 ID
     */
    static std::string generateBinaryId() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dis;

        std::array<uint8_t, 16> id; // 16 字节数组
        uint64_t high = dis(gen);
        uint64_t low = dis(gen);

        memcpy(id.data(), &high, 8); // 高 8 字节
        memcpy(id.data() + 8, &low, 8); // 低 8 字节

        // 返回二进制 ID
        return std::string(reinterpret_cast<const char *>(id.data()), id.size());
    }

protected:
};

#endif // HANDLERS_H
