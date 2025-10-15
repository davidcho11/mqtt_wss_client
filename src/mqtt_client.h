#pragma once

#include "event_queue.h"
#include <MQTTAsync.h>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <optional>
#include <filesystem>

#ifdef _WIN32
    #include <windows.h>
    #include <wincrypt.h>
    #pragma comment(lib, "crypt32.lib")
    #pragma comment(lib, "paho-mqtt3as.lib")
#elif __APPLE__
    #include <Security/Security.h>
    #include <CoreFoundation/CoreFoundation.h>
#endif

namespace fs = std::filesystem;

namespace mqtt_client {

struct MQTTConfig {
    std::string broker_host;
    int broker_port = 8883;
    std::string client_id;
    std::optional<std::string> username;
    std::optional<std::string> password;
    std::string websocket_path = "/mqtt";
    int keep_alive_seconds = 60;
    int qos = 1;
    int min_retry_interval = 1;
    int max_retry_interval = 60;
    std::optional<std::string> cert_file_path;  // 인증서 파일 경로 (선택사항)
    bool use_websockets = true;
};

class MQTTClient {
public:
    explicit MQTTClient(const MQTTConfig& config, EventQueue& event_queue);
    ~MQTTClient();

    MQTTClient(const MQTTClient&) = delete;
    MQTTClient& operator=(const MQTTClient&) = delete;

    // Thread에서 실행될 메인 함수
    void run();
    
    // Thread 중지 요청
    void stop();
    
    // 연결 상태 확인
    bool is_connected() const { return connected_.load(); }
    
    // MQTT 작업 요청 (Thread-safe)
    void request_subscribe(const std::string& topic, int qos = 1);
    void request_publish(const std::string& topic, const std::string& payload, 
                        int qos = 1, bool retained = false);
    void request_unsubscribe(const std::string& topic);

private:
    // 콜백 함수들 (static)
    static void on_connection_lost(void* context, char* cause);
    static int on_message_arrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message);
    static void on_delivery_complete(void* context, MQTTAsync_token token);
    static void on_connect_success(void* context, MQTTAsync_successData* response);
    static void on_connect_failure(void* context, MQTTAsync_failureData* response);
    static void on_subscribe_success(void* context, MQTTAsync_successData* response);
    static void on_subscribe_failure(void* context, MQTTAsync_failureData* response);
    static void on_send_success(void* context, MQTTAsync_successData* response);
    static void on_send_failure(void* context, MQTTAsync_failureData* response);

    // 인증서 관련
    std::string extract_windows_certificates();
    std::string setup_ssl_cert();
    
    // MQTT 연결
    bool connect_to_broker();
    void disconnect_from_broker();
    
    // 작업 처리
    void process_requests();

    MQTTConfig config_;
    EventQueue& event_queue_;
    MQTTAsync client_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    
    std::string temp_cert_file_;
    
    // 작업 큐
    struct WorkItem {
        enum class Type { SUBSCRIBE, PUBLISH, UNSUBSCRIBE };
        Type type;
        std::string topic;
        std::string payload;
        int qos;
        bool retained;
    };
    
    mutable std::mutex work_mutex_;
    std::queue<WorkItem> work_queue_;
};

} // namespace mqtt_client
