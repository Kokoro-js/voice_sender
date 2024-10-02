#include "EventPublisher.h"

#include "handlers/Handlers.h"

// 获取唯一实例
EventPublisher& EventPublisher::getInstance() {
    static EventPublisher instance;
    return instance;
}

// 构造函数
EventPublisher::EventPublisher()
    : context_(1),
      publisher_(context_, zmq::socket_type::pub),
      responder_(context_, zmq::socket_type::rep),
      bind_address_("tcp://*:5556"),
      initialized_(false) {
    Handlers::getInstance();
    std::atexit([]() {
        EventPublisher::getInstance().~EventPublisher();
    });
}

// 析构函数
EventPublisher::~EventPublisher() {
    if (initialized_) {
        publisher_.close();
        responder_.close();
    }
}

// 发布事件（字符串版本）
void EventPublisher::publish_event(const std::string& event_message) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_initialized();

        zmq::message_t event(event_message.size());
        memcpy(event.data(), event_message.c_str(), event_message.size());
        publisher_.send(event, zmq::send_flags::none);
        std::cout << "Published: " << event_message << std::endl;
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ Error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Standard Error: " << e.what() << std::endl;
    }
}

// 发布事件（FlatBuffers 版本）
void EventPublisher::publish_event(const uint8_t* data, size_t size) {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        ensure_initialized();

        zmq::message_t event(size);
        memcpy(event.data(), data, size);
        publisher_.send(event, zmq::send_flags::none);
        std::cout << "Published FlatBuffers event" << std::endl;
    } catch (const zmq::error_t& e) {
        std::cerr << "ZMQ Error: " << e.what() << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Standard Error: " << e.what() << std::endl;
    }
}

// 设置连接字符串
void EventPublisher::set_bind_address(const std::string& bind_address) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (initialized_) {
        publisher_.close();
        responder_.close();
        initialized_ = false;
    }
    bind_address_ = bind_address;
}

// 确保在首次使用前初始化 ZeroMQ socket
void EventPublisher::ensure_initialized() {
    if (!initialized_) {
        publisher_.bind(bind_address_);
        responder_.bind("tcp://*:5557");  // 设置 REQ/REP 模式的端口
        initialized_ = true;
    }
}


