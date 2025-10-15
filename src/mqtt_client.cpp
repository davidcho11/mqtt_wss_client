#include "mqtt_client.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>

namespace mqtt_client {

MQTTClient::MQTTClient(const MQTTConfig& config, EventQueue& event_queue)
    : config_(config), event_queue_(event_queue), client_(nullptr) {
    
    if (config_.client_id.empty()) {
        config_.client_id = "mqtt_client_" + 
            std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
    }

    // 활동 시간 초기화
    last_activity_ = std::chrono::steady_clock::now();
    last_check_time_ = std::chrono::steady_clock::now();
}

MQTTClient::~MQTTClient() {
    stop();
}

// ============================================================================
// 인증서 관련 함수
// ============================================================================
// Windows 인증서 추출
std::string MQTTClient::extract_windows_certificates() {
#ifdef _WIN32
    std::ostringstream pem_stream;
    const char* store_names[] = {"ROOT", "CA"};
    
    for (const auto& store_name : store_names) {
        HCERTSTORE hStore = CertOpenSystemStoreA(0, store_name);
        if (!hStore) {
            std::cerr << "[SSL] Failed to open certificate store: " << store_name << std::endl;
            continue;
        }

        PCCERT_CONTEXT pContext = nullptr;
        while ((pContext = CertEnumCertificatesInStore(hStore, pContext)) != nullptr) {
            DWORD pem_size = 0;
            if (CryptBinaryToStringA(pContext->pbCertEncoded, pContext->cbCertEncoded,
                                     CRYPT_STRING_BASE64HEADER, nullptr, &pem_size)) {
                std::vector<char> pem_buffer(pem_size);
                if (CryptBinaryToStringA(pContext->pbCertEncoded, pContext->cbCertEncoded,
                                         CRYPT_STRING_BASE64HEADER, pem_buffer.data(), &pem_size)) {
                    pem_stream << pem_buffer.data();
                }
            }
        }
        CertCloseStore(hStore, 0);
    }
    return pem_stream.str();
#else
    return "";
#endif
}

// macOS 인증서 추출
std::string MQTTClient::extract_macos_certificates() {
#ifdef __APPLE__
    std::ostringstream pem_stream;
    
    // macOS 10.10+ 에서는 SecTrustCopyAnchorCertificates 사용
    CFArrayRef anchor_certs = nullptr;
    OSStatus status = SecTrustCopyAnchorCertificates(&anchor_certs);
    
    if (status == errSecSuccess && anchor_certs) {
        CFIndex count = CFArrayGetCount(anchor_certs);
        std::cout << "[SSL] Found " << count << " anchor certificates" << std::endl;
        
        for (CFIndex i = 0; i < count; i++) {
            SecCertificateRef cert = (SecCertificateRef)CFArrayGetValueAtIndex(anchor_certs, i);
            
            // 인증서를 DER 형식으로 추출
            CFDataRef cert_data = SecCertificateCopyData(cert);
            if (cert_data) {
                const UInt8* der_data = CFDataGetBytePtr(cert_data);
                CFIndex der_length = CFDataGetLength(cert_data);
                
                // DER을 PEM으로 변환
                pem_stream << "-----BEGIN CERTIFICATE-----\n";
                
                std::string base64 = base64_encode(der_data, der_length);
                for (size_t j = 0; j < base64.length(); j += 64) {
                    pem_stream << base64.substr(j, 64) << "\n";
                }
                
                pem_stream << "-----END CERTIFICATE-----\n";
                
                CFRelease(cert_data);
            }
        }
        
        CFRelease(anchor_certs);
    } else {
        std::cerr << "[SSL] Failed to get anchor certificates: " << status << std::endl;
    }
    
    return pem_stream.str();
#else
    return "";
#endif
}

// 플랫폼 자동 선택
std::string MQTTClient::extract_system_certificates() {
#ifdef _WIN32
    std::cout << "[SSL] Extracting Windows system certificates..." << std::endl;
    return extract_windows_certificates();
#elif __APPLE__
    std::cout << "[SSL] Extracting macOS system certificates..." << std::endl;
    return extract_macos_certificates();
#elif __linux__
    std::cout << "[SSL] Linux detected - using system cert paths" << std::endl;
    return "";  // Linux는 /etc/ssl/certs 직접 사용
#else
    std::cerr << "[SSL] Unsupported platform for certificate extraction" << std::endl;
    return "";
#endif
}

std::string MQTTClient::setup_ssl_cert() {
    // 1. 사용자 지정 인증서 파일
    if (config_.cert_file_path.has_value() && fs::exists(config_.cert_file_path.value())) {
        std::cout << "[SSL] Using provided certificate file: " << config_.cert_file_path.value() << std::endl;
        return config_.cert_file_path.value();
    }
    
    // 2. macOS - OpenSSL 설치 경로 확인
#ifdef __APPLE__
    std::vector<std::string> macos_cert_paths = {
        "/etc/ssl/cert.pem",                           // macOS 시스템 기본
        "/usr/local/etc/openssl@3/cert.pem",          // Homebrew OpenSSL 3
        "/usr/local/etc/openssl@1.1/cert.pem",        // Homebrew OpenSSL 1.1
        "/opt/homebrew/etc/openssl@3/cert.pem",       // Apple Silicon Homebrew
        "/opt/homebrew/etc/openssl@1.1/cert.pem",     // Apple Silicon Homebrew
        "/usr/local/etc/openssl/cert.pem"             // Homebrew 구버전
    };
    
    for (const auto& path : macos_cert_paths) {
        if (fs::exists(path)) {
            std::cout << "[SSL] Using macOS certificate bundle: " << path << std::endl;
            return path;
        }
    }
    
    std::cout << "[SSL] No pre-installed certificate bundle found, extracting from system..." << std::endl;
#endif
    
    // 3. Linux - 시스템 경로 사용
#ifdef __linux__
    std::vector<std::string> linux_cert_paths = {
        "/etc/ssl/certs/ca-certificates.crt",  // Debian/Ubuntu
        "/etc/pki/tls/certs/ca-bundle.crt",    // RedHat/CentOS
        "/etc/ssl/ca-bundle.pem",               // OpenSUSE
        "/etc/ssl/cert.pem"                     // Generic
    };
    
    for (const auto& path : linux_cert_paths) {
        if (fs::exists(path)) {
            std::cout << "[SSL] Using Linux system certificates: " << path << std::endl;
            return path;
        }
    }
#endif
    
    // 4. 시스템 인증서 추출 (Windows 또는 macOS에서 경로를 못 찾은 경우)
    std::cout << "[SSL] Extracting system certificates..." << std::endl;
    std::string pem_certs = extract_system_certificates();
    
    if (pem_certs.empty()) {
        throw std::runtime_error("Failed to extract system certificates and no cert file provided");
    }
    
    // 임시 파일 저장
    fs::path temp_dir = fs::temp_directory_path();
    temp_cert_file_ = (temp_dir / ("mqtt_certs_" + config_.client_id + ".pem")).string();
    
    std::ofstream cert_file(temp_cert_file_, std::ios::binary);
    if (!cert_file) {
        throw std::runtime_error("Failed to create temporary certificate file");
    }
    cert_file << pem_certs;
    cert_file.close();
    
    std::cout << "[SSL] Temporary certificate file created: " << temp_cert_file_ << std::endl;
    return temp_cert_file_;
}

// Base64 인코딩 (macOS/크로스 플랫폼용)
std::string MQTTClient::base64_encode(const unsigned char* data, size_t length) {
    static const char base64_chars[] = 
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "abcdefghijklmnopqrstuvwxyz"
        "0123456789+/";
    
    std::string ret;
    int i = 0;
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];
    
