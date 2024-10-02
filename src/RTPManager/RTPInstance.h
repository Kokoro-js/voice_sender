#ifndef RTP_INSTANCE_H
#define RTP_INSTANCE_H

#include <string>
#include <unordered_map>
#include <random>
#include <uvgrtp/context.hh>
#include <uvgrtp/session.hh>
#include <uvgrtp/media_stream.hh>

#include "../kook/ChannelJoinedData.h"


class RTPInstance {
public:
    // 构造函数，传入远程地址
    explicit RTPInstance(const std::string &remote_address);

    std::string remote_address;

    // 析构函数
    ~RTPInstance();

    // 创建流，传入流标识（可以是字符串）、格式和标志
    uvgrtp::media_stream *createStream(const std::string &stream_id, const ChannelJoinedData &streamInfo,
                                       RTP_FORMAT format, int flags);

    // 获取流，通过流标识（字符串）
    uvgrtp::media_stream *getStream(const std::string &stream_id);

    bool destoryStream(const std::string &stream_id);

    bool destoryStream(uvgrtp::media_stream *stream);

    // 优化：默认情况下只处理一个流
    uvgrtp::media_stream *main_stream_;
    std::string main_stream_id_;
    uint32_t main_stream_timestamp_ = generate_initial_timestamp();

    // 当有多个流时，使用此 map 存储
    std::unordered_map<std::string, uvgrtp::media_stream *> streams_;
    std::unordered_map<uvgrtp::media_stream *, uint32_t> stream_timestamps;

    static inline uint32_t generate_initial_timestamp() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);
        return dis(gen);

        /*auto now = std::chrono::system_clock::now();
        // 计算与基准时间的差异
        auto duration = now.time_since_epoch();
        // 将时间差转换为秒数
        auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration).count();

        return static_cast<uint32_t>(seconds);*/
    }

private:
    uvgrtp::context ctx;
    uvgrtp::session *session;
};

#endif // RTP_INSTANCE_H
