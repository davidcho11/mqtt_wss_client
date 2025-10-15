#pragma once

#include "event_queue.h"
#include <MQTTAsync.h>
#include <string>
#include <memory>
#include <atomic>
#include <thread>
#include <optional>
#include <filesystem>
#include <chrono>
#include <queue>

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
    int keep_alive_seconds = 20;
    int qos = 1;
    int min_retry_interval = 1;
    int max_retry_interval = 60;
    std::optional<std::string> cert_file_path;  // 인증서 파일 경로 (선택사항)

    // 프로토콜 설정 (수정됨)
    bool use_websockets = true;    // true: WebSocket, false: TCP
    bool use_ssl = true;           // true: 보안(WSS/MQTTS), false: 비보안(WS/MQTT)
    
    int connection_check_interval_ms = 1000; // 연결 체크 간격
    
    // 프로토콜 문자열 반환 헬퍼
    std::string get_protocol_string() const {
        if (use_websockets) {
            return use_ssl ? "wss" : "ws";
        } else {
            return use_ssl ? "ssl" : "tcp";  // 또는 "mqtts" : "mqtt"
        }
    }
    
    // 기본 포트 제안 헬퍼
    int get_default_port() const {
        if (use_websockets) {
            return use_ssl ? 8883 : 8080;  // WSS : WS
        } else {
            return use_ssl ? 8883 : 1883;  // MQTTS : MQTT
        }
    }
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

    void check_connection_health();
    
    MQTTAsync get_client() const { return client_; }

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

    // 인증서 관련 (SSL 사용 시에만 플랫폼별 인증서 추출)
    std::string extract_windows_certificates();
    std::string extract_macos_certificates();
    std::string extract_system_certificates();  // 플랫폼 자동 선택
    std::string setup_ssl_cert();
    // Base64 인코딩 헬퍼
    std::string base64_encode(const unsigned char* data, size_t length);
 
    // MQTT 연결
    bool connect_to_broker();
    void disconnect_from_broker();
    // 작업 처리
    void process_requests();

    // 활동 추적
    void update_last_activity();
    bool detect_sleep_resume();    

    MQTTConfig config_;
    EventQueue& event_queue_;
    MQTTAsync client_;
    
    std::atomic<bool> connected_{false};
    std::atomic<bool> should_stop_{false};
    
    std::string temp_cert_file_;
    
    std::chrono::steady_clock::time_point last_activity_;
    std::chrono::steady_clock::time_point last_check_time_;
    mutable std::mutex activity_mutex_;

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
