#include "mqtt_client.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace mqtt_client;

std::atomic<bool> g_running{true};

void signal_handler(int signal) {
    std::cout << "\n[Main] Received signal " << signal << ", shutting down..." << std::endl;
    g_running.store(false);
}

class EventHandler {
public:
    explicit EventHandler(MQTTClient& client) : client_(client) {}
    
    // 이벤트 처리 - main에서 호출됨
    bool handle_event(const MQTTEvent& event) {
        std::cout << "\n[EventHandler] Received: " << event_type_to_string(event.type) << std::endl;
        
        switch (event.type) {
            case EventType::CONNECTED:
                on_connected();
                return true;
                
            case EventType::CONNECTION_LOST:
                on_connection_lost(event.message);
                return true;
                
            case EventType::MESSAGE_ARRIVED:
                return on_message_arrived(event.topic, event.payload, event.qos);
                
            case EventType::SUBSCRIBE_SUCCESS:
                on_subscribe_success();
                return true;
                
            case EventType::PUBLISH_SUCCESS:
                on_publish_success();
                return true;
                
            case EventType::ERROR:
                on_error(event.message);
                return true;
                
            default:
                return true;
        }
    }
    
private:
    bool on_connected() {
        std::cout << "[EventHandler] ✓ Successfully connected to broker!" << std::endl;
        std::cout << "[EventHandler] Subscribing to test topics..." << std::endl;
        
        // 구독 요청
        client_.request_subscribe("test/topic", 1);
        client_.request_subscribe("system/status", 1);
        client_.request_subscribe("control/stop", 1);  // 종료 명령 토픽
        return true;
    }
    
    bool on_connection_lost(const std::string& cause) {
        std::cout << "[EventHandler] ✗ Connection lost: " << cause << std::endl;
        std::cout << "[EventHandler] Auto-reconnection will be attempted..." << std::endl;
        return true;
    }
    
    bool on_message_arrived(const std::string& topic, const std::string& payload, int qos) {
        std::cout << "[EventHandler] Message arrived:" << std::endl;
        std::cout << "  Topic: " << topic << std::endl;
        std::cout << "  Payload: " << payload << std::endl;
        std::cout << "  QoS: " << qos << std::endl;
        
        // 특정 메시지 수신 시 종료
        if (topic == "control/stop" && payload == "shutdown") {
            std::cout << "\n[EventHandler] ⚠️ Shutdown command received!" << std::endl;
            std::cout << "[EventHandler] Stopping MQTT thread and exiting..." << std::endl;
            return false;  // false 반환 시 프로그램 종료
        }
        
        // Echo 응답 (테스트용)
        if (topic == "test/topic") {
            std::string response = "Echo: " + payload;
            client_.request_publish("test/response", response, 1, false);
        }
        
        return true;  // 계속 실행
    }
    
    bool on_subscribe_success() {
        std::cout << "[EventHandler] ✓ Subscription successful" << std::endl;
        
        // 구독 성공 후 테스트 메시지 발행
        message_count_++;
        if (message_count_ <= 3) {  // 처음 3개 구독 완료 후
            std::cout << "[EventHandler] Publishing test message..." << std::endl;
            client_.request_publish("test/topic", "Hello MQTT! Test message #1", 1, false);
        }
        return true;
    }
    
    bool on_publish_success() {
        std::cout << "[EventHandler] ✓ Message published successfully" << std::endl;
        return true;
    }
    
    bool on_error(const std::string& error) {
        std::cerr << "[EventHandler] ✗ Error: " << error << std::endl;
        return true;
    }
    
    MQTTClient& client_;
    int message_count_ = 0;
};

void print_usage() {
    std::cout << R"(
========================================
MQTT Client Test Program (WS/WSS/TCP/SSL)
========================================

Usage:
  mqtt_client_test [options] <broker_host> <port>

Options:
  --ws         Use WebSocket (default)
  --tcp        Use TCP instead of WebSocket
  --ssl        Use SSL/TLS (default)
  --no-ssl     Disable SSL/TLS (insecure)
  --cert PATH  Custom certificate file
  -h, --help   Show this help

Examples:
  # WSS (WebSocket Secure) - Default
  mqtt_client_test test.mosquitto.org 8883

  # WS (WebSocket without SSL) - Insecure
  mqtt_client_test --no-ssl test.mosquitto.org 8080

  # MQTTS (MQTT over SSL)
  mqtt_client_test --tcp --ssl broker.hivemq.com 8883

  # MQTT (Plain TCP) - Insecure
  mqtt_client_test --tcp --no-ssl test.mosquitto.org 1883

  # With custom certificate
  mqtt_client_test --cert ca.crt broker.example.com 8883

Protocol Combinations:
  WebSocket + SSL     = wss://   (Port 8883, 443)
  WebSocket + No SSL  = ws://    (Port 8080, 8083)
  TCP + SSL           = ssl://   (Port 8883)
  TCP + No SSL        = tcp://   (Port 1883)

Test Commands:
  - Subscribe to: test/topic, system/status, control/stop
  - Publish to: test/topic (will echo to test/response)
  - Send "shutdown" to "control/stop" to gracefully exit

Press Ctrl+C to exit
========================================
)" << std::endl;
}