    while (length--) {
        char_array_3[i++] = *(data++);
        if (i == 3) {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;
            
            for(i = 0; i < 4; i++)
                ret += base64_chars[char_array_4[i]];
            i = 0;
        }
    }
    
    if (i) {
        for(int j = i; j < 3; j++)
            char_array_3[j] = '\0';
        
        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        
        for (int j = 0; j < i + 1; j++)
            ret += base64_chars[char_array_4[j]];
        
        while(i++ < 3)
            ret += '=';
    }
    
    return ret;
}
// ============================================================================
// 활동 추적
// ============================================================================
void MQTTClient::update_last_activity() {
    std::lock_guard<std::mutex> lock(activity_mutex_);
    last_activity_ = std::chrono::steady_clock::now();
}

bool MQTTClient::detect_sleep_resume() {
    std::lock_guard<std::mutex> lock(activity_mutex_);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        now - last_check_time_).count();
    
    // 체크 간격보다 훨씬 오래 걸렸으면 sleep으로 판단
    // 예: 1초 간격인데 5초 이상 걸렸으면 비정상
    int expected_interval_sec = config_.connection_check_interval_ms / 1000 + 1;
    if (elapsed > expected_interval_sec * 3) {
        std::cout << "[Health] Detected unusual delay: " << elapsed 
                  << " seconds (expected ~" << expected_interval_sec 
                  << "s) - possible sleep/resume" << std::endl;
        last_check_time_ = now;
        return true;
    }
    
    last_check_time_ = now;
    return false;
}

