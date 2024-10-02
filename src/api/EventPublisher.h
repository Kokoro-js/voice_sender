#ifndef EVENTPUBLISHER_H
#define EVENTPUBLISHER_H

#include <zmq.hpp>
#include <string>
#include <mutex>
#include <iostream>

class EventPublisher {
public:
    // 获取唯一实例的静态方法
    static EventPublisher& getInstance();

    // 发布事件（字符串版本）
    void publish_event(const std::string& event_message);

    // 发布事件（FlatBuffers 版本）
    void publish_event(const uint8_t* data, size_t size);

    // 处理请求/响应
    void handle_request_response();

    // 设置连接字符串
    void set_bind_address(const std::string& bind_address);

private:
    EventPublisher();
    ~EventPublisher();

    // 禁用拷贝构造函数和赋值操作符
    EventPublisher(const EventPublisher&) = delete;
    EventPublisher& operator=(const EventPublisher&) = delete;

    void ensure_initialized();

    zmq::context_t context_;
    zmq::socket_t publisher_;
    zmq::socket_t responder_;
    std::string bind_address_;
    bool initialized_;
    std::mutex mutex_;
};

#endif // EVENTPUBLISHER_H