int main(int argc, char* argv[]) {
    // 사용법 출력
    if (argc < 2) {
        print_usage();
        std::cout << "\nStarting with default test broker (test.mosquitto.org)..." << std::endl;
    }
    
    // 시그널 핸들러 등록
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    try {
        // 설정
        MQTTConfig config;
        config.use_websockets = true;
        config.use_ssl = true;
        
        std::string broker_host;
        int broker_port = 0;
        
        // 인증서 파일 (선택사항)
        int arg_idx = 1;
        while (arg_idx < argc) {
            std::string arg = argv[arg_idx];
            
            if (arg == "--ws") {
                config.use_websockets = true;
                arg_idx++;
            } else if (arg == "--tcp") {
                config.use_websockets = false;
                arg_idx++;
            } else if (arg == "--ssl") {
                config.use_ssl = true;
                arg_idx++;
            } else if (arg == "--no-ssl") {
                config.use_ssl = false;
                arg_idx++;
            } else if (arg == "--cert" && arg_idx + 1 < argc) {
                config.cert_file_path = argv[arg_idx + 1];
                arg_idx += 2;
            } else if (arg == "-h" || arg == "--help") {
                print_usage();
                return 0;
            } else {
                if (broker_host.empty()) {
                    broker_host = arg;
                } else if (broker_port == 0) {
                    broker_port = std::atoi(arg.c_str());
                }
                arg_idx++;
            }
        }
        
        if (broker_host.empty()) {
            broker_host = "test.mosquitto.org";
        }
        
        config.broker_host = broker_host;
        
        if (broker_port == 0) {
            broker_port = config.get_default_port();
        }
        config.broker_port = broker_port;
        
        config.client_id = "cpp_mqtt_test_client";
        config.websocket_path = "/mqtt";
        config.keep_alive_seconds = 20;
        config.connection_check_interval_ms = 1000;

        // 인증 (필요한 경우)
        // config.username = "username";
        // config.password = "password";
        
        std::cout << "\n[Main] Configuration:" << std::endl;
        std::cout << "  Broker: " << config.broker_host << ":" << config.broker_port << std::endl;
        std::cout << "  Protocol: " << config.get_protocol_string() << "://" << std::endl;
        std::cout << "  WebSocket: " << (config.use_websockets ? "Yes" : "No") << std::endl;
        std::cout << "  SSL/TLS: " << (config.use_ssl ? "Yes" : "No") << std::endl;
        std::cout << "  Client ID: " << config.client_id << std::endl;
        
        if (config.cert_file_path.has_value()) {
            std::cout << "  Certificate: " << config.cert_file_path.value() << std::endl;
        } else if (config.use_ssl) {
            std::cout << "  Certificate: System certificates" << std::endl;
        }
        
        if (!config.use_ssl) {
            std::cout << "\n  ⚠️  WARNING: SSL/TLS is disabled - connection is NOT secure!" << std::endl;
        }
        
        std::cout << std::endl;
        
        // 이벤트 큐 생성
        EventQueue event_queue;
        // MQTT 클라이언트 생성
        MQTTClient mqtt_client(config, event_queue);
        // 이벤트 핸들러 생성
        EventHandler event_handler(mqtt_client);
        
        // MQTT 스레드 시작
        std::cout << "[Main] Starting MQTT thread..." << std::endl;
        std::thread mqtt_thread([&mqtt_client]() {
            mqtt_client.run();
        });
        
        // Main 루프 - 이벤트 처리
        std::cout << "[Main] Entering event loop..." << std::endl;
        std::cout << "[Main] Waiting for events...\n" << std::endl;
        
        auto last_status_time = std::chrono::steady_clock::now();
        int event_count = 0;
        
        while (g_running.load()) {
            // 이벤트 처리 (100ms 타임아웃)
            auto event = event_queue.pop(std::chrono::milliseconds(100));
            
            if (event.has_value()) {
                event_count++;
                // 이벤트 핸들러에서 처리
                bool should_continue = event_handler.handle_event(event.value());
                // false 반환 시 종료
                if (!should_continue) {
                    g_running.store(false);
                    break;
                }
            }
            // 주기적인 상태 출력 (30초마다)
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_status_time);
            
            if (elapsed.count() >= 30) {
                std::cout << "\n[Main] Status Report:" << std::endl;
                std::cout << "  Connected: " << (mqtt_client.is_connected() ? "Yes" : "No") << std::endl;
                std::cout << "  Events processed: " << event_count << std::endl;
                std::cout << "  Queue size: " << event_queue.size() << std::endl;
                std::cout << "  Protocol: " << config.get_protocol_string() << "://" << std::endl;
                std::cout << std::endl;
                
                last_status_time = now;
                // 주기적인 테스트 메시지 발행
                if (mqtt_client.is_connected()) {
                    auto timestamp = std::chrono::system_clock::now().time_since_epoch().count();
                    std::string msg = "Status update: " + std::to_string(timestamp);
                    mqtt_client.request_publish("system/status", msg, 1, false);
                }
            }
        }
        // 정리
        std::cout << "\n[Main] Shutting down..." << std::endl;
        mqtt_client.stop();
        
        std::cout << "[Main] Waiting for MQTT thread to finish..." << std::endl;
        if (mqtt_thread.joinable()) {
            mqtt_thread.join();
        }
        
        std::cout << "[Main] Cleanup completed" << std::endl;
        std::cout << "[Main] Total events processed: " << event_count << std::endl;
        std::cout << "\n[Main] Goodbye!" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "\n[Main] Fatal error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