void MQTTClient::check_connection_health() {
    // Sleep 복구 감지
    bool sleep_detected = detect_sleep_resume();
    
    if (!connected_.load()) {
        return;  // 이미 연결 끊김 상태
    }
    
    // Paho의 연결 상태 확인
    int is_connected = MQTTAsync_isConnected(client_);
    
    if (!is_connected) {
        std::cout << "[Health] Connection lost detected by isConnected()" << std::endl;
        connected_.store(false);
        event_queue_.push(MQTTEvent(EventType::CONNECTION_LOST, 
                                    "Stale connection detected"));
        // 자동 재연결이 작동할 것임
        return;
    }
    
    // Sleep 복구 후 명시적 확인
    if (sleep_detected) {
        std::cout << "[Health] Sleep detected - verifying connection..." << std::endl;
        
        // 활동이 오래 없었는지 확인
        std::lock_guard<std::mutex> lock(activity_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto no_activity_sec = std::chrono::duration_cast<std::chrono::seconds>(
            now - last_activity_).count();
        
        // Keep-alive 간격의 2배 이상 활동 없으면 의심
        if (no_activity_sec > config_.keep_alive_seconds * 2) {
            std::cout << "[Health] No activity for " << no_activity_sec 
                      << " seconds - forcing reconnect" << std::endl;
            
            connected_.store(false);
            
            // 명시적 재연결 (자동 재연결 대신)
            std::thread([this]() {
                std::cout << "[Health] Disconnecting stale connection..." << std::endl;
                
                MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
                disc_opts.timeout = 1000;
                MQTTAsync_disconnect(client_, &disc_opts);
                
                std::this_thread::sleep_for(std::chrono::seconds(2));
                
                std::cout << "[Health] Attempting reconnect..." << std::endl;
                // 자동 재연결이 작동하도록 놔둠
            }).detach();
        }
    }
    
    // 정상 활동 기록
    update_last_activity();
}

// ============================================================================
// MQTT 연결
// ============================================================================
bool MQTTClient::connect_to_broker() {
    // Server URI 생성
    std::string server_uri;
    std::string protocol = config_.get_protocol_string();
    
    if (config_.use_websockets) {
        server_uri = protocol + "://" + config_.broker_host + ":" + 
                     std::to_string(config_.broker_port) + config_.websocket_path;
    } else {
        server_uri = protocol + "://" + config_.broker_host + ":" + 
                     std::to_string(config_.broker_port);
    }
    
    // MQTT 클라이언트 생성
    std::cout << "[MQTT] Creating client: " << server_uri << std::endl;
    std::cout << "[MQTT] Protocol: " << protocol 
              << " (WebSocket: " << (config_.use_websockets ? "Yes" : "No")
              << ", SSL: " << (config_.use_ssl ? "Yes" : "No") << ")" << std::endl;
    
    int rc = MQTTAsync_create(&client_, server_uri.c_str(), config_.client_id.c_str(),
                              MQTTCLIENT_PERSISTENCE_NONE, nullptr);
    if (rc != MQTTASYNC_SUCCESS) {
        event_queue_.push(MQTTEvent(EventType::ERROR, "Failed to create MQTT client"));
        return false;
    }
    
    // 콜백 설정
    rc = MQTTAsync_setCallbacks(client_, this, on_connection_lost, 
                                on_message_arrived, on_delivery_complete);
    if (rc != MQTTASYNC_SUCCESS) {
        event_queue_.push(MQTTEvent(EventType::ERROR, "Failed to set callbacks"));
        MQTTAsync_destroy(&client_);
        return false;
    }
    
    // 연결 옵션 설정
    MQTTAsync_connectOptions conn_opts = MQTTAsync_connectOptions_initializer;
    conn_opts.keepAliveInterval = config_.keep_alive_seconds;
    conn_opts.cleansession = 1;
    conn_opts.automaticReconnect = 1; // 자동 재연결 활성화
    conn_opts.minRetryInterval = config_.min_retry_interval;
    conn_opts.maxRetryInterval = config_.max_retry_interval;
    conn_opts.onSuccess = on_connect_success;
    conn_opts.onFailure = on_connect_failure;
    conn_opts.context = this;
    
    // SSL 설정
    MQTTAsync_SSLOptions ssl_opts = MQTTAsync_SSLOptions_initializer;
    if (config_.use_ssl) {
        try {
            std::string cert_file = setup_ssl_cert();
            ssl_opts.trustStore = cert_file.c_str();
            ssl_opts.enableServerCertAuth = 1;
            conn_opts.ssl = &ssl_opts;
            std::cout << "[MQTT] SSL/TLS enabled" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[MQTT] SSL setup failed: " << e.what() << std::endl;
            MQTTAsync_destroy(&client_);
            return false;
        }
    } else {
        std::cout << "[MQTT] SSL/TLS disabled (insecure connection)" << std::endl;
    }
    
    if (config_.username.has_value()) {
        conn_opts.username = config_.username.value().c_str();
    }
    if (config_.password.has_value()) {
        conn_opts.password = config_.password.value().c_str();
    }
    
    std::cout << "[MQTT] Connecting to broker..." << std::endl;
    rc = MQTTAsync_connect(client_, &conn_opts);
    if (rc != MQTTASYNC_SUCCESS) {
        event_queue_.push(MQTTEvent(EventType::ERROR, "Failed to start connect"));
        MQTTAsync_destroy(&client_);
        return false;
    }
    
    return true;
}

void MQTTClient::disconnect_from_broker() {
    if (client_) {
        MQTTAsync_disconnectOptions disc_opts = MQTTAsync_disconnectOptions_initializer;
        disc_opts.timeout = 1000;
        MQTTAsync_disconnect(client_, &disc_opts);
        MQTTAsync_destroy(&client_);
        client_ = nullptr;
    }
    
    // 임시 인증서 파일 삭제
    if (!temp_cert_file_.empty() && fs::exists(temp_cert_file_)) {
        try {
            fs::remove(temp_cert_file_);
            std::cout << "[SSL] Temporary certificate file removed" << std::endl;
        } catch (const std::exception& e) {
            std::cerr << "[SSL] Failed to remove temporary certificate file: " << e.what() << std::endl;
        }
    }
}

void MQTTClient::run() {
    std::cout << "[Thread] MQTT thread started" << std::endl;
    
    if (!connect_to_broker()) {
        std::cerr << "[Thread] Failed to connect to broker" << std::endl;
        return;
    }
    
    auto last_health_check = std::chrono::steady_clock::now();
    
    // 메인 루프
    while (!should_stop_.load()) {
        process_requests();
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_health_check).count();
        
        if (elapsed_ms >= config_.connection_check_interval_ms) {
            check_connection_health();
            last_health_check = now;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    std::cout << "[Thread] Disconnecting..." << std::endl;
    disconnect_from_broker();
    connected_.store(false);
    std::cout << "[Thread] MQTT thread stopped" << std::endl;
}

void MQTTClient::stop() {
    std::cout << "[Thread] Stop requested" << std::endl;
    should_stop_.store(true);
}

void MQTTClient::process_requests() {
    std::lock_guard<std::mutex> lock(work_mutex_);
    
    while (!work_queue_.empty() && connected_.load()) {
        auto item = work_queue_.front();
        work_queue_.pop();
        
        switch (item.type) {
            case WorkItem::Type::SUBSCRIBE: {
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                opts.onSuccess = on_subscribe_success;
                opts.onFailure = on_subscribe_failure;
                opts.context = this;
                
                int rc = MQTTAsync_subscribe(client_, item.topic.c_str(), item.qos, &opts);
                if (rc != MQTTASYNC_SUCCESS) {
                    event_queue_.push(MQTTEvent(EventType::SUBSCRIBE_FAILURE, 
                                               "Subscribe request failed: " + item.topic));
                }
                break;
            }
            case WorkItem::Type::PUBLISH: {
                MQTTAsync_message pubmsg = MQTTAsync_message_initializer;
                pubmsg.payload = const_cast<char*>(item.payload.c_str());
                pubmsg.payloadlen = static_cast<int>(item.payload.length());
                pubmsg.qos = item.qos;
                pubmsg.retained = item.retained;
                
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                opts.onSuccess = on_send_success;
                opts.onFailure = on_send_failure;
                opts.context = this;
                
                int rc = MQTTAsync_sendMessage(client_, item.topic.c_str(), &pubmsg, &opts);
                if (rc != MQTTASYNC_SUCCESS) {
                    event_queue_.push(MQTTEvent(EventType::PUBLISH_FAILURE,
                                               "Publish request failed: " + item.topic));
                }
                break;
            }
            case WorkItem::Type::UNSUBSCRIBE: {
                MQTTAsync_responseOptions opts = MQTTAsync_responseOptions_initializer;
                opts.context = this;
                MQTTAsync_unsubscribe(client_, item.topic.c_str(), &opts);
                break;
            }
        }
    }
}

void MQTTClient::request_subscribe(const std::string& topic, int qos) {
    std::lock_guard<std::mutex> lock(work_mutex_);
    WorkItem item;
    item.type = WorkItem::Type::SUBSCRIBE;
    item.topic = topic;
    item.qos = qos;
    work_queue_.push(item);
}

void MQTTClient::request_publish(const std::string& topic, const std::string& payload,
                                 int qos, bool retained) {
    std::lock_guard<std::mutex> lock(work_mutex_);
    WorkItem item;
    item.type = WorkItem::Type::PUBLISH;
    item.topic = topic;
    item.payload = payload;
    item.qos = qos;
    item.retained = retained;
    work_queue_.push(item);
}

void MQTTClient::request_unsubscribe(const std::string& topic) {
    std::lock_guard<std::mutex> lock(work_mutex_);
    WorkItem item;
    item.type = WorkItem::Type::UNSUBSCRIBE;
    item.topic = topic;
    work_queue_.push(item);
}

// ============================================================================
// 콜백 함수들
// ============================================================================

void MQTTClient::on_connection_lost(void* context, char* cause) {
    auto* client = static_cast<MQTTClient*>(context);
    client->connected_.store(false);
    std::string cause_str = cause ? std::string(cause) : "Unknown";
    client->event_queue_.push(MQTTEvent(EventType::CONNECTION_LOST, cause_str));
    std::cout << "[Callback] Connection lost: " << cause_str << std::endl;
}

int MQTTClient::on_message_arrived(void* context, char* topicName, int topicLen, MQTTAsync_message* message) {
    auto* client = static_cast<MQTTClient*>(context);
    client->update_last_activity();
    
    std::string topic(topicName);
    std::string payload(static_cast<char*>(message->payload), message->payloadlen);
    
    client->event_queue_.push(MQTTEvent(EventType::MESSAGE_ARRIVED, topic, payload, message->qos));
    
    MQTTAsync_freeMessage(&message);
    MQTTAsync_free(topicName);
    return 1;
}

void MQTTClient::on_delivery_complete(void* context, MQTTAsync_token token) {
    auto* client = static_cast<MQTTClient*>(context);
    client->update_last_activity();
    
    MQTTEvent event(EventType::DELIVERY_COMPLETE);
    event.token = token;
    client->event_queue_.push(event);
}

void MQTTClient::on_connect_success(void* context, MQTTAsync_successData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    client->connected_.store(true);
    client->update_last_activity();
    client->event_queue_.push(MQTTEvent(EventType::CONNECTED, "Connected to broker"));
    std::cout << "[Callback] Connected successfully" << std::endl;
}

void MQTTClient::on_connect_failure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    std::string error_msg = response && response->message ? 
                           std::string(response->message) : "Unknown error";
    client->event_queue_.push(MQTTEvent(EventType::ERROR, "Connection failed: " + error_msg));
    std::cerr << "[Callback] Connection failed: " << error_msg << std::endl;
}

void MQTTClient::on_subscribe_success(void* context, MQTTAsync_successData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    client->event_queue_.push(MQTTEvent(EventType::SUBSCRIBE_SUCCESS, "Subscription successful"));
}

void MQTTClient::on_subscribe_failure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    std::string error_msg = response && response->message ?
                           std::string(response->message) : "Unknown error";
    client->event_queue_.push(MQTTEvent(EventType::SUBSCRIBE_FAILURE, "Subscribe failed: " + error_msg));
}

void MQTTClient::on_send_success(void* context, MQTTAsync_successData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    client->event_queue_.push(MQTTEvent(EventType::PUBLISH_SUCCESS, "Message published"));
}

void MQTTClient::on_send_failure(void* context, MQTTAsync_failureData* response) {
    auto* client = static_cast<MQTTClient*>(context);
    std::string error_msg = response && response->message ?
                           std::string(response->message) : "Unknown error";
    client->event_queue_.push(MQTTEvent(EventType::PUBLISH_FAILURE, "Publish failed: " + error_msg));
}

} // namespace mqtt_client
