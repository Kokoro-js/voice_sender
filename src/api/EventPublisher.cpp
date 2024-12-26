#include "EventPublisher.h"
#include "handlers/Handlers.h"
#include <thread>
#include <chrono>

EventPublisher &EventPublisher::getInstance() {
    static EventPublisher instance;
    return instance;
}

EventPublisher::EventPublisher()
        : context_(1),
          publisher_(context_, zmq::socket_type::pub),
          router(context_, zmq::socket_type::router),
          publisher_bind_address_("tcp://*:5556"),
          responder_bind_address_("tcp://*:5557"),
          initialized_(false) {
    Handlers::getInstance();
    initialize();
}

EventPublisher::~EventPublisher() {
    if (initialized_) {
        publisher_.close();
        router.close();
    }
}

void EventPublisher::publish_event(const std::string &event_message) {
    try {
        zmq::message_t event(event_message.data(), event_message.size());
        auto result = router.send(event, zmq::send_flags::dontwait);

        if (result) {
            LOG(INFO) << "Published: " << event_message;
        } else {
            LOG(WARNING) << "Failed to publish event (message queue may be full)";
        }
    } catch (const zmq::error_t &e) {
        LOG(ERROR) << "ZMQ Error: " << e.what();
    } catch (const std::exception &e) {
        LOG(ERROR) << "Standard Error: " << e.what();
    }
}

void EventPublisher::publish_event(const OMNI::Response &response) {
    try {
        size_t size = response.ByteSizeLong();
        std::vector<uint8_t> serialized_data(size);
        response.SerializeToArray(serialized_data.data(), size);

        std::string routing_id = "OMNI"; // 固定 routingId
        zmq::message_t client_identity(routing_id.begin(), routing_id.end());
        zmq::message_t empty_frame;
        zmq::message_t message_to_send(serialized_data.data(), serialized_data.size());
        router.send(client_identity, zmq::send_flags::sndmore);
        // router.send(empty_frame, zmq::send_flags::sndmore);
        auto result = router.send(message_to_send, zmq::send_flags::dontwait);
        if (!result) {
            LOG(WARNING) << "Failed to publish event (message queue may be full)";
        }
    } catch (const zmq::error_t &e) {
        LOG(ERROR) << "ZMQ Error: " << e.what();
    } catch (const std::exception &e) {
        LOG(ERROR) << "Standard Error: " << e.what();
    }
}

void EventPublisher::initialize() {
    if (!initialized_) {
        try {
            // initialize_publisher();
            initialize_responder();

            std::this_thread::sleep_for(std::chrono::seconds(2));
            initialized_ = true;

        } catch (const zmq::error_t &e) {
            LOG(ERROR) << "ZMQ Error during initialization: " << e.what();
            throw;
        }
    }
}

void EventPublisher::initialize_publisher() {
    try {
        publisher_.bind(publisher_bind_address_);
        LOG(INFO) << "Publisher bound to " << publisher_bind_address_;
    } catch (const zmq::error_t &e) {
        LOG(ERROR) << "Failed to bind publisher: " << e.what();
        throw;
    }
}

void EventPublisher::initialize_responder() {
    try {
        router.bind(responder_bind_address_);
        LOG(INFO) << "Responder bound to " << responder_bind_address_;
    } catch (const zmq::error_t &e) {
        LOG(ERROR) << "Failed to bind responder: " << e.what();
        throw;
    }
}