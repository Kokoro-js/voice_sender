#ifndef RTP_INSTANCE_H
#define RTP_INSTANCE_H

#include <string>
#include <unordered_map>
#include <memory>
#include <random>
#include <mutex> // 添加互斥锁
#include <uvgrtp/context.hh>
#include <uvgrtp/session.hh>
#include <uvgrtp/media_stream.hh>

struct ChannelJoinedData {
    std::string ip;
    int port;
    int rtcp_port;
    int audio_ssrc;
    int audio_pt;
    int bitrate;
    bool rtcp_mux;
};

class RTPInstance {
public:
    // 构造函数，传入远程地址
    explicit RTPInstance(const std::string &remote_address);

    std::string remote_address;

    // 析构函数
    ~RTPInstance();

    // 创建流，传入流标识（可以是字符串）、格式和标志
    std::shared_ptr<uvgrtp::media_stream>
    createStream(const std::string &stream_id, const ChannelJoinedData &streamInfo,
                 RTP_FORMAT format, int flags);

    // 获取流，通过流标识（字符串）
    std::shared_ptr<uvgrtp::media_stream> getStream(const std::string &stream_id);

    void destroyStream(const std::string &stream_id);

    void destroyStream(std::shared_ptr<uvgrtp::media_stream> stream);

    // Getter 方法
    std::shared_ptr<uvgrtp::media_stream> getMainStream() const;

    uint32_t getMainStreamTimestamp() const;

private:
    uvgrtp::context ctx;
    uvgrtp::session *session;
    mutable std::mutex mutex_; // 保护共享资源的互斥锁

    std::shared_ptr<uvgrtp::media_stream> main_stream_;
    std::string main_stream_id_;
    uint32_t main_stream_timestamp_;

    std::unordered_map<std::string, std::shared_ptr<uvgrtp::media_stream>> streams_;
    std::unordered_map<std::shared_ptr<uvgrtp::media_stream>, uint32_t> stream_timestamps_;

    static uint32_t generate_initial_timestamp();
};

#endif // RTP_INSTANCE_H
