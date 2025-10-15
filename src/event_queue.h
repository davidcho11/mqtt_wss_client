#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <string>
#include <chrono>

namespace mqtt_client {

enum class EventType {
    CONNECTED,
    CONNECTION_LOST,
    MESSAGE_ARRIVED,
    DELIVERY_COMPLETE,
    SUBSCRIBE_SUCCESS,
    SUBSCRIBE_FAILURE,
    PUBLISH_SUCCESS,
    PUBLISH_FAILURE,
    ERROR
};

struct MQTTEvent {
    EventType type;
    std::string topic;
    std::string payload;
    std::string message;
    int qos{0};
    int token{0};

    MQTTEvent() = default;
    MQTTEvent(EventType t, const std::string& msg = "") : type(t), message(msg) {}
    MQTTEvent(EventType t, const std::string& topic, const std::string& payload, int qos = 0)
        : type(t), topic(topic), payload(payload), qos(qos) {}
};

class EventQueue {
public:
    EventQueue() = default;
    ~EventQueue() = default;
    EventQueue(const EventQueue&) = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    void push(MQTTEvent event) {
        std::lock_guard<std::mutex> lock(mutex_);
        queue_.push(std::move(event));
        cv_.notify_one();
    }

    std::optional<MQTTEvent> pop(std::chrono::milliseconds timeout = std::chrono::milliseconds(100)) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (cv_.wait_for(lock, timeout, [this] { return !queue_.empty(); })) {
            auto event = std::move(queue_.front());
            queue_.pop();
            return event;
        }
        return std::nullopt;
    }

    std::optional<MQTTEvent> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        auto event = std::move(queue_.front());
        queue_.pop();
        return event;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<MQTTEvent> empty;
        std::swap(queue_, empty);
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<MQTTEvent> queue_;
};

inline const char* event_type_to_string(EventType type) {
    switch (type) {
        case EventType::CONNECTED: return "CONNECTED";
        case EventType::CONNECTION_LOST: return "CONNECTION_LOST";
        case EventType::MESSAGE_ARRIVED: return "MESSAGE_ARRIVED";
        case EventType::DELIVERY_COMPLETE: return "DELIVERY_COMPLETE";
        case EventType::SUBSCRIBE_SUCCESS: return "SUBSCRIBE_SUCCESS";
        case EventType::SUBSCRIBE_FAILURE: return "SUBSCRIBE_FAILURE";
        case EventType::PUBLISH_SUCCESS: return "PUBLISH_SUCCESS";
        case EventType::PUBLISH_FAILURE: return "PUBLISH_FAILURE";
        case EventType::ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

} // namespace mqtt_client
